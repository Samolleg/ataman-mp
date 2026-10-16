#ifndef LEVER_APPROACH_DIALOG_H
#define LEVER_APPROACH_DIALOG_H
#include <QDialog>
#include <QPointer>
#include <QSharedPointer>
#include <QMap>
#include <QVariant>
#include <QVector2D>
#include <QQuaternion>
class CView;
class CNode;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
namespace ei {
class DatabaseDocument;
class LeverApproachDialog : public QDialog {
    Q_OBJECT
public:
    LeverApproachDialog(const QSharedPointer<DatabaseDocument>& document, CView* view, QWidget* parent);
    ~LeverApproachDialog() override;
    bool initialize(QString* error);
signals:
    void transactionCommitted(const QString& rowKey);
protected:
    bool eventFilter(QObject*, QEvent*) override;
    void done(int result) override;
private:
    bool contextValid() const;
    void preview();
    void place(const QPoint& position);
    void apply();
    QSharedPointer<DatabaseDocument> m_document;
    QPointer<CView> m_view;
    QPointer<CNode> m_node;
    QString m_rowKey, m_model, m_profile;
    QMap<QString,QVariant> m_original;
    QVector3D m_origin, m_position, m_complexion;
    QQuaternion m_rotation, m_approachRotation;
    float m_scale{1};
    QVector2D m_points[2];
    QComboBox *m_count, *m_active;
    QDoubleSpinBox *m_reach, *m_coordinates[4];
    QLabel* m_status;
    bool m_dragging{false};
    bool m_ready{false};
    bool m_newRecord{false};
};
}
#endif
