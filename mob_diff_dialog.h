#ifndef MOB_DIFF_DIALOG_H
#define MOB_DIFF_DIALOG_H

#include <QByteArray>
#include <QDialog>
#include <QFileInfo>

class QLineEdit;
class QLabel;
class QTableWidget;

class CMobDiffDialog : public QDialog
{
public:
    CMobDiffDialog(const QFileInfo& baseline, const QFileInfo& candidate,
                   QWidget* parent = nullptr);

private:
    void applyFilter(const QString& text);
    void exportReport(bool json);

    QLineEdit* m_filter = nullptr;
    QLabel* m_summary = nullptr;
    QTableWidget* m_table = nullptr;
    QByteArray m_jsonReport;
    QByteArray m_textReport;
};

#endif // MOB_DIFF_DIALOG_H
