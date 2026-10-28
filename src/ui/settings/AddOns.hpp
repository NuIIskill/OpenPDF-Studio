#pragma once

#include <QList>

#include <functional>

QT_BEGIN_NAMESPACE
class QVBoxLayout;
class QWidget;
QT_END_NAMESPACE

/// The settings page for add-on packages: cards other parts of the program put on it, and the way to it.
namespace AddOns {

struct Card {
    std::function<void(QVBoxLayout *card)> build;
};

void addCard(Card card);

const QList<Card> &cards();

bool supported();

void open(QWidget *from);

}
