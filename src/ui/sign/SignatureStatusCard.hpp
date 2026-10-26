#pragma once

#include "engine/sign/SignatureVerifier.hpp"

#include <QColor>
#include <QFrame>
#include <QPointer>

QT_BEGIN_NAMESPACE
class QPushButton;
QT_END_NAMESPACE

/// The verification result of one digital signature, opened from its place on the page.
class SignatureStatusCard : public QFrame
{
    Q_OBJECT

public:
    SignatureStatusCard(const SignatureStatus &signature, QWidget *anchor, QWidget *parent);

    const QString &field() const { return m_field; }
    void setChecking();

    static QString iconName(const SignatureStatus &signature);
    static QColor  color(const SignatureStatus &signature);

Q_SIGNALS:
    void verifyRequested();
    void detailsRequested();
    void trustRequested();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;

private:
    QString           m_field;
    QPointer<QWidget> m_anchor;
    QPushButton      *m_verify { nullptr };
};
