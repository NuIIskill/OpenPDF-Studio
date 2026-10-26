#include "ui/sign/SignatureStatusCard.hpp"

#include "ui/info/InfoText.hpp"
#include "ui/theme/Theme.hpp"

#include <QApplication>
#include <QGraphicsDropShadowEffect>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPushButton>
#include <QVBoxLayout>

namespace {

constexpr int kCardWidth = 300;

QLabel *textLabel(const QString &text, const char *objectName, QWidget *parent)
{
    auto *label = new QLabel(text, parent);
    label->setObjectName(QLatin1String(objectName));
    label->setWordWrap(true);
    return label;
}

QPushButton *linkButton(const QString &text, QWidget *parent)
{
    auto *button = new QPushButton(text, parent);
    button->setObjectName(QStringLiteral("InfoActionLink"));
    button->setCursor(Qt::PointingHandCursor);
    return button;
}

}

SignatureStatusCard::SignatureStatusCard(const SignatureStatus &signature, QWidget *anchor,
                                         QWidget *parent)
    : QFrame(parent)
    , m_field(signature.field)
    , m_anchor(anchor)
{
    using S = SignatureStatus::State;
    setObjectName(QStringLiteral("SignatureStatusCard"));
    setAttribute(Qt::WA_DeleteOnClose);
    setAttribute(Qt::WA_StyledBackground, true);
    setFocusPolicy(Qt::StrongFocus);
    setFixedWidth(kCardWidth);

    const bool changed = signature.state != S::Invalid && !signature.coversWholeFile;
    const QString title = signature.state == S::Invalid ? tr("Signature invalid")
                        : changed                        ? tr("Changed after signing")
                        : signature.state == S::Valid    ? tr("Signature valid")
                                                         : tr("Signer not trusted");
    QStringList lines;
    if (signature.state == S::Invalid) {
        lines << tr("The document was changed after signing or the signature is damaged");
    } else {
        lines << (changed ? tr("The signed version is intact, but changes were saved after it")
                          : tr("Document unchanged since signing"));
        lines << (signature.state == S::Valid ? tr("Certificate trusted on this computer")
                                              : tr("Certificate not trusted on this computer"));
    }

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(18, 14, 14, 14);
    root->setSpacing(10);

    auto *head = new QHBoxLayout;
    head->setSpacing(10);
    auto *icon = new QLabel(this);
    icon->setPixmap(Theme::renderSvg(iconName(signature), color(signature), 24,
                                     devicePixelRatioF()));
    head->addWidget(icon);
    head->addWidget(textLabel(title, "SignatureStatusTitle", this), 1);
    auto *closeBtn = new QPushButton(this);
    closeBtn->setObjectName(QStringLiteral("InfoCenterClose"));
    closeBtn->setIcon(Theme::makeIcon(QStringLiteral("x"), Theme::IconMuted,
                                      Theme::IconMuted, Theme::IconDisabled, 16));
    closeBtn->setIconSize(QSize(16, 16));
    closeBtn->setFixedSize(24, 24);
    closeBtn->setCursor(Qt::PointingHandCursor);
    closeBtn->setFlat(true);
    closeBtn->setToolTip(tr("Close"));
    connect(closeBtn, &QPushButton::clicked, this, &QWidget::close);
    head->addWidget(closeBtn, 0, Qt::AlignTop);
    root->addLayout(head);

    root->addWidget(textLabel(signature.signer.isEmpty() ? tr("Unknown signer") : signature.signer,
                              "SignatureStatusSigner", this));
    auto *text = new QVBoxLayout;
    text->setSpacing(4);
    for (const QString &line : std::as_const(lines))
        text->addWidget(textLabel(line, "SignatureStatusLine", this));
    root->addLayout(text);

    if (signature.state == S::Untrusted && !signature.fingerprint.isEmpty()) {
        QPushButton *trust = linkButton(tr("Trust this signer..."), this);
        connect(trust, &QPushButton::clicked, this, &SignatureStatusCard::trustRequested);
        root->addWidget(trust, 0, Qt::AlignLeft);
    }

    auto *divider = new QFrame(this);
    divider->setObjectName(QStringLiteral("SignatureStatusDivider"));
    divider->setFixedHeight(1);
    root->addWidget(divider);

    auto *foot = new QHBoxLayout;
    QPushButton *details = linkButton(tr("Certificate details"), this);
    connect(details, &QPushButton::clicked, this, &SignatureStatusCard::detailsRequested);
    foot->addWidget(details);
    foot->addStretch(1);
    m_verify = new QPushButton(tr("Verify again"), this);
    m_verify->setObjectName(QStringLiteral("SignatureStatusButton"));
    m_verify->setCursor(Qt::PointingHandCursor);
    connect(m_verify, &QPushButton::clicked, this, &SignatureStatusCard::verifyRequested);
    foot->addWidget(m_verify);
    root->addLayout(foot);

    auto *shadow = new QGraphicsDropShadowEffect(this);
    shadow->setBlurRadius(28);
    shadow->setOffset(0, 6);
    shadow->setColor(QColor(0, 0, 0, Theme::DarkMode ? 160 : 45));
    setGraphicsEffect(shadow);
    adjustSize();
}

void SignatureStatusCard::setChecking()
{
    m_verify->setEnabled(false);
    m_verify->setText(tr("Checking..."));
}

QString SignatureStatusCard::iconName(const SignatureStatus &signature)
{
    return signature.state == SignatureStatus::State::Valid && signature.coversWholeFile
        ? QStringLiteral("shield-check") : QStringLiteral("shield-alert");
}

QColor SignatureStatusCard::color(const SignatureStatus &signature)
{
    if (signature.state == SignatureStatus::State::Invalid) return InfoText::errorColor();
    return signature.state == SignatureStatus::State::Valid && signature.coversWholeFile
        ? Theme::Success : InfoText::warningColor();
}

bool SignatureStatusCard::eventFilter(QObject *watched, QEvent *event)
{
    auto *w = qobject_cast<QWidget *>(watched);
    if (event->type() == QEvent::KeyPress && w && w->window() == window()
            && static_cast<QKeyEvent *>(event)->key() == Qt::Key_Escape) {
        close();
        return true;
    }
    if (event->type() == QEvent::MouseButtonPress) {
        const QPoint at = static_cast<QMouseEvent *>(event)->globalPosition().toPoint();
        const bool onAnchor = m_anchor && m_anchor->rect().contains(m_anchor->mapFromGlobal(at));
        if (w && w->window() == window() && !onAnchor && !rect().contains(mapFromGlobal(at)))
            close();
    }
    return QFrame::eventFilter(watched, event);
}

void SignatureStatusCard::showEvent(QShowEvent *event)
{
    qApp->installEventFilter(this);
    QFrame::showEvent(event);
}

void SignatureStatusCard::hideEvent(QHideEvent *event)
{
    qApp->removeEventFilter(this);
    QFrame::hideEvent(event);
}
