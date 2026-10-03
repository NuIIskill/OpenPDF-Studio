#pragma once

#include <QList>
#include <QWidget>

QT_BEGIN_NAMESPACE
class QGridLayout;
class QLabel;
class QScrollArea;
QT_END_NAMESPACE

class SignatureCard;

/// The "Saved" page of the sign dialog: a grid of the stored signature templates.
class SavedSignaturesView : public QWidget
{
    Q_OBJECT

public:
    explicit SavedSignaturesView(QWidget *parent = nullptr);

    void    refresh(const QString &select = QString());
    QString currentPath() const { return m_current; }

Q_SIGNALS:
    void currentChanged();

private:
    void select(const QString &path);
    void renameCard(SignatureCard *card);
    void deleteCard(SignatureCard *card);

    QScrollArea *m_scroll { nullptr };
    QGridLayout *m_grid   { nullptr };
    QLabel      *m_empty  { nullptr };
    QList<SignatureCard *> m_cards;
    QString m_current;
};
