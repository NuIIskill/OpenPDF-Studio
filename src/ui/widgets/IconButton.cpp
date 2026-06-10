#include "IconButton.hpp"

#include <QEnterEvent>

IconButton::IconButton(const QString &label, QWidget *parent)
    : QPushButton(label, parent)
{
    init();
}

IconButton::IconButton(QWidget *parent)
    : QPushButton(parent)
{
    init();
}

void IconButton::init()
{
    setObjectName(QStringLiteral("IconButton"));
    setFixedSize(36, 36);
    setCursor(Qt::PointingHandCursor);
    setFlat(true);
    // Stylesheet handles visual states — nothing more needed here.
}

void IconButton::setIconName(const QString &name)
{
    m_iconName = name;
    // TODO: load SVG from :/icons/<name>.svg and call setIcon().
}

void IconButton::setToggle(bool on)
{
    setCheckable(on);
}

void IconButton::enterEvent(QEnterEvent *event)
{
    QPushButton::enterEvent(event);
    // Hover styling is driven entirely by QSS :hover pseudo-state.
}

void IconButton::leaveEvent(QEvent *event)
{
    QPushButton::leaveEvent(event);
}
