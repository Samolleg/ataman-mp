#ifndef WORLD_MAP_LAYOUT_COMMAND_H
#define WORLD_MAP_LAYOUT_COMMAND_H
#include <QUndoCommand>
#include <functional>

// A world-layout operation owns JSON coordinates, never MPR/MOB documents.
class CWorldMapLayoutCommand : public QUndoCommand
{
public:
    using Apply = std::function<bool(bool, QString*)>;
    using Failed = std::function<void(const QString&)>;
    CWorldMapLayoutCommand(const QString& text, Apply apply, Failed failed)
        : QUndoCommand(text), m_apply(apply), m_failed(failed) {}
    void redo() override { run(true); }
    void undo() override { run(false); }
private:
    void run(bool forward)
    {
        QString error;
        if(!m_apply(forward, &error))
        {
            setObsolete(true);
            if(m_failed) m_failed(error);
        }
    }
    Apply m_apply;
    Failed m_failed;
};
#endif
