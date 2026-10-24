#include <QtWidgets>
int main(int argc,char** argv)
{
    QApplication app(argc,argv);QTranslator translation;
    if(argc!=2||!translation.load(QString::fromLocal8Bit(argv[1]))){qCritical()<<"FAIL Qt Russian translation load";return 1;}
    app.installTranslator(&translation);QDialogButtonBox buttons(QDialogButtonBox::Open|QDialogButtonBox::Cancel);
    const auto cancel=buttons.button(QDialogButtonBox::Cancel)->text();const auto open=buttons.button(QDialogButtonBox::Open)->text();
    QTextStream output(stdout);output.setCodec("UTF-8");output<<"Cancel="<<cancel<<" Open="<<open<<"\n";
    if(!cancel.contains(QString::fromUtf8("Отмена"))||!open.contains(QString::fromUtf8("Открыть"))){qCritical()<<"FAIL standard Qt dialog remains untranslated";return 1;}
    QTextStream(stdout)<<"PASS real hidden Qt buttons use Russian\n";return 0;
}
