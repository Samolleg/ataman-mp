#include <QtTest>
#include <QApplication>
#include <QClipboard>
#include <QElapsedTimer>
#include <QImage>
#include <QLineEdit>
#include <QMenu>
#include <QMimeData>
#include <QProcess>
#include <QPointer>
#include <QTimer>
#include <QToolTip>
#include <QUrl>
#include "verified_clipboard.h"
#ifdef Q_OS_WIN
#include <qt_windows.h>
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
#ifdef Q_OS_WIN
    bool openForCheck(HWND window)
    {
        QElapsedTimer timeout;
        timeout.start();
        do {
            if(window && OpenClipboard(window)) return true;
            QTest::qWait(10);
        } while(timeout.elapsed() < 1000);
        return false;
    }
#endif
    bool holdClipboard(QProcess& holder, int ms)
    {
        holder.start(QCoreApplication::applicationFilePath(), {"--hold", QString::number(ms)});
        if(!holder.waitForStarted(3000)) return false;
        QElapsedTimer timeout;
        timeout.start();
        QByteArray output;
        do {
            QTest::qWait(10); // The clipboard's current OLE owner must dispatch native messages.
            output += holder.readAllStandardOutput();
            if(output.contains("locked")) return true;
        } while(timeout.elapsed() < 3000 && holder.state() != QProcess::NotRunning);
        return false;
    }
    bool finishHolder(QProcess& holder)
    {
        return (holder.state() == QProcess::NotRunning || holder.waitForFinished(3000))
            && holder.exitStatus() == QProcess::NormalExit && holder.exitCode() == 0;
    }
private slots:
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
    if(argc == 3 && QByteArray(argv[1]) == "--hold") {
        HWND window = CreateWindowExW(0, L"STATIC", L"", 0, 0, 0, 0, 0,
                                      HWND_MESSAGE, nullptr, GetModuleHandleW(nullptr), nullptr);
        if(!window) return 3;
        QElapsedTimer timeout;
        timeout.start();
        while(!OpenClipboard(window)) {
            if(timeout.elapsed() >= 2000) return 2;
            QThread::msleep(10);
        }
        printf("locked\n");
        fflush(stdout);
        QThread::msleep(QByteArray(argv[2]).toUInt());
        CloseClipboard();
        DestroyWindow(window);
        return 0;
    }
#endif
    QApplication app(argc, argv);
    HiddenWidgets hidden;
    app.installEventFilter(&hidden);
    ei::ClipboardRestoreGuard clipboard;
    if(!clipboard.captured()) { fprintf(stderr, "%s\n", qPrintable(clipboard.error())); return 2; }
    if(argc == 3 && QByteArray(argv[1]) == "--guard-child") {
        // Historical UI checks lack verified restoration. Keep their original
        // executable unchanged, while an independent parent preserves native
        // clipboard data for a safe baseline comparison.
        QProcess child;
        child.start(QString::fromLocal8Bit(argv[2]), {"--ui-widget-check"});
        if(!child.waitForStarted(3000)) return 2;
        QElapsedTimer timeout;
        timeout.start();
        while(child.state() != QProcess::NotRunning && timeout.elapsed() < 60000)
            QTest::qWait(25);
        const bool finished = child.state() == QProcess::NotRunning;
        if(!finished) { child.kill(); child.waitForFinished(3000); }
        const auto output = child.readAllStandardOutput();
        const auto errors = child.readAllStandardError();
        fwrite(output.constData(), 1, size_t(output.size()), stdout);
        fwrite(errors.constData(), 1, size_t(errors.size()), stderr);
        const bool restored = clipboard.restore();
        printf("%s: baseline parent restored original native clipboard\n", restored ? "PASS" : "FAIL");
        if(!finished || !restored || child.exitStatus() != QProcess::NormalExit) return 2;
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
