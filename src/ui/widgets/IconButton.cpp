#include "IconButton.hpp"
#include "ui/theme/Theme.hpp"

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
}

void IconButton::setIconName(const QString &name, const QColor &normalColor)
{
    m_iconName    = name;
    m_normalColor = normalColor.isValid() ? normalColor : Theme::IconNormal;
    m_hoverColor  = Theme::DarkMode ? QColor(QStringLiteral("#E5E7EB")) : QColor(QStringLiteral("#111827"));

    m_normalIcon = Theme::makeIcon(name, normalColor,
                                   Theme::IconChecked, Theme::IconDisabled);
    m_hoverIcon  = Theme::makeIcon(name, m_hoverColor,
                                   Theme::IconChecked, Theme::IconDisabled);

    setIcon(m_normalIcon);
    setIconSize(QSize(20, 20));
    setText(QString());
}

void IconButton::setToggle(bool on)
{
    setCheckable(on);
}

void IconButton::enterEvent(QEnterEvent *event)
{
    QPushButton::enterEvent(event);
    if (!m_iconName.isEmpty() && !isChecked())
        setIcon(m_hoverIcon);
}

void IconButton::leaveEvent(QEvent *event)
{
    QPushButton::leaveEvent(event);
    if (!m_iconName.isEmpty() && !isChecked())
        setIcon(m_normalIcon);
}
