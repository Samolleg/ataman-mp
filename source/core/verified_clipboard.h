#ifndef EI_VERIFIED_CLIPBOARD_H
#define EI_VERIFIED_CLIPBOARD_H

#include <QByteArray>
#include <QMap>
#include <QVariant>
#include <QUrl>
#include <memory>

class QLineEdit;
class QMenu;
class QMimeData;
class QWidget;

namespace ei {

struct NativeClipboardState;

// Explicit, process-lifetime test isolation. Construct BEFORE QApplication,
// install AFTER QApplication but BEFORE any windows/clipboard use. The QPA
// adapter covers Qt's built-in editors too; ordinary launches never install it.
class TestClipboardSession
{
public:
    explicit TestClipboardSession(bool enabled);
    ~TestClipboardSession();
    bool prepared() const;
    bool install();
    bool verify() const;
    TestClipboardSession(const TestClipboardSession&) = delete;
    TestClipboardSession& operator=(const TestClipboardSession&) = delete;
private:
    bool m_enabled;
};
bool testClipboardEnabled();
bool testClipboardRequested(int argc, char** argv);
const QMimeData* clipboardMimeData();
QString clipboardText();
// Negative-test probe: bypasses the adapter; native hooks MUST deny the access.
void probeSystemClipboardForTest();

// Value copies only: QClipboard may delete a transferred QMimeData inside
// setMimeData() when the platform write fails.
struct ClipboardSnapshot
{
    QMap<QString, QByteArray> formats;
    QVariant image;
    QList<QUrl> urls;
    static ClipboardSnapshot fromMimeData(const QMimeData& mime);
    QMimeData* createMimeData() const;
    bool matches(const QMimeData* mime, QString* mismatch = nullptr) const;
};

struct ClipboardWriteResult
{
    bool ok = false;
    int attempts = 0;
    int elapsedMs = 0;
    QString error;
};

ClipboardWriteResult writeClipboard(const ClipboardSnapshot& data, int timeoutMs = 1000);
bool copyMimeDataToClipboard(const QMimeData& data, QWidget* feedback = nullptr);
bool copyTextToClipboard(const QString& text, QWidget* feedback = nullptr);
bool copyLineEditSelection(QLineEdit* editor, bool cut = false);
QMenu* verifiedLineEditContextMenu(QLineEdit* editor);

// Used by native checks, never by ordinary Copy. An unreadable clipboard must
// abort the check before its first synthetic write. Native data is materialized
// before the check process exits, without Qt MIME alias conversion.
// Windows accepts complete seekable IStream byte payloads (128 MiB per format,
// 256 MiB total). Storage/metafiles fail closed. OLE transport metadata is not
// replayed; restored byte formats support native HGLOBAL and OLE ISTREAM.
// Missing raster GDI sources may be read from Windows' synthesized DIB/DIBV5
// under a read-only native open, with the same completeness and size limits.
// A monitor-exclusion marker prevents restored content becoming a history item.
class ClipboardRestoreGuard
{
public:
    ClipboardRestoreGuard();
    ~ClipboardRestoreGuard();
    bool captured() const { return m_captured; }
    const QString& error() const { return m_error; }
    bool restore();
    ClipboardRestoreGuard(const ClipboardRestoreGuard&) = delete;
    ClipboardRestoreGuard& operator=(const ClipboardRestoreGuard&) = delete;
private:
    ClipboardSnapshot m_saved;
    std::unique_ptr<NativeClipboardState> m_native;
    bool m_captured = false;
    bool m_restored = false;
    QString m_error;
};

}
#endif
