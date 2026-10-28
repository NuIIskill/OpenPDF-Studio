#include "ui/settings/AddOns.hpp"
#include "ui/MainWindow.hpp"

#include <QApplication>

namespace {

QList<AddOns::Card> &registry()
{
    static QList<AddOns::Card> list;
    return list;
}

}

void AddOns::addCard(Card card)
{
    if (!card.build) return;
    registry().append(std::move(card));
}

const QList<AddOns::Card> &AddOns::cards()
{
    return registry();
}

bool AddOns::supported()
{
#if defined(Q_OS_LINUX) || defined(Q_OS_WIN)
    return true;
#else
    return false;
#endif
}

void AddOns::open(QWidget *from)
{
    if (!supported()) return;
    for (QWidget *w = from; w; w = w->parentWidget())
        if (auto *main = qobject_cast<MainWindow *>(w)) {
            main->openAddOns();
            return;
        }
    for (QWidget *w : QApplication::topLevelWidgets())
        if (auto *main = qobject_cast<MainWindow *>(w)) {
            main->openAddOns();
            return;
        }
}
