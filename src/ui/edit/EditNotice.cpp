#include "ui/edit/EditNotice.hpp"

#include "app/AppSettings.hpp"
#include "ui/theme/Theme.hpp"

#include <QEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QTimer>
#include <QToolButton>

namespace {

constexpr int HoldMs   = 12000;
constexpr int MarginPx = 16;
constexpr int TextWidthPx = 420;

}

EditNotice::EditNotice(QWidget *host)
    : QFrame(host)
{
    setObjectName(QStringLiteral("EditNotice"));
    setAttribute(Qt::WA_StyledBackground, true);

    auto *row = new QHBoxLayout(this);
    row->setContentsMargins(14, 10, 8, 10);
    row->setSpacing(10);

    m_icon = new QLabel(this);
    row->addWidget(m_icon, 0, Qt::AlignTop);

    m_text = new QLabel(this);
    m_text->setObjectName(QStringLiteral("EditNoticeText"));
    m_text->setWordWrap(true);
    m_text->setFixedWidth(TextWidthPx);
    row->addWidget(m_text, 1);

    auto *never = new QPushButton(tr("Don't show again"), this);
    never->setObjectName(QStringLiteral("EditNoticeNever"));
    never->setCursor(Qt::PointingHandCursor);
    never->setFlat(true);
    row->addWidget(never, 0, Qt::AlignVCenter);
    connect(never, &QPushButton::clicked, this, [this]() {
        AppSettings().setNoticeHidden(keyOf(m_kind), true);
        hide();
    });

    auto *close = new QToolButton(this);
    close->setObjectName(QStringLiteral("EditNoticeClose"));
    close->setText(QStringLiteral("×"));
    close->setToolTip(tr("Close"));
    close->setCursor(Qt::PointingHandCursor);
    close->setAutoRaise(true);
    row->addWidget(close, 0, Qt::AlignTop);
    connect(close, &QToolButton::clicked, this, &QWidget::hide);

    m_hold = new QTimer(this);
    m_hold->setSingleShot(true);
    m_hold->setInterval(HoldMs);
    connect(m_hold, &QTimer::timeout, this, &QWidget::hide);

    host->installEventFilter(this);
    hide();
}

QString EditNotice::keyOf(Kind kind)
{
    return kind == Kind::Scan ? QStringLiteral("scanEditing") : QStringLiteral("substituteFont");
}

void EditNotice::offer(Kind kind)
{
    // Once per document and kind, never once turned off.
    if (m_shown.contains(int(kind)) || AppSettings().noticeHidden(keyOf(kind))) return;
    m_shown.insert(int(kind));
    m_kind = kind;
    m_text->setText(kind == Kind::Scan
        ? tr("This text was scanned. It is recognised automatically, which can cause "
             "errors, and its font or font size can differ from the original. Please check "
             "the result.")
        : tr("The font of this text cannot be used for editing. The text is set in the "
             "closest font available, which can look a little different."));
    m_icon->setPixmap(Theme::renderSvg(QStringLiteral("info"), Theme::Primary, 18,
                                       devicePixelRatioF()));
    adjustSize();
    reposition();
    show();
    raise();
    m_hold->start();
}

void EditNotice::reposition()
{
    QWidget *host = parentWidget();
    if (!host) return;
    move((host->width() - width()) / 2, host->height() - height() - MarginPx);
}

bool EditNotice::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == parentWidget() && event->type() == QEvent::Resize && isVisible())
        reposition();
    return QFrame::eventFilter(watched, event);
}
