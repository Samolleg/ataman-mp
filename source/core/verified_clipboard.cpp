#include "verified_clipboard.h"

#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QDebug>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QImage>
#include <QKeyEvent>
#include <QLineEdit>
#include <QLibrary>
#include <QMenu>
#include <QMimeData>
#include <QPointer>
#include <QScopedValueRollback>
#include <QThread>
#include <QTimer>
#include <QToolTip>
#include <QVector>
#include <climits>
#include <cstring>
#ifdef Q_OS_WIN
#include <qt_windows.h>
#include <ole2.h>
#endif

namespace ei {
#ifdef Q_OS_WIN
namespace {
// Keep the guard self-contained: the application does not link gdi32 directly.
// Failure to resolve an OS API is a safe capture/allocation failure.
struct PaletteApi
{
    QLibrary library{QStringLiteral("gdi32")};
    decltype(&GetPaletteEntries) entries = reinterpret_cast<decltype(&GetPaletteEntries)>(library.resolve("GetPaletteEntries"));
    decltype(&CreatePalette) create = reinterpret_cast<decltype(&CreatePalette)>(library.resolve("CreatePalette"));
    decltype(&DeleteObject) destroy = reinterpret_cast<decltype(&DeleteObject)>(library.resolve("DeleteObject"));
    bool available() const { return entries && create && destroy; }
};
const PaletteApi& paletteApi()
{
    static const PaletteApi api;
    return api;
}

// Bounds apply before allocation and while reading (Stat is not trusted).
constexpr quint64 maxClipboardBytes = 128 * 1024 * 1024;
constexpr quint64 maxSnapshotBytes = 256 * 1024 * 1024;
int guardedWrites = 0; // GUI thread; ordinary Copy outside a check is unchanged.
bool openNativeClipboard();
bool readStream(IStream* stream, QByteArray& bytes)
{
    if(!stream) return false;
    LARGE_INTEGER zero = {};
    ULARGE_INTEGER position = {};
    if(stream->Seek(zero, STREAM_SEEK_CUR, &position) != S_OK) return false;
    struct RestorePosition {
        IStream* stream;
        ULARGE_INTEGER position;
        ~RestorePosition() {
            LARGE_INTEGER offset; offset.QuadPart = position.QuadPart;
            stream->Seek(offset, STREAM_SEEK_SET, nullptr);
        }
    } restore{stream, position};
    if(stream->Seek(zero, STREAM_SEEK_SET, nullptr) != S_OK) return false;
    STATSTG stat = {};
    if(stream->Stat(&stat, STATFLAG_NONAME) != S_OK || stat.cbSize.QuadPart > maxClipboardBytes)
        return false;
    bytes.clear();
    char chunk[64 * 1024];
    for(;;) {
        ULONG count = 0;
        const HRESULT result = stream->Read(chunk, sizeof(chunk), &count);
        if((result != S_OK && result != S_FALSE) || count > sizeof(chunk)
            || quint64(bytes.size()) + count > maxClipboardBytes) return false;
        bytes.append(chunk, int(count));
        if(result == S_FALSE || !count) break;
    }
    // A short/failed/truncated stream must never become a restorable snapshot.
    return quint64(bytes.size()) == stat.cbSize.QuadPart;
}

UINT monitorExclusion()
{
    return RegisterClipboardFormatW(L"ExcludeClipboardContentFromMonitorProcessing");
}
}
#endif
struct NativeClipboardState
{
#ifdef Q_OS_WIN
    QMap<UINT, QByteArray> formats;
    QVector<UINT> order; // Native format priority matters, e.g. DIBV5 before DIB.
    QVector<UINT> synthesized; // Availability, never opaque GDI handle bytes.
    QByteArray paletteEntries; // Some desktops do not synthesize CF_PALETTE.

    static bool canSynthesize(UINT format, const QMap<UINT, QByteArray>& values)
    {
        // A complete raster source permits BITMAP synthesis, never metafile
        // synthesis. Accept PALETTE only with a color table, and preserve its
        // actual entries as well: synthesis is unavailable on some desktops.
        for(UINT source : {UINT(CF_DIBV5), UINT(CF_DIB)}) {
            const auto it = values.constFind(source);
            if(it == values.cend() || it->size() < int(sizeof(BITMAPINFOHEADER))) continue;
            BITMAPINFOHEADER header;
            memcpy(&header, it->constData(), sizeof(header));
            if((header.biSize != sizeof(BITMAPINFOHEADER) && header.biSize != sizeof(BITMAPV4HEADER)
                && header.biSize != sizeof(BITMAPV5HEADER)) || header.biPlanes != 1
                || header.biWidth <= 0 || header.biHeight == 0 || header.biHeight == LONG_MIN
                || (header.biCompression != BI_RGB && header.biCompression != BI_BITFIELDS)
                || (header.biBitCount != 1 && header.biBitCount != 4 && header.biBitCount != 8
                    && header.biBitCount != 16 && header.biBitCount != 24 && header.biBitCount != 32)) continue;
            const quint64 colors = header.biClrUsed ? header.biClrUsed
                : (header.biBitCount <= 8 ? quint64(1) << header.biBitCount : 0);
            if(header.biBitCount <= 8 && colors > (quint64(1) << header.biBitCount)) continue;
            const quint64 masks = header.biSize == sizeof(BITMAPINFOHEADER)
                && header.biCompression == BI_BITFIELDS ? 3 * sizeof(DWORD) : 0;
            const quint64 stride = ((quint64(header.biWidth) * header.biBitCount + 31) / 32) * 4;
            const quint64 height = header.biHeight < 0 ? -qint64(header.biHeight) : header.biHeight;
            if(quint64(header.biSize) + masks + colors * sizeof(RGBQUAD) + stride * height
                > quint64(it->size())) continue;
            if(format == CF_BITMAP || (format == CF_PALETTE && colors)) return true;
        }
        return false;
    }

    bool capture(QString& error)
    {
        IDataObject* object = nullptr;
        if(OleGetClipboard(&object) != S_OK || !object) return false;
        IEnumFORMATETC* enumerator = nullptr;
        const HRESULT enumerated = object->EnumFormatEtc(DATADIR_GET, &enumerator);
        if(enumerated != S_OK || !enumerator) { object->Release(); return false; }
        QMap<UINT, QByteArray> values;
        QVector<UINT> priority;
        QVector<UINT> deferred;
        QByteArray palette;
        quint64 totalBytes = 0;
        FORMATETC format;
        bool ok = true;
        HRESULT next;
        while((next = enumerator->Next(1, &format, nullptr)) == S_OK) {
            // Decide GDI dependencies after capturing every HGLOBAL source:
            // OLE may enumerate BITMAP before DIB. Preserve source priority.
            const bool rasterGdi = (format.cfFormat == CF_BITMAP || format.cfFormat == CF_PALETTE)
                && (format.tymed & TYMED_GDI);
            if(format.ptd || format.lindex != -1 || format.dwAspect != DVASPECT_CONTENT
                || (format.tymed & TYMED_ISTORAGE)
                || format.cfFormat == CF_ENHMETAFILE || format.cfFormat == CF_METAFILEPICT
                || (!(format.tymed & (TYMED_HGLOBAL | TYMED_ISTREAM)) && !rasterGdi)) {
                if(format.ptd) CoTaskMemFree(format.ptd);
                error = QStringLiteral("Native clipboard format %1 is not preservable; check aborted before writes.")
                    .arg(format.cfFormat);
                ok = false;
                break;
            }
            if(!(format.tymed & (TYMED_HGLOBAL | TYMED_ISTREAM))) {
                if(format.cfFormat == CF_PALETTE) {
                    if(!paletteApi().available()) { ok = false; break; }
                    format.tymed = TYMED_GDI;
                    STGMEDIUM medium = {};
                    if(object->GetData(&format, &medium) != S_OK) { ok = false; break; }
                    HPALETTE handle = medium.tymed == TYMED_GDI ? HPALETTE(medium.hBitmap) : nullptr;
                    const UINT count = handle ? paletteApi().entries(handle, 0, 0, nullptr) : 0;
                    if(!count || count > 65535) ok = false;
                    else {
                        palette.resize(int(count * sizeof(PALETTEENTRY)));
                        ok = paletteApi().entries(handle, 0, count,
                            reinterpret_cast<PALETTEENTRY*>(palette.data())) == count;
                    }
                    ReleaseStgMedium(&medium);
                    if(!ok) break;
                }
                if(!deferred.contains(format.cfFormat)) deferred.append(format.cfFormat);
                continue;
            }
            const DWORD offered = format.tymed;
            format.tymed = offered & TYMED_HGLOBAL ? TYMED_HGLOBAL : TYMED_ISTREAM;
            STGMEDIUM medium = {};
            HRESULT received = object->GetData(&format, &medium);
            if(received != S_OK && (offered & TYMED_ISTREAM) && format.tymed == TYMED_HGLOBAL) {
                ReleaseStgMedium(&medium);
                medium = {};
                format.tymed = TYMED_ISTREAM;
                received = object->GetData(&format, &medium);
            }
            QByteArray payload;
            if(received != S_OK) ok = false;
            else if(medium.tymed == TYMED_ISTREAM && format.tymed == TYMED_ISTREAM)
                ok = readStream(medium.pstm, payload);
            else if(medium.tymed == TYMED_HGLOBAL && format.tymed == TYMED_HGLOBAL) {
                const SIZE_T size = GlobalSize(medium.hGlobal);
                const void* bytes = size && size <= maxClipboardBytes ? GlobalLock(medium.hGlobal) : nullptr;
                ok = bytes != nullptr;
                if(bytes) {
                    payload = QByteArray(static_cast<const char*>(bytes), int(size));
                    GlobalUnlock(medium.hGlobal);
                }
            } else ok = false;
            ReleaseStgMedium(&medium);
            if(ok && values.contains(format.cfFormat)) ok = values[format.cfFormat] == payload;
            else if(ok) {
                totalBytes += quint64(payload.size());
                ok = totalBytes <= maxSnapshotBytes;
                if(ok) { priority.append(format.cfFormat); values.insert(format.cfFormat, payload); }
            }
            if(!ok) error = QStringLiteral("Native clipboard format %1 cannot be read completely within clipboard limits; check aborted before writes.").arg(format.cfFormat);
            if(!ok) break;
        }
        if(next != S_FALSE && next != S_OK) ok = false;
        enumerator->Release();
        object->Release();
        bool needsNativeSource = false;
        if(ok) for(UINT format : deferred)
            if(!canSynthesize(format, values)) needsNativeSource = true;
        if(needsNativeSource) {
            // OleSetClipboard exposes the object's formats through OLE, but
            // Windows may synthesize DIB/DIBV5 only through the native API.
            // Read both missing byte sources under one read-only open, keeping
            // their native relative priority (not a fixed DIB/V5 ordering).
            if(!openNativeClipboard()) return false; // Constructor retries contention.
            QVector<UINT> sources;
            UINT native = 0;
            while((native = EnumClipboardFormats(native)) != 0)
                if(native == CF_DIB || native == CF_DIBV5) sources.append(native);
            for(UINT source : {UINT(CF_DIBV5), UINT(CF_DIB)})
                if(!sources.contains(source) && IsClipboardFormatAvailable(source)) sources.append(source);
            for(UINT source : sources) {
                if(values.contains(source)) continue;
                HANDLE handle = IsClipboardFormatAvailable(source) ? GetClipboardData(source) : nullptr;
                const SIZE_T size = handle ? GlobalSize(handle) : 0;
                const void* bytes = size && size <= maxClipboardBytes
                    && totalBytes + size <= maxSnapshotBytes ? GlobalLock(handle) : nullptr;
                if(!bytes) { ok = false; break; }
                const QByteArray payload(static_cast<const char*>(bytes), int(size));
                GlobalUnlock(handle);
                QMap<UINT, QByteArray> candidate;
                candidate.insert(source, payload);
                if(!canSynthesize(CF_BITMAP, candidate)) { ok = false; break; }
                totalBytes += size;
                values.insert(source, payload);
                priority.append(source);
            }
            CloseClipboard();
            if(!ok) {
                error = QStringLiteral("Native DIB synthesis source cannot be read completely within clipboard limits; check aborted before writes.");
                return false;
            }
        }
        if(ok) for(UINT format : deferred) {
            if(!canSynthesize(format, values)) {
                error = QStringLiteral("Native clipboard format %1 has no preservable synthesis source; check aborted before writes.")
                    .arg(format);
                ok = false;
                break;
            }
        }
        if(ok) {
            // OLE does not always enumerate Windows' implicit BITMAP format.
            for(UINT format : {UINT(CF_BITMAP)})
                if(!deferred.contains(format) && IsClipboardFormatAvailable(format)
                    && canSynthesize(format, values)) deferred.append(format);
            formats = values;
            order = priority;
            synthesized = deferred;
            paletteEntries = palette;
        }
        return ok;
    }

    // Caller holds OpenClipboard: compare actual native IDs and bytes, including
    // binary browser formats that Qt cannot read under their literal aliases.
    bool matches() const
    {
        if(formats.isEmpty()) return CountClipboardFormats() == 0;
        int priority = 0;
        UINT format = 0;
        while((format = EnumClipboardFormats(format)) != 0) {
            if(priority < order.size() && format == order[priority]) ++priority;
            else if(formats.contains(format)) return false;
        }
        if(priority != order.size()) return false;
        for(auto it = formats.cbegin(); it != formats.cend(); ++it) {
            HANDLE handle = GetClipboardData(it.key());
            if(!handle || GlobalSize(handle) != SIZE_T(it.value().size())) return false;
            if(it.value().isEmpty()) continue;
            const void* bytes = GlobalLock(handle);
            if(!bytes) return false;
            const bool same = memcmp(bytes, it.value().constData(), size_t(it.value().size())) == 0;
            GlobalUnlock(handle);
            if(!same) return false;
        }
        for(UINT format : synthesized)
            if(!IsClipboardFormatAvailable(format) || !GetClipboardData(format)) return false;
        if(!paletteEntries.isEmpty()) {
            HPALETTE palette = HPALETTE(GetClipboardData(CF_PALETTE));
            const UINT count = UINT(paletteEntries.size() / sizeof(PALETTEENTRY));
            QByteArray actual(paletteEntries.size(), '\0');
            if(!paletteApi().available() || !palette || paletteApi().entries(palette, 0, 0, nullptr) != count
                || paletteApi().entries(palette, 0, count, reinterpret_cast<PALETTEENTRY*>(actual.data())) != count
                || actual != paletteEntries) return false;
        }
        return true;
    }

    // Allocate everything before emptying the clipboard. On success Windows
    // owns each materialized block, so process exit does not lose delayed data.
    bool write() const
    {
        QMap<UINT, HGLOBAL> blocks;
        bool ok = true;
        HPALETTE palette = nullptr;
        if(!paletteEntries.isEmpty()) {
            if(!paletteApi().available()) return false;
            const WORD count = WORD(paletteEntries.size() / sizeof(PALETTEENTRY));
            QByteArray logical(int(sizeof(WORD) * 2) + paletteEntries.size(), '\0');
            LOGPALETTE* data = reinterpret_cast<LOGPALETTE*>(logical.data());
            data->palVersion = 0x300;
            data->palNumEntries = count;
            memcpy(data->palPalEntry, paletteEntries.constData(), size_t(paletteEntries.size()));
            palette = paletteApi().create(data);
            if(!palette) return false;
        }
        for(auto it = formats.cbegin(); it != formats.cend(); ++it) {
            HGLOBAL block = GlobalAlloc(GMEM_MOVEABLE, SIZE_T(it.value().size()));
            if(block && it.value().isEmpty()) { blocks.insert(it.key(), block); continue; }
            void* bytes = block ? GlobalLock(block) : nullptr;
            if(!bytes) { if(block) GlobalFree(block); ok = false; break; }
            memcpy(bytes, it.value().constData(), size_t(it.value().size()));
            GlobalUnlock(block);
            blocks.insert(it.key(), block);
        }
        // OLE's DataObject/Ole Private Data are transport metadata, excluded by
        // EnumFormatEtc, not user payloads to replay as stale process pointers.
        // Persist user bytes as native HGLOBAL: Windows' OLE wrapper exposes
        // registered byte formats through HGLOBAL AND ISTREAM after owner exit.
        // Keep existing cloud/history marker bytes; add only the documented
        // monitor exclusion to prevent re-inserting restored content in history.
        const UINT exclusion = monitorExclusion();
        HGLOBAL excluded = nullptr;
        if(ok && !formats.isEmpty() && !formats.contains(exclusion)) {
            excluded = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, sizeof(DWORD));
            ok = excluded != nullptr;
        }
        if(ok) ok = EmptyClipboard();
        if(ok && excluded) {
            ok = SetClipboardData(exclusion, excluded);
            if(ok) excluded = nullptr;
        }
        if(ok) for(UINT format : order) {
            if(!SetClipboardData(format, blocks[format])) { ok = false; break; }
            blocks[format] = nullptr; // Ownership transferred to Windows.
        }
        if(ok && palette) {
            ok = SetClipboardData(CF_PALETTE, palette);
            if(ok) palette = nullptr;
        }
        if(palette) paletteApi().destroy(palette);
        if(excluded) GlobalFree(excluded);
        for(HGLOBAL block : blocks) if(block) GlobalFree(block);
        return ok; // Windows finishes synthesis when CloseClipboard is called.
    }
#endif
};
namespace {
bool writing = false; // GUI thread only; a nested request must not replace this write.

bool openNativeClipboard()
{
#ifdef Q_OS_WIN
    // A distinct hidden HWND makes this probe independent of a NULL-HWND
    // opener. Never share another application's clipboard window identity.
    struct ProbeWindow {
        HWND handle = CreateWindowExW(0, L"STATIC", L"", 0, 0, 0, 0, 0,
                                      HWND_MESSAGE, nullptr, GetModuleHandleW(nullptr), nullptr);
        ~ProbeWindow() { if(handle) DestroyWindow(handle); }
    };
    static ProbeWindow window;
    return window.handle && OpenClipboard(window.handle);
#endif
    return true;
}

bool nativeAvailable()
{
    if(!openNativeClipboard()) return false;
#ifdef Q_OS_WIN
    CloseClipboard();
#endif
    return true;
}

void serviceClipboardEvents(int waitMs)
{
    // Do not dispatch another user's Copy/Cut/Paste or a socket callback in
    // the middle of a transaction. Timers/native OLE messages remain live.
    const auto flags = QEventLoop::ExcludeUserInputEvents | QEventLoop::ExcludeSocketNotifiers;
    if(waitMs <= 0) {
        QCoreApplication::processEvents(flags);
        return;
    }
    QEventLoop loop;
    QTimer::singleShot(waitMs, &loop, &QEventLoop::quit);
    loop.exec(flags);
}

void reportFailure(QWidget* feedback, const QString& error)
{
    qWarning().noquote() << error; // Never log clipboard contents.
    QApplication::beep();
    if(feedback)
        QToolTip::showText(feedback->mapToGlobal(feedback->rect().center()), error, feedback);
}

QString actionLabel(QString text)
{
    return text.section('\t', 0, 0).remove('&');
}

}

ClipboardSnapshot ClipboardSnapshot::fromMimeData(const QMimeData& mime)
{
    ClipboardSnapshot result;
    for(const QString& format : mime.formats())
        result.formats.insert(format, mime.data(format));
    if(mime.hasImage()) result.image = mime.imageData();
    if(mime.hasUrls()) result.urls = mime.urls();
    return result;
}

QMimeData* ClipboardSnapshot::createMimeData() const
{
    QMimeData* mime = new QMimeData;
    for(auto it = formats.cbegin(); it != formats.cend(); ++it)
        mime->setData(it.key(), it.value());
    if(image.isValid()) mime->setImageData(image);
    if(formats.contains(QStringLiteral("text/uri-list"))) mime->setUrls(urls);
    return mime;
}

bool ClipboardSnapshot::matches(const QMimeData* mime, QString* mismatch) const
{
    if(!mime) {
        if(mismatch) *mismatch = QStringLiteral("native MIME unavailable");
        return formats.isEmpty() && !image.isValid();
    }
    if(formats.isEmpty() && !image.isValid()) return mime->formats().isEmpty();
    for(auto it = formats.cbegin(); it != formats.cend(); ++it) {
        if(it.key() == QStringLiteral("text/uri-list")) {
            // Qt serializes native URL lists with platform line endings.
            // Preserve the exact URL values using its typed accessor.
            if(!mime->hasUrls() || mime->urls() != urls) {
                if(mismatch) *mismatch = QStringLiteral("URL values unavailable or changed");
                return false;
            }
        } else {
            if(mime->hasFormat(it.key()) && mime->data(it.key()) == it.value()) continue;
            if(mismatch) *mismatch = QStringLiteral("MIME format unavailable or changed: %1").arg(it.key());
            return false;
        }
    }
    if(image.isValid() && (!mime->hasImage()
        || qvariant_cast<QImage>(mime->imageData()) != qvariant_cast<QImage>(image))) {
        if(mismatch) *mismatch = QStringLiteral("Image pixels unavailable or changed");
        return false;
    }
    return true;
}

ClipboardWriteResult writeClipboard(const ClipboardSnapshot& data, int timeoutMs)
{
    ClipboardWriteResult result;
    if(!qApp || QThread::currentThread() != qApp->thread() || writing) {
        result.error = QStringLiteral("Clipboard write is already in progress or is outside the GUI thread.");
        return result;
    }
    QScopedValueRollback<bool> active(writing, true);
    QElapsedTimer timer;
    timer.start();
    QClipboard* clipboard = QApplication::clipboard();
    serviceClipboardEvents(0);
    do {
        // Avoid Qt's blocking native retries while another owner has the
        // clipboard open. A race after this preflight is handled by readback.
        if(nativeAvailable()) {
            ++result.attempts;
            QMimeData* mime = data.createMimeData();
#ifdef Q_OS_WIN
            // EmptyClipboard has no content to monitor. Keep an explicitly
            // empty verified write empty, including its strict MIME readback.
            const QString exclusionName = QStringLiteral("ExcludeClipboardContentFromMonitorProcessing");
            // For writes Qt registers the literal MIME name. Its Windows MIME
            // alias is a read representation and would create a different ID.
            if(guardedWrites && (!data.formats.isEmpty() || data.image.isValid())
                && !mime->hasFormat(exclusionName))
                mime->setData(exclusionName, QByteArray(4, '\0'));
#endif
            clipboard->setMimeData(mime);
            // The transferred pointer may already have been deleted. Read
            // only the current clipboard and compare with our value snapshot.
            if(clipboard->ownsClipboard() && data.matches(clipboard->mimeData())) {
                result.ok = true;
                break;
            }
        }
        const int remaining = qMax(0, timeoutMs - int(timer.elapsed()));
        if(!remaining) break;
        serviceClipboardEvents(qMin(25, remaining));
    } while(timer.elapsed() < timeoutMs);
    result.elapsedMs = int(timer.elapsed());
    if(!result.ok)
        result.error = QCoreApplication::translate("VerifiedClipboard",
            "Could not copy: the Windows clipboard is busy. Try copying again.");
    return result;
}

bool copyMimeDataToClipboard(const QMimeData& data, QWidget* feedback)
{
    const ClipboardSnapshot snapshot = ClipboardSnapshot::fromMimeData(data);
    const QPointer<QWidget> target(feedback);
    const ClipboardWriteResult result = writeClipboard(snapshot);
    if(!result.ok) reportFailure(target, result.error);
    return result.ok;
}

bool copyTextToClipboard(const QString& text, QWidget* feedback)
{
    QMimeData mime;
    mime.setText(text);
    return copyMimeDataToClipboard(mime, feedback);
}

bool copyLineEditSelection(QLineEdit* editor, bool cut)
{
    if(!editor || !editor->hasSelectedText() || editor->echoMode() != QLineEdit::Normal
        || (cut && editor->isReadOnly())) return false;
    const QString text = editor->text();
    const QString selected = editor->selectedText();
    const int start = editor->selectionStart();
    const QPointer<QLineEdit> target(editor);
    if(!copyTextToClipboard(selected, editor)) return false;
    // Events needed by OLE may have rebuilt or edited the field. Never cut a
    // different selection, nor delete text when the clipboard write failed.
    if(cut && target && !target->isReadOnly() && target->text() == text
        && target->selectionStart() == start && target->selectedText() == selected)
        target->insert(QString());
    return true;
}

QMenu* verifiedLineEditContextMenu(QLineEdit* editor)
{
    QMenu* menu = editor->createStandardContextMenu();
    const QString copy = actionLabel(QCoreApplication::translate("QLineEdit", "&Copy"));
    const QString cut = actionLabel(QCoreApplication::translate("QLineEdit", "Cu&t"));
    const QString paste = actionLabel(QCoreApplication::translate("QLineEdit", "&Paste"));
    for(QAction* action : menu->actions()) {
        const QString label = actionLabel(action->text());
        if(label != copy && label != cut && label != paste) continue;
        QObject::disconnect(action, nullptr, nullptr, nullptr);
        if(label == paste) {
            // Use the editor's usual compatibility/preview handler also from
            // the menu, rather than bypassing it with QLineEdit::paste().
            action->setObjectName(QStringLiteral("verifiedClipboardPaste"));
            QObject::connect(action, &QAction::triggered, editor, [editor] {
                QKeyEvent event(QEvent::KeyPress, Qt::Key_V, Qt::ControlModifier);
                QCoreApplication::sendEvent(editor, &event);
            });
        } else {
            const bool cutting = label == cut;
            action->setObjectName(cutting ? QStringLiteral("verifiedClipboardCut")
                                          : QStringLiteral("verifiedClipboardCopy"));
            QObject::connect(action, &QAction::triggered, editor, [editor, cutting] {
                copyLineEditSelection(editor, cutting);
            });
        }
    }
    return menu;
}

ClipboardRestoreGuard::ClipboardRestoreGuard()
{
#ifdef Q_OS_WIN
    m_native.reset(new NativeClipboardState);
#endif
    QElapsedTimer timer;
    timer.start();
    do {
        serviceClipboardEvents(0);
        if(nativeAvailable()) {
#ifdef Q_OS_WIN
            // Capture the native, persisted representation. Qt's local HTML
            // and URL values are encoded by OLE (fragment markers/line ends).
            // Capturing a local value then comparing with its encoded form
            // after exit would incorrectly report a restoration failure.
            if(QApplication::clipboard()->ownsClipboard()) {
                if(OleFlushClipboard() != S_OK) {
                    serviceClipboardEvents(25);
                    continue;
                }
                serviceClipboardEvents(0);
            }
            const DWORD sequence = GetClipboardSequenceNumber();
            const bool empty = CountClipboardFormats() == 0;
            if(empty) { m_native->formats.clear(); m_native->order.clear(); }
            const bool saved = empty || m_native->capture(m_error);
            if(!m_error.isEmpty()) return;
            if(saved && openNativeClipboard()) {
                m_captured = sequence == GetClipboardSequenceNumber() && m_native->matches();
                CloseClipboard();
                if(m_captured) { ++guardedWrites; return; }
            }
#else
            const QMimeData* mime = QApplication::clipboard()->mimeData();
            if(mime) {
                m_saved = ClipboardSnapshot::fromMimeData(*mime);
                m_captured = m_saved.matches(QApplication::clipboard()->mimeData());
                if(m_captured) return;
            }
#endif
        }
        serviceClipboardEvents(25);
    } while(timer.elapsed() < 1000);
    m_error = QStringLiteral("Native clipboard capture failed; no synthetic write performed.");
}

bool ClipboardRestoreGuard::restore()
{
    if(!m_captured) { m_error = QStringLiteral("Native clipboard capture failed."); return false; }
    if(m_restored) return true;
#ifdef Q_OS_WIN
    QElapsedTimer timer;
    timer.start();
    bool restored = false;
    do {
        serviceClipboardEvents(0);
        if(openNativeClipboard()) {
            const bool written = m_native->write();
            CloseClipboard();
            // Synthesized GDI formats may not exist until the writer closes.
            // Reopen for readback; successful SetClipboardData is not a PASS.
            if(written && openNativeClipboard()) {
                restored = m_native->matches();
                CloseClipboard();
            }
            if(restored) break;
        }
        serviceClipboardEvents(25);
    } while(timer.elapsed() < 2000);
    if(!restored) {
        m_error = QStringLiteral("Clipboard native restore/write/readback failed.");
        return false;
    }
    serviceClipboardEvents(0); // Release Qt's old owner after native replacement.
#else
    if(!writeClipboard(m_saved, 2000).ok) {
        m_error = QStringLiteral("Clipboard restore write/readback failed.");
        return false;
    }
#endif
    m_restored = true;
    m_error.clear();
    return true;
}

ClipboardRestoreGuard::~ClipboardRestoreGuard()
{
    if(m_captured && !m_restored && !restore())
        qWarning("Clipboard restoration failed; native check is not a PASS.");
#ifdef Q_OS_WIN
    if(m_captured) --guardedWrites;
#endif
}
}
