#include "ui/sign/SavedSignaturesView.hpp"
#include "ui/sign/SignatureCard.hpp"

#include <QGridLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QScrollArea>
#include <QVBoxLayout>

#include <algorithm>

namespace {

constexpr int kColumns = 3;

}

SavedSignaturesView::SavedSignaturesView(QWidget *parent)
    : QWidget(parent)
{
    auto *vl = new QVBoxLayout(this);
    vl->setContentsMargins(16, 12, 16, 12);
    vl->setSpacing(12);

    auto *content = new QWidget;
    content->setObjectName(QStringLiteral("SSavedContent"));
    m_grid = new QGridLayout(content);
    m_grid->setContentsMargins(0, 0, 0, 0);
    m_grid->setSpacing(12);
    for (int c = 0; c < kColumns; ++c) m_grid->setColumnStretch(c, 1);

    m_scroll = new QScrollArea;
    m_scroll->setObjectName(QStringLiteral("SSavedScroll"));
    m_scroll->setWidgetResizable(true);
    m_scroll->setFrameShape(QFrame::NoFrame);
    m_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_scroll->setWidget(content);
    m_scroll->viewport()->setAutoFillBackground(false);

    m_empty = new QLabel(tr("No saved signatures yet."));
    m_empty->setObjectName(QStringLiteral("SMuted"));
    m_empty->setAlignment(Qt::AlignCenter);

    vl->addWidget(m_scroll, 1);
    vl->addWidget(m_empty, 1);
    refresh();
}

void SavedSignaturesView::refresh(const QString &select)
{
    for (SignatureCard *card : std::as_const(m_cards)) {
        m_grid->removeWidget(card);
        card->hide();
        card->deleteLater();
    }
    m_cards.clear();
    for (int r = 0; r < m_grid->rowCount(); ++r) m_grid->setRowStretch(r, 0);

    const QList<SignatureStore::Entry> entries = SignatureStore::list();
    for (int i = 0; i < entries.size(); ++i) {
        auto *card = new SignatureCard(entries.at(i));
        connect(card, &SignatureCard::clicked, this, [this, card]() { this->select(card->entry().path); });
        connect(card, &SignatureCard::renameRequested, this, [this, card]() { renameCard(card); });
        connect(card, &SignatureCard::deleteRequested, this, [this, card]() { deleteCard(card); });
        m_grid->addWidget(card, i / kColumns, i % kColumns);
        m_cards.append(card);
    }
    m_grid->setRowStretch(m_grid->rowCount(), 1);

    m_scroll->setVisible(!m_cards.isEmpty());
    m_empty->setVisible(m_cards.isEmpty());

    const bool keep = std::any_of(m_cards.cbegin(), m_cards.cend(), [&](const SignatureCard *c) {
        return c->entry().path == (select.isEmpty() ? m_current : select);
    });
    this->select(keep ? (select.isEmpty() ? m_current : select) : QString());
}

void SavedSignaturesView::select(const QString &path)
{
    for (SignatureCard *card : std::as_const(m_cards))
        card->setSelected(card->entry().path == path);
    if (path == m_current) return;
    m_current = path;
    Q_EMIT currentChanged();
}

void SavedSignaturesView::renameCard(SignatureCard *card)
{
    const QString path = card->entry().path;
    bool ok = false;
    const QString name = QInputDialog::getText(this, tr("Rename signature"), tr("Name"),
                                               QLineEdit::Normal, card->entry().name, &ok)
                             .trimmed();
    if (!ok || name.isEmpty() || name == card->entry().name) return;
    if (!SignatureStore::rename(path, name))
        QMessageBox::warning(this, window()->windowTitle(), tr("Could not rename the signature."));
    refresh(path);
}

void SavedSignaturesView::deleteCard(SignatureCard *card)
{
    const QString path = card->entry().path;
    if (QMessageBox::question(this, window()->windowTitle(),
                              tr("Delete \"%1\"?").arg(card->entry().name))
            != QMessageBox::Yes)
        return;
    SignatureStore::remove(path);
    refresh();
}
