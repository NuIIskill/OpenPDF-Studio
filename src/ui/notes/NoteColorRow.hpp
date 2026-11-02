#pragma once

#include <QColor>
#include <QList>
#include <QWidget>

class QAbstractButton;
class QBoxLayout;
class QVBoxLayout;

/// Round swatches for a note's color: one collection the user can change, and a bubble to mix a new color.
class NoteColorRow : public QWidget
{
    Q_OBJECT

public:
    static constexpr int MaxColors = 12;

    explicit NoteColorRow(QWidget *parent = nullptr);

    static QList<QColor> colors();
    static QList<QColor> defaultPalette();
    static QColor standard();

    void setColor(const QColor &color);
    void setPalette(const QList<QColor> &colors);
    void setMixedColor(const QColor &color);
    void setHint(int stage);
    void setDropTargets(bool on);
    void pop(const QColor &color);

Q_SIGNALS:
    void picked(const QColor &color);
    void dropped(const QColor &color, const QPoint &globalPos, int fromSlot);
    void removed(int slot);
    void resetRequested();
    void filled(int slot, const QColor &color);
    void mixed(const QColor &color);
    void dragStarted();
    void dragEnded();

private:
    void rebuild();
    void startShake();
    void shake(qreal progress);
    QList<QAbstractButton *> shaking() const;
    QAbstractButton *addSwatch(const QColor &color, int slot);
    void mix();
    void mixInto(int slot, QWidget *anchor);

    QList<QColor>            m_palette;
    QColor                   m_current;
    QColor                   m_mixed;
    QVBoxLayout             *m_lines { nullptr };
    QAbstractButton         *m_mixer { nullptr };
    QList<QAbstractButton *> m_swatches;
    QList<QAbstractButton *> m_empties;
    int                      m_hint { 0 };
    bool                     m_targets { false };
    class QVariantAnimation *m_shake { nullptr };
    class QTimer            *m_again { nullptr };
};
