#pragma once

#include <QUndoCommand>
#include <QWidget>
#include <QPoint>

class PlaceAnnotationCommand : public QUndoCommand
{
public:
    PlaceAnnotationCommand(QWidget *ann, const QString &text, QUndoCommand *parent = nullptr)
        : QUndoCommand(text, parent), m_ann(ann) {}

    void undo() override { m_ann->hide(); }
    void redo() override { m_ann->show(); m_ann->raise(); }

private:
    QWidget *m_ann;
};

class DeleteAnnotationCommand : public QUndoCommand
{
public:
    DeleteAnnotationCommand(QWidget *ann, const QString &text, QUndoCommand *parent = nullptr)
        : QUndoCommand(text, parent), m_ann(ann) {}

    void undo() override { m_ann->show(); m_ann->raise(); }
    void redo() override { m_ann->hide(); }

private:
    QWidget *m_ann;
};

class MoveAnnotationCommand : public QUndoCommand
{
public:
    MoveAnnotationCommand(QWidget *ann, const QPoint &from, const QPoint &to,
                          const QString &text, QUndoCommand *parent = nullptr)
        : QUndoCommand(text, parent), m_ann(ann), m_old(from), m_new(to) {}

    void undo() override { m_ann->move(m_old); }
    void redo() override { m_ann->move(m_new); }

    int id() const override { return static_cast<int>(reinterpret_cast<quintptr>(m_ann) & 0x7FFFFFFF); }

    bool mergeWith(const QUndoCommand *other) override {
        if (other->id() != id()) return false;
        m_new = static_cast<const MoveAnnotationCommand *>(other)->m_new;
        return true;
    }

private:
    QWidget *m_ann;
    QPoint   m_old;
    QPoint   m_new;
};
