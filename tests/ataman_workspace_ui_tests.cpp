#include <QtTest>
#include <QSignalSpy>
#include "ui/flat_tree_widget.h"
#include "ui/graphite_caption.h"

class AtamanWorkspaceUiTests : public QObject
{
    Q_OBJECT
private slots:
    void flatRowsRetainInteraction()
    {
        CFlatTreeWidget tree;
        tree.setAttribute(Qt::WA_DontShowOnScreen);
        tree.setColumnCount(2);
        tree.setHeaderLabels({"Objects", "Count"});
        tree.resize(330, 230);
        tree.setColumnWidth(0, 220);
        auto* root = new QTreeWidgetItem(&tree, {"Map", "2"});
        auto* category = new QTreeWidgetItem(root, {"Objects", "1"});
        auto* leaf = new QTreeWidgetItem(category, {"Test object", ""});
        leaf->setFlags(leaf->flags() | Qt::ItemIsUserCheckable);
        leaf->setCheckState(0, Qt::Unchecked);
        root->setExpanded(true);
        category->setExpanded(true);
        tree.show();
        QTest::qWait(30);
        QCOMPARE(tree.visualItemRect(root).left(), tree.visualItemRect(category).left());
        QCOMPARE(tree.visualItemRect(root).left(), tree.visualItemRect(leaf).left());
        int clicked = 0;
        connect(&tree, &QTreeWidget::itemClicked, &tree, [&clicked]() { ++clicked; });
        const QPoint arrow(tree.disclosureWidth()/2, tree.visualItemRect(category).center().y());
        QTest::mouseClick(tree.viewport(), Qt::LeftButton, Qt::NoModifier, arrow);
        QVERIFY(!category->isExpanded());
        QCOMPARE(clicked, 0);
        QTest::mouseClick(tree.viewport(), Qt::LeftButton, Qt::NoModifier, arrow);
        QVERIFY(category->isExpanded());
        QTest::mouseClick(tree.viewport(), Qt::LeftButton, Qt::NoModifier,
                          QPoint(110, tree.visualItemRect(leaf).center().y()));
        QCOMPARE(tree.currentItem(), leaf);
        QCOMPARE(clicked, 1);
        QTest::keyClick(&tree, Qt::Key_Space);
        QCOMPARE(leaf->checkState(0), Qt::Checked);
        QTest::mouseClick(tree.viewport(), Qt::LeftButton, Qt::NoModifier,
            QPoint(tree.disclosureWidth()+8, tree.visualItemRect(leaf).center().y()));
        QCOMPARE(leaf->checkState(0), Qt::Unchecked);
        tree.setCurrentItem(category);
        QTest::keyClick(&tree, Qt::Key_Left);
        QVERIFY(!category->isExpanded());
        QTest::keyClick(&tree, Qt::Key_Right);
        QVERIFY(category->isExpanded());
        leaf->setHidden(true);
        QVERIFY(tree.visualItemRect(leaf).isEmpty());
        leaf->setHidden(false);
        QVERIFY(!tree.visualItemRect(leaf).isEmpty());
        QVERIFY(tree.grab().save(QCoreApplication::applicationDirPath()+"/flat-tree-check.png"));
    }

    void nativeCaptionColor()
    {
#ifdef Q_OS_WIN
        if(QGuiApplication::platformName() != "windows") QSKIP("Requires native Windows platform");
        QWidget window;
        const WId id = window.winId();
        QVERIFY(applyGraphiteCaption(id));
        // Caption/text color attributes are documented setters, not getters.
        // Check native application succeeds again after handle recreation.
        window.setWindowFlags(window.windowFlags() | Qt::WindowStaysOnTopHint);
        QVERIFY(applyGraphiteCaption(window.winId()));
#endif
    }
};

QTEST_MAIN(AtamanWorkspaceUiTests)
#include "ataman_workspace_ui_tests.moc"
