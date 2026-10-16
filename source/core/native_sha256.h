#pragma once

#include <QByteArray>
#include <QString>
#ifdef Q_OS_WIN
#include <qt_windows.h>
#include <bcrypt.h>
#endif

// Optional system SHA-256. An unavailable/failed backend returns no digest;
// the caller must recompute the complete input with its portable backend.
class CNativeSha256
{
public:
    CNativeSha256()
    {
#if defined(Q_OS_WIN) && !defined(EI_DISABLE_NATIVE_SHA256)
        const auto& api = functions();
        if(api.open && api.create && api.update && api.finish && api.destroy && api.close
           && api.open(&m_algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) >= 0)
            m_ok = api.create(m_algorithm, &m_hash, nullptr, 0, nullptr, 0, 0) >= 0;
#endif
    }
    ~CNativeSha256()
    {
#ifdef Q_OS_WIN
        const auto& api = functions();
        if(m_hash) api.destroy(m_hash);
        if(m_algorithm) api.close(m_algorithm, 0);
#endif
    }
    CNativeSha256(const CNativeSha256&) = delete;
    CNativeSha256& operator=(const CNativeSha256&) = delete;
    bool isAvailable() const { return m_ok; }
    void addData(const QByteArray& bytes) { addData(bytes.constData(), bytes.size()); }
    void addData(const char* data, int size)
    {
        if(size < 0 || (!data && size > 0)) { m_ok = false; return; }
#ifdef Q_OS_WIN
        if(m_ok && size > 0)
            m_ok = functions().update(m_hash,
                reinterpret_cast<PUCHAR>(const_cast<char*>(data)), ULONG(size), 0) >= 0;
#else
        Q_UNUSED(data); Q_UNUSED(size);
#endif
    }
    QByteArray result()
    {
#ifdef Q_OS_WIN
        QByteArray digest(32, '\0');
        if(m_ok && functions().finish(m_hash,
                reinterpret_cast<PUCHAR>(digest.data()), ULONG(digest.size()), 0) >= 0)
        {
            m_ok = false; // A finished hash cannot be reused.
            return digest;
        }
#endif
        m_ok = false;
        return {};
    }
private:
    bool m_ok{false};
#ifdef Q_OS_WIN
    struct SFunctions
    {
        decltype(&BCryptOpenAlgorithmProvider) open{nullptr};
        decltype(&BCryptCreateHash) create{nullptr};
        decltype(&BCryptHashData) update{nullptr};
        decltype(&BCryptFinishHash) finish{nullptr};
        decltype(&BCryptDestroyHash) destroy{nullptr};
        decltype(&BCryptCloseAlgorithmProvider) close{nullptr};
        SFunctions()
        {
            wchar_t systemPath[MAX_PATH];
            const UINT length = GetSystemDirectoryW(systemPath, MAX_PATH);
            if(!length || length >= MAX_PATH) return;
            const QString path = QString::fromWCharArray(systemPath) + QStringLiteral("/bcrypt.dll");
            // Keep the system module loaded for process lifetime, including
            // late worker cleanup. Never search the app folder or PATH.
            HMODULE module = LoadLibraryW(reinterpret_cast<LPCWSTR>(path.utf16()));
            if(!module) return;
            open = reinterpret_cast<decltype(open)>(GetProcAddress(module, "BCryptOpenAlgorithmProvider"));
            create = reinterpret_cast<decltype(create)>(GetProcAddress(module, "BCryptCreateHash"));
            update = reinterpret_cast<decltype(update)>(GetProcAddress(module, "BCryptHashData"));
            finish = reinterpret_cast<decltype(finish)>(GetProcAddress(module, "BCryptFinishHash"));
            destroy = reinterpret_cast<decltype(destroy)>(GetProcAddress(module, "BCryptDestroyHash"));
            close = reinterpret_cast<decltype(close)>(GetProcAddress(module, "BCryptCloseAlgorithmProvider"));
        }
    };
    static const SFunctions& functions() { static const SFunctions api; return api; }
    BCRYPT_ALG_HANDLE m_algorithm{nullptr};
    BCRYPT_HASH_HANDLE m_hash{nullptr};
#endif
};
