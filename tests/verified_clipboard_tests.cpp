#include <QtTest>
#include <QApplication>
#include <QClipboard>
#include <QElapsedTimer>
#include <QImage>
#include <QBuffer>
#include <QLineEdit>
#include <QMenu>
#include <QMimeData>
#include <QProcess>
#include <QPointer>
#include <QFile>
#include <QTemporaryDir>
#include <QTimer>
#include <QToolTip>
#include <QUrl>
#include <climits>
#include <cstring>
#include "verified_clipboard.h"
#ifdef Q_OS_WIN
#include <qt_windows.h>
#include <ole2.h>
#endif

#ifdef Q_OS_WIN
// Native fixtures and readback deliberately avoid Qt's image MIME conversion.
static bool openForCheck(HWND window)
{
    QElapsedTimer timeout;
    timeout.start();
    do {
        if(OpenClipboard(window)) return true;
        QTest::qWait(10);
    } while(timeout.elapsed() < 1000);
    return false;
}
static QByteArray rasterDib(bool indexed)
{
    BITMAPINFOHEADER header = {};
    header.biSize = sizeof(header);
    header.biWidth = 2;
    header.biHeight = 2;
    header.biPlanes = 1;
    header.biBitCount = indexed ? 8 : 24;
    header.biSizeImage = indexed ? 8 : 16;
    header.biClrUsed = indexed ? 2 : 0;
    QByteArray bytes(int(sizeof(header) + header.biClrUsed * sizeof(RGBQUAD) + header.biSizeImage), '\0');
    memcpy(bytes.data(), &header, sizeof(header));
    if(indexed) {
        const RGBQUAD colors[2] = {{10, 20, 30, 0}, {70, 80, 90, 0}};
        memcpy(bytes.data() + sizeof(header), colors, sizeof(colors));
        bytes[int(sizeof(header) + sizeof(colors))] = 1;
    } else {
        for(int i = 0; i < 16; ++i) bytes[int(sizeof(header)) + i] = char(i * 11);
    }
    return bytes;
}

static QByteArray pngBytes()
{
    QImage image(2, 2, QImage::Format_ARGB32);
    image.fill(QColor(30, 90, 150, 200));
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "PNG");
    return bytes;
}

// Exercise real COM failure/short-read semantics without allocating huge data.
class FaultStream : public IStream
{
    ULONG refs = 1;
    IStream* inner;
    int fault;
public:
    FaultStream(IStream* stream, int mode) : inner(stream), fault(mode) {}
    virtual ~FaultStream() { inner->Release(); }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** out) override {
        if(!out) return E_POINTER;
        *out = nullptr;
        if(iid == IID_IUnknown || iid == IID_IStream || iid == IID_ISequentialStream) { *out = this; AddRef(); return S_OK; }
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs; }
    ULONG STDMETHODCALLTYPE Release() override { ULONG n = --refs; if(!n) delete this; return n; }
    HRESULT STDMETHODCALLTYPE Read(void* p, ULONG n, ULONG* read) override {
        if(fault == 2) { if(read) *read = 0; return STG_E_READFAULT; }
        return inner->Read(p, n, read);
    }
    HRESULT STDMETHODCALLTYPE Seek(LARGE_INTEGER n, DWORD origin, ULARGE_INTEGER* pos) override {
        return fault == 1 ? STG_E_INVALIDFUNCTION : inner->Seek(n, origin, pos);
    }
    HRESULT STDMETHODCALLTYPE Stat(STATSTG* stat, DWORD flags) override {
        const HRESULT result = inner->Stat(stat, flags);
        if(result == S_OK && fault == 3) ++stat->cbSize.QuadPart;
        if(result == S_OK && fault == 4) stat->cbSize.QuadPart = 129ULL * 1024 * 1024;
        return result;
    }
    HRESULT STDMETHODCALLTYPE Write(const void* p, ULONG n, ULONG* written) override { return inner->Write(p,n,written); }
    HRESULT STDMETHODCALLTYPE SetSize(ULARGE_INTEGER n) override { return inner->SetSize(n); }
    HRESULT STDMETHODCALLTYPE CopyTo(IStream* p, ULARGE_INTEGER n, ULARGE_INTEGER* r, ULARGE_INTEGER* w) override { return inner->CopyTo(p,n,r,w); }
    HRESULT STDMETHODCALLTYPE Commit(DWORD n) override { return inner->Commit(n); }
    HRESULT STDMETHODCALLTYPE Revert() override { return inner->Revert(); }
    HRESULT STDMETHODCALLTYPE LockRegion(ULARGE_INTEGER a, ULARGE_INTEGER b, DWORD c) override { return inner->LockRegion(a,b,c); }
    HRESULT STDMETHODCALLTYPE UnlockRegion(ULARGE_INTEGER a, ULARGE_INTEGER b, DWORD c) override { return inner->UnlockRegion(a,b,c); }
    HRESULT STDMETHODCALLTYPE Clone(IStream**) override { return E_NOTIMPL; }
};

// Real OLE source, PNG deliberately rejects HGLOBAL. Ownership and delayed
// rendering stay in this process; parent must pump OLE while child runs.
class StreamSource : public IDataObject
{
    ULONG refs = 1;
public:
    virtual ~StreamSource() = default;
    QMap<UINT, QByteArray> bytes;
    DWORD pngMedium = TYMED_ISTREAM;
    int streamFault = 0;
    StreamSource() {
        bytes.insert(RegisterClipboardFormatW(L"PNG"), pngBytes());
        bytes.insert(CF_DIB, rasterDib(false));
        BITMAPV5HEADER header = {};
        const QByteArray dib = rasterDib(false);
        memcpy(&header, dib.constData(), sizeof(BITMAPINFOHEADER));
        header.bV5Size = sizeof(header);
        bytes.insert(CF_DIBV5, QByteArray(reinterpret_cast<const char*>(&header), sizeof(header)) + dib.mid(sizeof(BITMAPINFOHEADER)));
        bytes.insert(RegisterClipboardFormatW(L"CanUploadToCloudClipboard"), QByteArray::fromHex("01000000"));
        bytes.insert(RegisterClipboardFormatW(L"CanIncludeInClipboardHistory"), QByteArray::fromHex("01000000"));
        bytes.insert(RegisterClipboardFormatW(L"ExcludeClipboardContentFromMonitorProcessing"), QByteArray(4, '\0'));
    }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** out) override {
        if(!out) return E_POINTER;
        *out = nullptr;
        if(iid == IID_IUnknown || iid == IID_IDataObject) { *out = static_cast<IDataObject*>(this); AddRef(); return S_OK; }
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs; }
    ULONG STDMETHODCALLTYPE Release() override { const ULONG n = --refs; if(!n) delete this; return n; }
    HRESULT STDMETHODCALLTYPE QueryGetData(FORMATETC* f) override {
        if(!f) return E_POINTER;
        if(f->ptd || f->lindex != -1 || f->dwAspect != DVASPECT_CONTENT) return DV_E_FORMATETC;
        if(!bytes.contains(f->cfFormat)) return DV_E_FORMATETC;
        return (f->tymed & (f->cfFormat == RegisterClipboardFormatW(L"PNG") ? pngMedium : DWORD(TYMED_HGLOBAL))) ? S_OK : DV_E_TYMED;
    }
    HRESULT STDMETHODCALLTYPE GetData(FORMATETC* f, STGMEDIUM* m) override {
        const HRESULT valid = QueryGetData(f);
        if(valid != S_OK) return valid;
        *m = {};
        const QByteArray payload = bytes[f->cfFormat];
        if(f->cfFormat == RegisterClipboardFormatW(L"PNG") && (pngMedium & TYMED_ISTREAM)) {
            m->tymed = TYMED_ISTREAM;
            if(CreateStreamOnHGlobal(nullptr, TRUE, &m->pstm) != S_OK) return E_OUTOFMEMORY;
            ULONG written = 0;
            if(m->pstm->Write(payload.constData(), ULONG(payload.size()), &written) != S_OK || written != ULONG(payload.size())) {
                ReleaseStgMedium(m); *m = {}; return E_FAIL;
            }
            if(streamFault) m->pstm = new FaultStream(m->pstm, streamFault);
            // Return positioned at EOF, forcing guard to seek to the start.
            return S_OK;
        }
        if(pngMedium == TYMED_ISTORAGE && f->cfFormat == RegisterClipboardFormatW(L"PNG")) {
            ILockBytes* lock = nullptr;
            if(CreateILockBytesOnHGlobal(nullptr, TRUE, &lock) != S_OK) return E_FAIL;
            m->tymed = TYMED_ISTORAGE;
            const HRESULT result = StgCreateDocfileOnILockBytes(lock, STGM_CREATE | STGM_READWRITE | STGM_SHARE_EXCLUSIVE, 0, &m->pstg);
            lock->Release(); return result;
        }
        m->tymed = TYMED_HGLOBAL;
        m->hGlobal = GlobalAlloc(GMEM_MOVEABLE, SIZE_T(payload.size()));
        void* data = m->hGlobal ? GlobalLock(m->hGlobal) : nullptr;
        if(!data) { ReleaseStgMedium(m); *m = {}; return E_OUTOFMEMORY; }
        memcpy(data, payload.constData(), size_t(payload.size())); GlobalUnlock(m->hGlobal);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE EnumFormatEtc(DWORD dir, IEnumFORMATETC** out) override {
        if(dir != DATADIR_GET) return E_NOTIMPL;
        QVector<FORMATETC> list;
        for(auto it = bytes.cbegin(); it != bytes.cend(); ++it) {
            FORMATETC f = {}; f.cfFormat = CLIPFORMAT(it.key()); f.dwAspect = DVASPECT_CONTENT; f.lindex = -1;
            f.tymed = it.key() == RegisterClipboardFormatW(L"PNG") ? pngMedium : DWORD(TYMED_HGLOBAL);
            list.append(f);
        }
        // Resolve OS helper; application/test needs no new dependency.
        typedef HRESULT (WINAPI *CreateEnum)(UINT, FORMATETC*, IEnumFORMATETC**);
        static const auto create = reinterpret_cast<CreateEnum>(GetProcAddress(LoadLibraryW(L"shell32.dll"), "SHCreateStdEnumFmtEtc"));
        return create ? create(UINT(list.size()), list.data(), out) : E_NOTIMPL;
    }
    HRESULT STDMETHODCALLTYPE GetDataHere(FORMATETC*, STGMEDIUM*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetCanonicalFormatEtc(FORMATETC*, FORMATETC* out) override { if(out) out->ptd = nullptr; return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE SetData(FORMATETC*, STGMEDIUM*, BOOL) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE DAdvise(FORMATETC*, DWORD, IAdviseSink*, DWORD*) override { return OLE_E_ADVISENOTSUPPORTED; }
    HRESULT STDMETHODCALLTYPE DUnadvise(DWORD) override { return OLE_E_ADVISENOTSUPPORTED; }
    HRESULT STDMETHODCALLTYPE EnumDAdvise(IEnumSTATDATA**) override { return OLE_E_ADVISENOTSUPPORTED; }
};

static bool installStream(int fault = 0, DWORD medium = TYMED_ISTREAM)
{
    auto* source = new StreamSource;
    source->streamFault = fault; source->pngMedium = medium;
    QElapsedTimer timeout;
    timeout.start();
    HRESULT result;
    do {
        result = OleSetClipboard(source);
        if(result == S_OK) break;
        QTest::qWait(10);
    } while(timeout.elapsed() < 1000);
    source->Release();
    return result == S_OK;
}

static QByteArray oleBytes(UINT id, DWORD tymed, HRESULT* status = nullptr)
{
    QByteArray result;
    IDataObject* object = nullptr;
    HRESULT hr = OleGetClipboard(&object);
    if(hr == S_OK && object) {
        FORMATETC f = {}; f.cfFormat = CLIPFORMAT(id); f.dwAspect = DVASPECT_CONTENT; f.lindex = -1; f.tymed = tymed;
        STGMEDIUM m = {}; hr = object->GetData(&f, &m);
        if(hr == S_OK && m.tymed == TYMED_HGLOBAL) {
            const SIZE_T size = GlobalSize(m.hGlobal);
            const void* data = GlobalLock(m.hGlobal);
            if(data && size <= INT_MAX) result = QByteArray(static_cast<const char*>(data), int(size));
            if(data) GlobalUnlock(m.hGlobal);
        } else if(hr == S_OK && m.tymed == TYMED_ISTREAM) {
            LARGE_INTEGER zero = {}; m.pstm->Seek(zero, STREAM_SEEK_SET, nullptr);
            char chunk[4096]; ULONG read = 0;
            do { hr = m.pstm->Read(chunk, sizeof(chunk), &read); result.append(chunk, int(read)); } while(hr == S_OK && read);
            if(hr == S_FALSE) hr = S_OK;
        }
        ReleaseStgMedium(&m); object->Release();
    }
    if(status) *status = hr;
    return result;
}

static bool clipboardInventory()
{
    HWND window = CreateWindowExW(0, L"STATIC", L"", 0, 0, 0, 0, 0,
        HWND_MESSAGE, nullptr, GetModuleHandleW(nullptr), nullptr);
    const bool opened = openForCheck(window);
    if(!opened) { if(window) DestroyWindow(window); return false; }
    UINT id = 0;
    while((id = EnumClipboardFormats(id))) {
        wchar_t name[256] = {};
        GetClipboardFormatNameW(id, name, 256);
        printf("Native format %u %s\n", id, qPrintable(QString::fromWCharArray(name)));
    }
    CloseClipboard();
    DestroyWindow(window);
    IDataObject* object = nullptr;
    if(OleGetClipboard(&object) != S_OK || !object) return false;
    IEnumFORMATETC* enumerator = nullptr;
    const HRESULT result = object->EnumFormatEtc(DATADIR_GET, &enumerator);
    if(result != S_OK || !enumerator) { object->Release(); return false; }
    FORMATETC format = {};
    while(enumerator->Next(1, &format, nullptr) == S_OK) {
        printf("OLE format %u tymed=%lu aspect=%lu index=%ld\n", format.cfFormat,
            static_cast<unsigned long>(format.tymed), static_cast<unsigned long>(format.dwAspect), static_cast<long>(format.lindex));
        if(format.ptd) CoTaskMemFree(format.ptd);
    }
    enumerator->Release(); object->Release(); return true;
}

static bool installRaster(bool indexed, bool bitmapFirst = false)
{
    const QByteArray dib = rasterDib(indexed);
    HGLOBAL block = GlobalAlloc(GMEM_MOVEABLE, SIZE_T(dib.size()));
    void* bytes = block ? GlobalLock(block) : nullptr;
    if(!bytes) { if(block) GlobalFree(block); return false; }
    memcpy(bytes, dib.constData(), size_t(dib.size()));
    GlobalUnlock(block);
    HDC dc = GetDC(nullptr);
    HBITMAP bitmap = bitmapFirst ? CreateDIBitmap(dc,
        reinterpret_cast<const BITMAPINFOHEADER*>(dib.constData()), CBM_INIT,
        dib.constData() + sizeof(BITMAPINFOHEADER),
        reinterpret_cast<const BITMAPINFO*>(dib.constData()), DIB_RGB_COLORS) : nullptr;
    ReleaseDC(nullptr, dc);
    HPALETTE palette = nullptr;
    if(indexed) {
        struct { WORD version; WORD count; PALETTEENTRY entries[2]; } logical =
            {0x300, 2, {{30, 20, 10, 0}, {90, 80, 70, 0}}};
        palette = CreatePalette(reinterpret_cast<const LOGPALETTE*>(&logical));
    }
    HWND window = CreateWindowExW(0, L"STATIC", L"", 0, 0, 0, 0, 0,
        HWND_MESSAGE, nullptr, GetModuleHandleW(nullptr), nullptr);
    HGLOBAL excluded = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, sizeof(DWORD));
    bool ok = window && excluded && (!bitmapFirst || bitmap) && (!indexed || palette) && openForCheck(window);
    if(ok) {
        ok = EmptyClipboard();
        if(ok) {
            ok = SetClipboardData(RegisterClipboardFormatW(L"ExcludeClipboardContentFromMonitorProcessing"), excluded);
            if(ok) excluded = nullptr;
        }
        if(ok && bitmapFirst) {
            ok = SetClipboardData(CF_BITMAP, bitmap);
            if(ok) bitmap = nullptr;
        }
        if(ok) { ok = SetClipboardData(CF_DIB, block); if(ok) block = nullptr; }
        if(ok && palette) { ok = SetClipboardData(CF_PALETTE, palette); if(ok) palette = nullptr; }
        CloseClipboard();
    }
    if(window) DestroyWindow(window);
    if(bitmap) DeleteObject(bitmap);
    if(palette) DeleteObject(palette);
    if(block) GlobalFree(block);
    if(excluded) GlobalFree(excluded);
    return ok;
}

static QByteArray readNativeBytes(UINT format)
{
    QByteArray result;
    HWND window = CreateWindowExW(0, L"STATIC", L"", 0, 0, 0, 0, 0,
        HWND_MESSAGE, nullptr, GetModuleHandleW(nullptr), nullptr);
    if(openForCheck(window)) {
        HANDLE block = GetClipboardData(format);
        const SIZE_T size = block ? GlobalSize(block) : 0;
        const void* bytes = size && size <= INT_MAX ? GlobalLock(block) : nullptr;
        if(bytes) { result = QByteArray(static_cast<const char*>(bytes), int(size)); GlobalUnlock(block); }
        CloseClipboard();
    }
    if(window) DestroyWindow(window);
    return result;
}
#endif

class HiddenWidgets : public QObject
{
    bool eventFilter(QObject* obj, QEvent* event) override
    {
        if(event->type() == QEvent::Polish)
            if(auto* widget = qobject_cast<QWidget*>(obj)) widget->setAttribute(Qt::WA_DontShowOnScreen);
        return false;
    }
};

class VerifiedClipboardTests : public QObject
{
    Q_OBJECT
    bool holdClipboard(QProcess& holder, int ms)
    {
        QTemporaryDir ready;
        if(!ready.isValid()) return false;
        const QString marker = ready.filePath("locked");
        holder.setProcessChannelMode(QProcess::ForwardedChannels);
        holder.setInputChannelMode(QProcess::ForwardedInputChannel);
        holder.start(QCoreApplication::applicationFilePath(), {"--hold", QString::number(ms), marker});
        if(!holder.waitForStarted(3000)) return false;
        QElapsedTimer timeout;
        timeout.start();
        do {
            QTest::qWait(10); // The clipboard's current OLE owner must dispatch native messages.
            if(QFile::exists(marker)) return true;
        } while(timeout.elapsed() < 3000 && holder.state() != QProcess::NotRunning);
        return false;
    }
    bool finishHolder(QProcess& holder)
    {
        return (holder.state() == QProcess::NotRunning || holder.waitForFinished(3000))
            && holder.exitStatus() == QProcess::NormalExit && holder.exitCode() == 0;
    }
private slots:
    void explicitMonitorMarkerIsNotOverwritten()
    {
#ifdef Q_OS_WIN
        QMimeData original;
        original.setText("Synthetic marker preservation");
        const QString markerName = QStringLiteral("ExcludeClipboardContentFromMonitorProcessing");
        const QByteArray marker = QByteArray::fromHex("01000000");
        original.setData(markerName, marker);
        QVERIFY(ei::copyMimeDataToClipboard(original));
        QVERIFY(OleFlushClipboard() == S_OK);
        QCOMPARE(readNativeBytes(RegisterClipboardFormatW(L"ExcludeClipboardContentFromMonitorProcessing")), marker);
        QVERIFY(ei::copyTextToClipboard("Synthetic automatic marker"));
        QVERIFY(OleFlushClipboard() == S_OK);
        QCOMPARE(readNativeBytes(RegisterClipboardFormatW(L"ExcludeClipboardContentFromMonitorProcessing")), QByteArray(4, '\0'));
#endif
    }
    void emptyVerifiedWriteRemainsEmpty()
    {
        QMimeData empty;
        const auto snapshot = ei::ClipboardSnapshot::fromMimeData(empty);
        const auto result = ei::writeClipboard(snapshot);
        QVERIFY2(result.ok, qPrintable(result.error));
        QVERIFY(snapshot.matches(QApplication::clipboard()->mimeData()));
        QVERIFY(QApplication::clipboard()->mimeData()->formats().isEmpty());
    }
    void streamOnlyPngRestoresBothMedia()
    {
#ifdef Q_OS_WIN
        StreamSource source;
        FORMATETC f = {}; f.cfFormat = CLIPFORMAT(RegisterClipboardFormatW(L"PNG"));
        f.dwAspect = DVASPECT_CONTENT; f.lindex = -1; f.tymed = TYMED_HGLOBAL;
        QCOMPARE(source.QueryGetData(&f), HRESULT(DV_E_TYMED));
        QVERIFY(installStream());
        QCOMPARE(oleBytes(f.cfFormat, TYMED_ISTREAM), pngBytes());
        ei::ClipboardRestoreGuard guard;
        QVERIFY2(guard.captured(), qPrintable(guard.error()));
        const auto original = source.bytes;
        QVERIFY(ei::copyTextToClipboard("Temporary replacement"));
        QVERIFY2(guard.restore(), qPrintable(guard.error()));
        for(auto it = original.cbegin(); it != original.cend(); ++it) QCOMPARE(readNativeBytes(it.key()), it.value());
        HRESULT status = E_FAIL;
        QCOMPARE(oleBytes(f.cfFormat, TYMED_HGLOBAL, &status), pngBytes()); QCOMPARE(status, HRESULT(S_OK));
        const QByteArray stream = oleBytes(f.cfFormat, TYMED_ISTREAM, &status);
        QCOMPARE(status, HRESULT(S_OK)); QCOMPARE(stream, pngBytes());
        QVERIFY(!QImage::fromData(stream, "PNG").isNull());
        QVERIFY(IsClipboardFormatAvailable(CF_BITMAP));
#endif
    }
    void storageRefusesBeforeWrites()
    {
#ifdef Q_OS_WIN
        QVERIFY(installStream(false, TYMED_ISTORAGE));
        const DWORD sequence = GetClipboardSequenceNumber();
        ei::ClipboardRestoreGuard guard;
        QVERIFY(!guard.captured()); QVERIFY(!guard.error().isEmpty());
        QCOMPARE(GetClipboardSequenceNumber(), sequence);
#endif
    }
    void unreadableStreamRefusesBeforeWrites_data()
    {
        QTest::addColumn<int>("fault");
        QTest::newRow("not-seekable") << 1;
        QTest::newRow("read-failure") << 2;
        QTest::newRow("truncated") << 3;
        QTest::newRow("over-128MiB") << 4;
    }
    void unreadableStreamRefusesBeforeWrites()
    {
#ifdef Q_OS_WIN
        QFETCH(int, fault);
        QVERIFY(installStream(fault));
        const DWORD sequence = GetClipboardSequenceNumber();
        ei::ClipboardRestoreGuard guard;
        QVERIFY(!guard.captured()); QVERIFY(!guard.error().isEmpty());
        QCOMPARE(GetClipboardSequenceNumber(), sequence);
#endif
    }
    void nativeBitmapBeforeDibRestores()
    {
#ifdef Q_OS_WIN
        QVERIFY(installRaster(false, true));
        const QByteArray original = readNativeBytes(CF_DIB);
        QCOMPARE(original, rasterDib(false));
        QVERIFY(IsClipboardFormatAvailable(CF_BITMAP));
        ei::ClipboardRestoreGuard guard;
        QVERIFY2(guard.captured(), qPrintable(guard.error()));
        QVERIFY(ei::copyTextToClipboard("Temporary replacement"));
        QVERIFY2(guard.restore(), qPrintable(guard.error()));
        QCOMPARE(readNativeBytes(CF_DIB), original);
        QVERIFY(IsClipboardFormatAvailable(CF_BITMAP));
        QVERIFY(openForCheck(nullptr));
        const bool bitmapAvailable = GetClipboardData(CF_BITMAP) != nullptr;
        CloseClipboard();
        QVERIFY(bitmapAvailable);
#endif
    }
    void indexedDibRestoresBitmapAndPalette()
    {
#ifdef Q_OS_WIN
        QVERIFY(installRaster(true));
        QVERIFY(openForCheck(nullptr));
        const bool available = GetClipboardData(CF_BITMAP) && GetClipboardData(CF_PALETTE);
        CloseClipboard();
        QVERIFY(available);
        ei::ClipboardRestoreGuard guard;
        QVERIFY2(guard.captured(), qPrintable(guard.error()));
        QVERIFY(ei::copyTextToClipboard("Temporary replacement"));
        QVERIFY2(guard.restore(), qPrintable(guard.error()));
        QCOMPARE(readNativeBytes(CF_DIB), rasterDib(true));
        QVERIFY(IsClipboardFormatAvailable(CF_BITMAP));
        QVERIFY(IsClipboardFormatAvailable(CF_PALETTE));
        QVERIFY(openForCheck(nullptr));
        HPALETTE palette = HPALETTE(GetClipboardData(CF_PALETTE));
        PALETTEENTRY entries[2] = {};
        const UINT count = palette ? GetPaletteEntries(palette, 0, 2, entries) : 0;
        CloseClipboard();
        QCOMPARE(count, UINT(2));
        QCOMPARE(entries[0].peRed, BYTE(30));
        QCOMPARE(entries[1].peBlue, BYTE(70));
#endif
    }
    void paletteWithoutDibRefusesBeforeWrites()
    {
#ifdef Q_OS_WIN
        struct { WORD version; WORD count; PALETTEENTRY entries[2]; } logical =
            {0x300, 2, {{30, 20, 10, 0}, {90, 80, 70, 0}}};
        HPALETTE palette = CreatePalette(reinterpret_cast<const LOGPALETTE*>(&logical));
        QVERIFY(palette);
        HWND window = CreateWindowExW(0, L"STATIC", L"", 0, 0, 0, 0, 0,
            HWND_MESSAGE, nullptr, GetModuleHandleW(nullptr), nullptr);
        const bool opened = openForCheck(window);
        const bool installed = opened && EmptyClipboard() && SetClipboardData(CF_PALETTE, palette);
        if(opened) CloseClipboard();
        if(window) DestroyWindow(window);
        if(!installed) DeleteObject(palette);
        QVERIFY(installed);
        const DWORD sequence = GetClipboardSequenceNumber();
        ei::ClipboardRestoreGuard guard;
        QVERIFY(!guard.captured());
        QVERIFY(!guard.error().isEmpty());
        QCOMPARE(GetClipboardSequenceNumber(), sequence);
        QVERIFY(IsClipboardFormatAvailable(CF_PALETTE));
#endif
    }
    void metafileWithDibRefusesBeforeWrites()
    {
#ifdef Q_OS_WIN
        QVERIFY(installRaster(false));
        HDC dc = CreateEnhMetaFileW(nullptr, nullptr, nullptr, L"Synthetic\0Metafile\0\0");
        QVERIFY(dc);
        MoveToEx(dc, 0, 0, nullptr); LineTo(dc, 2, 2);
        HENHMETAFILE metafile = CloseEnhMetaFile(dc);
        QVERIFY(metafile);
        HWND window = CreateWindowExW(0, L"STATIC", L"", 0, 0, 0, 0, 0,
            HWND_MESSAGE, nullptr, GetModuleHandleW(nullptr), nullptr);
        const bool opened = openForCheck(window);
        const bool installed = opened && SetClipboardData(CF_ENHMETAFILE, metafile);
        if(opened) CloseClipboard();
        if(window) DestroyWindow(window);
        if(!installed) DeleteEnhMetaFile(metafile);
        QVERIFY(installed);
        const QByteArray dib = readNativeBytes(CF_DIB);
        const DWORD sequence = GetClipboardSequenceNumber();
        ei::ClipboardRestoreGuard guard;
        QVERIFY(!guard.captured());
        QVERIFY(!guard.error().isEmpty());
        QCOMPARE(GetClipboardSequenceNumber(), sequence);
        QCOMPARE(readNativeBytes(CF_DIB), dib);
        QVERIFY(IsClipboardFormatAvailable(CF_ENHMETAFILE));
#endif
    }
    void rapidNativeCopies()
    {
        QWidget anchor;
        for(int i = 0; i < 100; ++i) {
            QToolTip::showText(QPoint(10, 10), "Synthetic tooltip", &anchor);
            QMimeData mime;
            mime.setText(QString("Synthetic %1").arg(i));
            mime.setData("application/x-synthetic", QByteArray::number(i));
            const auto snapshot = ei::ClipboardSnapshot::fromMimeData(mime);
            const auto result = ei::writeClipboard(snapshot);
            QVERIFY2(result.ok, qPrintable(result.error));
            QVERIFY(snapshot.matches(QApplication::clipboard()->mimeData()));
            QToolTip::hideText();
        }
    }
    void temporaryLockKeepsTimersLive()
    {
        QProcess holder;
        QVERIFY(holdClipboard(holder, 250));
        int heartbeat = 0;
        QTimer timer;
        connect(&timer, &QTimer::timeout, [&] { ++heartbeat; });
        timer.start(10);
        QMimeData mime;
        mime.setText("Released lock");
        const auto result = ei::writeClipboard(ei::ClipboardSnapshot::fromMimeData(mime));
        QVERIFY(finishHolder(holder));
        QVERIFY2(result.ok, qPrintable(result.error));
        QVERIFY(heartbeat >= 3);
        QVERIFY(result.elapsedMs >= 100 && result.elapsedMs < 1500);
        QCOMPARE(QApplication::clipboard()->text(), QString("Released lock"));
    }
    void permanentLockIsBoundedAndPreservesData()
    {
        QVERIFY(ei::copyTextToClipboard("Existing synthetic text"));
        QProcess holder;
        QVERIFY(holdClipboard(holder, 700));
        QMimeData mime;
        mime.setText("Must not replace old value");
        const auto result = ei::writeClipboard(ei::ClipboardSnapshot::fromMimeData(mime), 150);
        QVERIFY(!result.ok);
        QVERIFY(!result.error.isEmpty());
        QCOMPARE(result.attempts, 0);
        QVERIFY(result.elapsedMs >= 140 && result.elapsedMs < 450);
        QVERIFY(finishHolder(holder));
        QCOMPARE(QApplication::clipboard()->text(), QString("Existing synthetic text"));
    }
    void nestedWriteCannotReplaceTransaction()
    {
        QProcess holder;
        QVERIFY(holdClipboard(holder, 150));
        ei::ClipboardWriteResult nested;
        bool tried = false;
        QTimer::singleShot(20, [&] {
            QMimeData mime;
            mime.setText("Nested wrong value");
            nested = ei::writeClipboard(ei::ClipboardSnapshot::fromMimeData(mime));
            tried = true;
        });
        QVERIFY(ei::copyTextToClipboard("Outer correct value"));
        QVERIFY(finishHolder(holder));
        QVERIFY(tried && !nested.ok);
        QCOMPARE(nested.attempts, 0);
        QCOMPARE(QApplication::clipboard()->text(), QString("Outer correct value"));
    }
    void exactSelectionAndContextMenu()
    {
        QLineEdit line("abcdef");
        line.setSelection(2, 2);
        QVERIFY(ei::copyLineEditSelection(&line));
        QCOMPARE(QApplication::clipboard()->text(), QString("cd"));
        QVERIFY(!QApplication::clipboard()->mimeData()->hasFormat("application/x-ei-mapper-property-type"));
        QScopedPointer<QMenu> menu(ei::verifiedLineEditContextMenu(&line));
        auto* copy = menu->findChild<QAction*>("verifiedClipboardCopy");
        auto* cut = menu->findChild<QAction*>("verifiedClipboardCut");
        QVERIFY(copy && cut);
        QVERIFY(ei::copyTextToClipboard("Other text"));
        copy->trigger();
        QCOMPARE(QApplication::clipboard()->text(), QString("cd"));
        cut->trigger();
        QCOMPARE(line.text(), QString("abef"));
        QCOMPARE(QApplication::clipboard()->text(), QString("cd"));
        line.undo();
        QCOMPARE(line.text(), QString("abcdef"));
    }
    void failedCutNeverDeletesSelection()
    {
        QLineEdit line("abcdef");
        line.setSelection(2, 2);
        QProcess holder;
        QVERIFY(holdClipboard(holder, 1400));
        QVERIFY(!ei::copyLineEditSelection(&line, true));
        QCOMPARE(line.text(), QString("abcdef"));
        QCOMPARE(line.selectedText(), QString("cd"));
        QVERIFY(finishHolder(holder));
    }
    void changedSelectionIsNotCut()
    {
        QLineEdit line("abcdef");
        line.setSelection(2, 2);
        QProcess holder;
        QVERIFY(holdClipboard(holder, 150));
        QTimer::singleShot(20, &line, [&] { line.setSelection(0, 2); });
        QVERIFY(ei::copyLineEditSelection(&line, true));
        QVERIFY(finishHolder(holder));
        QCOMPARE(line.text(), QString("abcdef"));
        QCOMPARE(line.selectedText(), QString("ab"));
        QCOMPARE(QApplication::clipboard()->text(), QString("cd"));
    }
    void destroyedEditorIsNotAccessedAfterWaiting()
    {
        QPointer<QLineEdit> line = new QLineEdit("abcdef");
        line->setSelection(2, 2);
        QProcess holder;
        QVERIFY(holdClipboard(holder, 150));
        QTimer::singleShot(20, [&line] { delete line.data(); });
        QVERIFY(ei::copyLineEditSelection(line, true));
        QVERIFY(finishHolder(holder));
        QVERIFY(line.isNull());
        QCOMPARE(QApplication::clipboard()->text(), QString("cd"));
    }
    void restoreAllFormatsAndFlushNativeData()
    {
        QMimeData original;
        original.setText(QString::fromUtf8("Русский текст"));
        original.setHtml("<b>Synthetic HTML</b>");
        original.setUrls({QUrl("https://example.invalid/synthetic")});
        QImage image(4, 4, QImage::Format_ARGB32);
        image.fill(QColor(30, 90, 150, 200));
        original.setImageData(image);
        original.setData("application/x-synthetic-binary", QByteArray("\0\1\2\xff", 4));
        original.setData("Synthetic native binary", QByteArray("\0\1\2\xff", 4));
        auto snapshot = ei::ClipboardSnapshot::fromMimeData(original);
        QVERIFY(ei::writeClipboard(snapshot).ok);
        ei::ClipboardRestoreGuard guard;
        QVERIFY2(guard.captured(), qPrintable(guard.error()));
        QVERIFY(ei::copyTextToClipboard("Temporary test data"));
        QVERIFY2(guard.restore(), qPrintable(guard.error()));
        // Read each typed value once per attempt. Qt's synthesized image MIME
        // bytes and storage format may differ, but every pixel, alpha, URL and
        // original payload must survive. The guard also checks all native bytes
        // and format priority before reporting a successful restore.
        QElapsedTimer timeout;
        timeout.start();
        bool valuesRestored = false;
        do {
            const QMimeData* mime = QApplication::clipboard()->mimeData();
            const QImage actualImage = qvariant_cast<QImage>(mime->imageData());
            valuesRestored = mime->text() == original.text()
                && mime->html().contains(original.html()) && mime->urls() == original.urls()
                && actualImage.convertToFormat(QImage::Format_ARGB32) == image
                && mime->data("application/x-synthetic-binary") == original.data("application/x-synthetic-binary")
                && mime->data("Synthetic native binary") == original.data("Synthetic native binary");
            if(valuesRestored) break;
            QTest::qWait(25);
        } while(timeout.elapsed() < 2000);
        QVERIFY2(valuesRestored, "Original text/HTML/URLs/image pixels/binary data did not survive native restoration.");
        QVERIFY(guard.restore()); // Idempotent; no second overwrite.
    }
    void restoreLiteralNativeAlias()
    {
#ifdef Q_OS_WIN
        const QString alias = QStringLiteral("application/x-qt-windows-mime;value=\"Synthetic aliased binary\"");
        const QByteArray binary("\0\1\2\xff", 4);
        QMimeData original;
        original.setData(alias, binary);
        QVERIFY(ei::copyMimeDataToClipboard(original));
        ei::ClipboardRestoreGuard guard;
        QVERIFY2(guard.captured(), qPrintable(guard.error()));
        QVERIFY(ei::copyTextToClipboard("Temporary replacement"));
        QVERIFY2(guard.restore(), qPrintable(guard.error()));
        HWND window = CreateWindowExW(0, L"STATIC", L"", 0, 0, 0, 0, 0,
                                      HWND_MESSAGE, nullptr, GetModuleHandleW(nullptr), nullptr);
        const bool opened = openForCheck(window);
        bool preserved = false;
        if(opened) {
            HANDLE handle = GetClipboardData(RegisterClipboardFormatW(reinterpret_cast<LPCWSTR>(alias.utf16())));
            const void* bytes = handle ? GlobalLock(handle) : nullptr;
            preserved = bytes && GlobalSize(handle) >= SIZE_T(binary.size())
                && memcmp(bytes, binary.constData(), size_t(binary.size())) == 0;
            if(bytes) GlobalUnlock(handle);
            CloseClipboard();
        }
        if(window) DestroyWindow(window);
        QVERIFY(opened && preserved);
#endif
    }
    void restoreEmptyClipboard()
    {
#ifdef Q_OS_WIN
        HWND window = CreateWindowExW(0, L"STATIC", L"", 0, 0, 0, 0, 0,
                                      HWND_MESSAGE, nullptr, GetModuleHandleW(nullptr), nullptr);
        const bool opened = openForCheck(window);
        const bool emptied = opened && EmptyClipboard();
        if(opened) CloseClipboard();
        if(window) DestroyWindow(window);
        QVERIFY(emptied);
        ei::ClipboardRestoreGuard guard;
        QVERIFY2(guard.captured(), qPrintable(guard.error()));
        QVERIFY(ei::copyTextToClipboard("Temporary replacement"));
        QVERIFY2(guard.restore(), qPrintable(guard.error()));
        QCOMPARE(CountClipboardFormats(), 0);
#endif
    }
};

int main(int argc, char** argv)
{
#ifdef Q_OS_WIN
    if(argc == 4 && QByteArray(argv[1]) == "--hold") {
        HWND window = CreateWindowExW(0, L"STATIC", L"", 0, 0, 0, 0, 0,
                                      HWND_MESSAGE, nullptr, GetModuleHandleW(nullptr), nullptr);
        if(!window) return 3;
        QElapsedTimer timeout;
        timeout.start();
        while(!OpenClipboard(window)) {
            if(timeout.elapsed() >= 2000) return 2;
            QThread::msleep(10);
        }
        QFile marker(QString::fromLocal8Bit(argv[3]));
        if(!marker.open(QIODevice::WriteOnly)) { CloseClipboard(); DestroyWindow(window); return 4; }
        marker.write("locked"); marker.close();
        QThread::msleep(QByteArray(argv[2]).toUInt());
        CloseClipboard();
        DestroyWindow(window);
        return 0;
    }
#endif
    QApplication app(argc, argv);
    HiddenWidgets hidden;
    app.installEventFilter(&hidden);
#ifdef Q_OS_WIN
    if(argc == 2 && QByteArray(argv[1]) == "--inventory") return clipboardInventory() ? 0 : 2;
#endif
    ei::ClipboardRestoreGuard clipboard;
    if(!clipboard.captured()) { fprintf(stderr, "%s\n", qPrintable(clipboard.error())); return 2; }
    if(argc == 3 && QByteArray(argv[1]) == "--guard-stream-child") {
#ifdef Q_OS_WIN
        if(!installStream()) return 2;
        if(!clipboardInventory()) return 2;
#endif
    }
    if(argc == 3 && QByteArray(argv[1]) == "--guard-image-child") {
#ifdef Q_OS_WIN
        // The owner's contents are captured before installing any fixture.
        if(!installRaster(false, true)) return 2;
#endif
    }
    if(argc == 3 && (QByteArray(argv[1]) == "--guard-child" || QByteArray(argv[1]) == "--guard-image-child" || QByteArray(argv[1]) == "--guard-stream-child")) {
        // Historical UI checks lack verified restoration. Keep their original
        // executable unchanged, while an independent parent preserves native
        // clipboard data for a safe baseline comparison.
        QProcess child;
        QTemporaryDir childOutput;
        if(!childOutput.isValid()) return 2;
        child.setStandardOutputFile(childOutput.filePath("out"));
        child.setStandardErrorFile(childOutput.filePath("err"));
        child.setInputChannelMode(QProcess::ForwardedInputChannel);
#ifdef Q_OS_WIN
        const QByteArray imageBefore = readNativeBytes(CF_DIB);
        const QByteArray imageV5Before = readNativeBytes(CF_DIBV5);
        StreamSource expectedStream;
#endif
        child.start(QString::fromLocal8Bit(argv[2]), {"--ui-widget-check"});
        if(!child.waitForStarted(3000)) return 2;
        QElapsedTimer timeout;
        timeout.start();
        while(child.state() != QProcess::NotRunning && timeout.elapsed() < 60000)
            QTest::qWait(25);
        const bool finished = child.state() == QProcess::NotRunning;
        if(!finished) { child.kill(); child.waitForFinished(3000); }
        QFile outputFile(childOutput.filePath("out")), errorFile(childOutput.filePath("err"));
        outputFile.open(QIODevice::ReadOnly); errorFile.open(QIODevice::ReadOnly);
        const auto output = outputFile.readAll();
        const auto errors = errorFile.readAll();
        fwrite(output.constData(), 1, size_t(output.size()), stdout);
        fwrite(errors.constData(), 1, size_t(errors.size()), stderr);
#ifdef Q_OS_WIN
        const bool imageRestored = QByteArray(argv[1]) != "--guard-image-child"
            || (readNativeBytes(CF_DIB) == imageBefore && readNativeBytes(CF_DIBV5) == imageV5Before
                && IsClipboardFormatAvailable(CF_BITMAP));
        printf("%s: child preserved image HGLOBAL bytes and CF_BITMAP availability\n", imageRestored ? "PASS" : "FAIL");
        bool streamRestored = true;
        if(QByteArray(argv[1]) == "--guard-stream-child") {
            for(auto it = expectedStream.bytes.cbegin(); it != expectedStream.bytes.cend(); ++it)
                streamRestored = streamRestored && readNativeBytes(it.key()) == it.value();
            const UINT png = RegisterClipboardFormatW(L"PNG");
            HRESULT globalStatus = E_FAIL, streamStatus = E_FAIL;
            const QByteArray global = oleBytes(png, TYMED_HGLOBAL, &globalStatus);
            const QByteArray stream = oleBytes(png, TYMED_ISTREAM, &streamStatus);
            streamRestored = streamRestored && globalStatus == S_OK && streamStatus == S_OK
                && global == pngBytes() && stream == global && !QImage::fromData(stream, "PNG").isNull();
            printf("%s: child preserved all stream fixture bytes, PNG HGLOBAL/ISTREAM and decoding\n", streamRestored ? "PASS" : "FAIL");
            if(!clipboardInventory()) streamRestored = false;
        }
#else
        const bool imageRestored = true;
        const bool streamRestored = true;
#endif
        const bool restored = clipboard.restore();
        printf("%s: baseline parent restored original native clipboard\n", restored ? "PASS" : "FAIL");
        if(!finished || !restored || !imageRestored || !streamRestored || child.exitStatus() != QProcess::NormalExit) return 2;
        return child.exitCode();
    }
    VerifiedClipboardTests tests;
    const int result = QTest::qExec(&tests, argc, argv);
    const bool restored = clipboard.restore();
    printf("%s: original clipboard restored and native data flushed\n", restored ? "PASS" : "FAIL");
    if(!restored) fprintf(stderr, "%s\n", qPrintable(clipboard.error()));
    return restored ? result : 1;
}
#include "verified_clipboard_tests.moc"
