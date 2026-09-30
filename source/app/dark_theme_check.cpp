#include "dark_theme_check.h"
#include "ui_theme.h"
#include "tree_view.h"
#include "worldobj.h"
#include "script_editor.h"
#include "script_highlighter.h"
#include "settings.h"
#include "landscape_tools.h"
#include "tile_form.h"
#include "resource_setup_dialog.h"
#include "travel_panel.h"
#include "map_table_editor_dialog.h"
#include "multiline_edit_form.h"
#include "message_log_panel.h"
#include "bodypartedit_form.h"
#include "unitstat_form.h"
#include "randomize_form.h"
#include "select_window.h"
#include "round_mob_form.h"
#include "maps_browser.h"
#include "resource_browser.h"
#include "createobjectform.h"
#include "camera_telephoto_controls.h"
#include "collection_editor_dialog.h"
#include "terrain_atlas_review_dialog.h"
#include "minimap_export_dialog.h"
#include "mob_script_window.h"
#include "scenario_panel.h"
#include "range_dialog.h"
#include "behavior_collection_widget.h"
#include "trap_collection_widget.h"
#include "model_gallery.h"
#include "database_editor_dialog.h"
#include "profile_database.h"
#include "item_spell_constructor_dialog.h"
#include "mob_parameters.h"
#include "mob_diff_dialog.h"
#include <QApplication>
#include <QStyleFactory>
#include <QTextStream>
#include <QTextBlock>
#include <QTextLayout>
#include <QLabel>
#include <QPushButton>
#include <QLineEdit>
#include <QSpinBox>
#include <QComboBox>
#include <QMenu>
#include <QMessageBox>
#include <QInputDialog>
#include <QFileDialog>
#include <QColorDialog>
#include <QFontDialog>
#include <QProgressDialog>
#include <QVBoxLayout>
#include <QTemporaryDir>
#include <QDir>
#include <cmath>

namespace {
double luminance(QColor color) {
    auto channel=[](double c) { return c<=0.04045?c/12.92:std::pow((c+0.055)/1.055,2.4); };
    return 0.2126*channel(color.redF())+0.7152*channel(color.greenF())+0.0722*channel(color.blueF());
}
}
double darkThemeTextContrast(QColor a,QColor b) {
    auto x=luminance(a), y=luminance(b); return (qMax(x,y)+0.05)/(qMin(x,y)+0.05);
}
int runDarkThemeCheck()
{
    // CSettings persists on destruction. Keep that write in synthetic data.
    QTemporaryDir workingDirectory;
    struct RestoreDirectory {
        QString path = QDir::currentPath();
        ~RestoreDirectory() { QDir::setCurrent(path); }
    } restoreDirectory;
    if(!workingDirectory.isValid() || !QDir::setCurrent(workingDirectory.path())) return 2;
    const auto originalPalette=qApp->palette(); const auto originalSheet=qApp->styleSheet();
    ei::installDarkUiPalette();
    QTextStream out(stdout); int failures=0;
    auto check=[&](bool ok,QString name) { out<<(ok?"PASS: ":"FAIL: ")<<name<<"\n";out.flush();if(!ok)++failures; };
    const auto palette=qApp->palette();
    for(auto group : {QPalette::Active,QPalette::Inactive,QPalette::Disabled})
        for(auto roles : {qMakePair(QPalette::WindowText,QPalette::Window),qMakePair(QPalette::Text,QPalette::Base),
                         qMakePair(QPalette::ButtonText,QPalette::Button),qMakePair(QPalette::HighlightedText,QPalette::Highlight),
                         qMakePair(QPalette::ToolTipText,QPalette::ToolTipBase)})
            check(darkThemeTextContrast(palette.color(group,roles.first),palette.color(group,roles.second))>=4.5,
                  QString("palette group=%1 text=%2 background=%3").arg(group).arg(roles.first).arg(roles.second));
    check(darkThemeTextContrast(palette.color(QPalette::PlaceholderText),palette.color(QPalette::Base))>=4.5,"search placeholders readable");
    for(auto tone : {ei::UiTextTone::Error,ei::UiTextTone::Warning,ei::UiTextTone::Success,
                     ei::UiTextTone::Link,ei::UiTextTone::Incomplete,ei::UiTextTone::Muted})
        check(darkThemeTextContrast(ei::uiTextColor(tone),palette.color(QPalette::Base))>=4.5 &&
              darkThemeTextContrast(ei::uiTextColor(tone),palette.color(QPalette::Window))>=4.5,
              QString("diagnostic tone %1 readable on both dark backgrounds").arg(int(tone)));
    CTreeView tree; tree.setColumnCount(3); tree.setHeaderLabels({"Objects","ID","Count"});
    CWorldObj object;
    tree.beginRefresh();
    auto* group=tree.acquireRow("group",nullptr,nullptr);
    auto* item=tree.acquireRow("item",group,&object);
    group->setText(0,"ZombieFemaleF2"); group->setText(2,"3");
    item->setText(0,"Human Hadagan Peasants F1-90"); item->setText(1,"90");
    tree.finishRefresh(); group->setExpanded(true);
    for(int column=0;column<3;++column)
        check(!group->data(column,Qt::ForegroundRole).isValid() && !item->data(column,Qt::ForegroundRole).isValid(),
              QString("hierarchy column %1 inherits normal and selected palette").arg(column));
    item->setForeground(0,QColor(Qt::black));
    tree.beginRefresh(); tree.acquireRow("group",nullptr,nullptr); tree.acquireRow("item",group,&object); tree.finishRefresh();
    check(!item->data(0,Qt::ForegroundRole).isValid(),"retained hierarchy row clears stale explicit foreground");
    CScriptEditor editor; CScriptHighlighter highlighter(editor.document());
    editor.setPlainText("if\n\"quoted\"\nGetObject()\n// comment\nobject foo\n42"); highlighter.rehighlight();
    int formats=0;
    for(auto block=editor.document()->begin();block.isValid();block=block.next())
        for(const auto& range:block.layout()->formats()) {
            ++formats;
            const auto color=range.format.foreground().color();
            check(darkThemeTextContrast(color,palette.color(QPalette::Base))>=4.5 && darkThemeTextContrast(color,palette.color(QPalette::AlternateBase))>=4.5,
                  "MOB script token readable: "+block.text());
        }
    check(formats>=6,"all six script token classes actually highlighted");
    for(const auto& extra:editor.extraSelections())
        check(darkThemeTextContrast(palette.color(QPalette::Text),extra.format.background().color())>=4.5,"current script line preserves text contrast");
    auto checkWindow=[&](QWidget* window,QString name) {
        window->setAttribute(Qt::WA_DontShowOnScreen); window->ensurePolished();
        auto widgets=window->findChildren<QWidget*>(); widgets.prepend(window);
        bool readable=true; int textWidgets=0;
        for(auto* widget:widgets) {
            widget->ensurePolished(); QPalette::ColorRole fg,bg;
            if(qobject_cast<QLabel*>(widget)) { fg=QPalette::WindowText;bg=widget->backgroundRole(); }
            else if(qobject_cast<QLineEdit*>(widget)||qobject_cast<QPlainTextEdit*>(widget)||qobject_cast<QAbstractSpinBox*>(widget)||qobject_cast<QAbstractItemView*>(widget)) { fg=QPalette::Text; bg=QPalette::Base; }
            else if(qobject_cast<QAbstractButton*>(widget)||qobject_cast<QComboBox*>(widget)) { fg=QPalette::ButtonText;bg=QPalette::Button; }
            else continue;
            ++textWidgets; const auto p=widget->palette();
            if(darkThemeTextContrast(p.color(fg),p.color(bg))<4.5) {
                readable=false; out<<"LOW CONTRAST: "<<name<<" "<<widget->metaObject()->className()<<" "<<widget->objectName()<<" "<<p.color(fg).name()<<" / "<<p.color(bg).name()<<"\n";
            }
            if(auto* view=qobject_cast<QAbstractItemView*>(widget)) {
                const auto* model=view->model();
                if(!model) continue;
                std::function<void(const QModelIndex&)> inspectRows;
                inspectRows=[&](const QModelIndex& parent) {
                    for(int row=0;row<model->rowCount(parent);++row) {
                        for(int column=0;column<model->columnCount(parent);++column) {
                            const auto index=model->index(row,column,parent);
                            const auto foreground=index.data(Qt::ForegroundRole);
                            const auto background=index.data(Qt::BackgroundRole);
                            const QColor text=foreground.isValid() ? qvariant_cast<QBrush>(foreground).color() : p.color(QPalette::Text);
                            const QColor base=background.isValid() ? qvariant_cast<QBrush>(background).color() : p.color(QPalette::Base);
                            if(darkThemeTextContrast(text,base)<4.5) {
                                readable=false; out<<"LOW CONTRAST: "<<name<<" model row="<<row<<" column="<<column<<" "<<text.name()<<" / "<<base.name()<<"\n";
                            }
                        }
                        inspectRows(model->index(row,0,parent));
                    }
                };
                inspectRows({});
            }
        }
        check(readable && textWidgets>0,name+QString(" (%1 text widgets)").arg(textWidgets)); delete window;
    };
    checkWindow(new CSettings,"Settings");
    checkWindow(new CLandscapeTools,"Landscape tools");
    checkWindow(new CTileForm,"Tile palette");
    checkWindow(new CResourceSetupDialog,"Resource setup");
    checkWindow(new CTravelPanel,"Appearances and transitions");
    checkWindow(new CMapTableEditorDialog,"map.txt editor");
    checkWindow(new CMultiLineEditForm,"Multiline property editor");
    checkWindow(new CMessageLogPanel,"Message log");
    checkWindow(new CBodyPartEditForm,"Model parts");
    checkWindow(new CUnitStatForm,"Unit parameters");
    checkWindow(new CRandomizeForm,"Randomization");
    checkWindow(new CSelectForm,"Selection filter");
    checkWindow(new CRoundMobForm,"Round MOB");
    checkWindow(new CMapsBrowser,"Maps and collections");
    checkWindow(new CResourceBrowser,"Resource browser");
    checkWindow(new CCreateObjectForm,"Create object");
    checkWindow(new CCameraTelephotoControls,"Camera controls");
    checkWindow(new CCollectionEditorDialog(SEnduranceCollection(),{}),"Collection editor");
    checkWindow(new CTerrainAtlasReviewDialog(STerrainAtlasProfile(),{}),"Terrain atlas review");
    checkWindow(new CMinimapExportDialog(SMinimapSceneCaptureResult(),{},"","","",SMinimapGradeSettings(),false),"Minimap export");
    checkWindow(new CMobScriptWindow(nullptr,nullptr),"MOB script window");
    checkWindow(new CScenarioPanel(nullptr),"Map scenario");
    checkWindow(new CRangeDialog,"MOB ranges");
    CMob syntheticMob;
    QList<QString> factionNames;
    QVector<QVector<uint>> relations(32,QVector<uint>(32));
    for(int row=0;row<32;++row) {
        factionNames.append(QString("Faction %1").arg(row));
        for(int column=0;column<32;++column) relations[row][column]=uint((row+column)%3);
    }
    relations[31][31]=9; // Unknown stored value must inherit a readable dark background.
    syntheticMob.setDiplomacyNames(factionNames); syntheticMob.setDiplomacyField(relations);
    checkWindow(new CMobParameters(nullptr,&syntheticMob,nullptr),"MOB parameters and diplomacy");
    checkWindow(new CMobDiffDialog(QFileInfo(workingDirectory.path()+"/missing-old.mob"),
                                 QFileInfo(workingDirectory.path()+"/missing-new.mob")),"MOB comparison diagnostics");
    checkWindow(new CBehaviorCollectionWidget(nullptr,0),"Patrol points");
    checkWindow(new CTrapCollectionWidget(nullptr),"Trap points");
    QTemporaryDir catalogDir;
    ei::TechnicalModelCatalog catalog; catalog.load(catalogDir.path()+"/model-catalog.json");
    ei::TechnicalTextureCatalog scan;
    checkWindow(new CModelGallery({"stbuho45"},&catalog,"synthetic",{},&scan,{}),"Model gallery");
    auto snapshot=QSharedPointer<ei::SProfileDatabaseSnapshot>::create();
    ei::SRaceModelDatabaseRecord race; race.name="testRace"; race.model="testModel";
    ei::SMonsterDatabaseRecord monster; monster.name="testMonster"; monster.baseRace=race.name;
    snapshot->raceModels.insert(race.name,race); snapshot->monsters.insert(monster.name,monster);
    snapshot->profileId="dark-theme-synthetic";
    auto database=ei::DatabaseDocument::fromSnapshot(snapshot);
    checkWindow(new ei::DatabaseEditorDialog(database),"Database editor");
    checkWindow(new ei::ItemSpellConstructorDialog(database,{}),"Item and spell constructor");
    checkWindow(new QMessageBox(QMessageBox::Warning,"Warning","Readable warning",QMessageBox::Ok),"Message boxes");
    auto* input=new QInputDialog; input->setLabelText("Input"); input->setInputMode(QInputDialog::TextInput);
    checkWindow(input,"Input dialogs");
    auto* files=new QFileDialog; files->setOption(QFileDialog::DontUseNativeDialog); checkWindow(files,"Qt file dialogs");
    auto* colors=new QColorDialog; colors->setOption(QColorDialog::DontUseNativeDialog); checkWindow(colors,"Qt color dialogs");
    auto* fonts=new QFontDialog; fonts->setOption(QFontDialog::DontUseNativeDialog); checkWindow(fonts,"Qt font dialogs");
    checkWindow(new QProgressDialog("Working","Cancel",0,100),"Progress dialogs");
    if(qEnvironmentVariableIsSet("EI_THEME_SCREENSHOT")) {
        QWidget panel; panel.setAttribute(Qt::WA_DontShowOnScreen); auto* layout=new QVBoxLayout(&panel);
        layout->addWidget(new QLabel("Dark theme: hierarchy, selection and MOB script"));
        layout->addWidget(&tree); layout->addWidget(&editor); panel.resize(760,560); panel.show();
        tree.setColumnWidth(0,550); tree.setColumnWidth(1,70); item->setSelected(true);
        QApplication::processEvents(); panel.grab().save(qEnvironmentVariable("EI_THEME_SCREENSHOT"));
        tree.setParent(nullptr); editor.setParent(nullptr);
    }
    qApp->setPalette(originalPalette); qApp->setStyleSheet(originalSheet);
    out<<"dark-theme-check: "<<failures<<" failures\n";return failures?1:0;
}
