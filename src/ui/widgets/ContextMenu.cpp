#include "ui/widgets/ContextMenu.hpp"

#include "ui/theme/Theme.hpp"

#include <QAction>
#include <QStyleOptionMenuItem>

namespace {

QColor dangerColor()
{
    return Theme::DarkMode ? QColor(QStringLiteral("#F87171"))
                           : QColor(QStringLiteral("#DC2626"));
}

}

ContextMenu::ContextMenu(QWidget *parent)
    : QMenu(parent)
{
    setObjectName(QStringLiteral("ImageMenu"));
}

QAction *ContextMenu::addAction(const char *icon, const QString &text)
{
    return QMenu::addAction(Theme::makeIcon(QString::fromLatin1(icon)), text);
}

QAction *ContextMenu::addDangerAction(const char *icon, const QString &text)
{
    m_danger = QMenu::addAction(
        Theme::makeIcon(QString::fromLatin1(icon), dangerColor(), dangerColor()), text);
    return m_danger;
}

void ContextMenu::initStyleOption(QStyleOptionMenuItem *option, const QAction *action) const
{
    QMenu::initStyleOption(option, action);
    if (!action->isEnabled()) return;
    const QColor text = action == m_danger ? dangerColor()
                                           : option->palette.color(QPalette::ButtonText);
    for (const QPalette::ColorRole role : { QPalette::Text, QPalette::ButtonText,
                                            QPalette::WindowText, QPalette::HighlightedText })
        option->palette.setColor(role, text);
}
