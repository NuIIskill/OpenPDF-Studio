#include "ui/info/InfoCenter.hpp"

#include "engine/sign/SignatureManager.hpp"
#include "ui/DocumentView.hpp"
#include "ui/info/InfoText.hpp"
#include "ui/protect/ProtectDialog.hpp"
#include "ui/sign/SignatureRemoval.hpp"
#include "ui/sign/SignerTrust.hpp"
#include "ui/theme/Theme.hpp"
#include "ui/view/DocumentInfoLoader.hpp"

#include <QApplication>
#include <QGraphicsDropShadowEffect>
#include <QGridLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLocale>
#include <QMouseEvent>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QTimer>
#include <QVBoxLayout>

#include <functional>

namespace {

constexpr int kCardWidth = 390;
constexpr int kKeyWidth  = 140;

QPointer<InfoCenter> s_open;

QLabel *iconLabel(const QString &name, const QColor &tint, int size, QWidget *parent)
{
    auto *label = new QLabel(parent);
    label->setFixedSize(size, size);
    label->setPixmap(Theme::renderSvg(name, tint, size, parent->devicePixelRatioF()));
    return label;
}

QLabel *textLabel(const QString &text, const char *objectName, QWidget *parent)
{
    auto *label = new QLabel(text, parent);
    label->setObjectName(QLatin1String(objectName));
    label->setWordWrap(true);
    return label;
}

QLabel *keyLabel(const QString &text, QWidget *parent)
{
    QLabel *label = textLabel(text, "InfoKey", parent);
    label->setFixedWidth(kKeyWidth);
    return label;
}

/// One block of the card: an icon, a title, key/value rows and optional details that fold out.
class Section : public QFrame
{
public:
    Section(const QString &icon, const QColor &tint, const QString &title, QWidget *parent)
        : QFrame(parent)
    {
        setObjectName(QStringLiteral("InfoSection"));
        setAttribute(Qt::WA_StyledBackground, true);
        m_grid = new QGridLayout(this);
        m_grid->setContentsMargins(10, 10, 10, 10);
        m_grid->setHorizontalSpacing(12);
        m_grid->setVerticalSpacing(4);
        m_grid->setColumnStretch(1, 1);
        m_grid->addWidget(iconLabel(icon, tint, 22, this), 0, 0, 2, 1, Qt::AlignTop);
        m_grid->addWidget(textLabel(title, "InfoSectionTitle", this), 0, 1);

        m_facts = new QGridLayout;
        m_facts->setContentsMargins(0, 0, 0, 0);
        m_facts->setHorizontalSpacing(8);
        m_facts->setVerticalSpacing(3);
        m_facts->setColumnMinimumWidth(0, kKeyWidth);
        m_facts->setColumnStretch(1, 1);
        m_grid->addLayout(m_facts, 1, 1);
    }

    void addRow(const QString &key, const QString &value, const QColor &color = {})
    {
        const int row = m_facts->rowCount();
        m_facts->addWidget(keyLabel(key, this), row, 0, Qt::AlignTop);
        QLabel *val = textLabel(value, "InfoValue", this);
        if (color.isValid())
            val->setStyleSheet(QStringLiteral("color: %1;").arg(color.name()));
        m_facts->addWidget(val, row, 1, Qt::AlignTop);
    }

    void addLine(const QString &text, const char *objectName = "InfoValue",
                 const QColor &color = {})
    {
        QLabel *label = textLabel(text, objectName, this);
        if (color.isValid())
            label->setStyleSheet(QStringLiteral("color: %1;").arg(color.name()));
        m_facts->addWidget(label, m_facts->rowCount(), 0, 1, 2);
    }

    QVBoxLayout *details(bool expanded, std::function<void(bool)> toggled)
    {
        m_toggled = std::move(toggled);
        m_details = new QWidget(this);
        auto *layout = new QVBoxLayout(m_details);
        layout->setContentsMargins(0, 6, 0, 0);
        layout->setSpacing(8);
        m_grid->addWidget(m_details, 2, 1);

        m_chevron = new QLabel(this);
        m_chevron->setObjectName(QStringLiteral("InfoChevron"));
        m_chevron->setFixedSize(16, 16);
        m_grid->addWidget(m_chevron, 0, 2, 2, 1, Qt::AlignVCenter);
        setProperty("expandable", true);
        setCursor(Qt::PointingHandCursor);
        setExpanded(expanded);
        return layout;
    }

    void setAction(std::function<void()> action)
    {
        m_action = std::move(action);
        setProperty("clickable", true);
        setCursor(Qt::PointingHandCursor);
    }

protected:
    void mouseReleaseEvent(QMouseEvent *event) override
    {
        const QPoint at = event->position().toPoint();
        const bool inDetails = m_details && m_details->isVisible()
                            && m_details->geometry().contains(at);
        const bool onChevron = m_chevron && m_chevron->geometry().contains(at);
        if (event->button() == Qt::LeftButton && !inDetails) {
            if (m_action && !onChevron) {
                m_action();
            } else if (m_details) {
                setExpanded(!m_details->isVisible());
                if (m_toggled) m_toggled(m_details->isVisible());
            }
        }
        QFrame::mouseReleaseEvent(event);
    }

private:
    void setExpanded(bool expanded)
    {
        m_details->setVisible(expanded);
        m_chevron->setPixmap(Theme::renderSvg(expanded ? QStringLiteral("chevron-down")
                                                       : QStringLiteral("chevron-right"),
                                              Theme::IconMuted, 16, devicePixelRatioF()));
    }

    QGridLayout *m_grid    { nullptr };
    QGridLayout *m_facts   { nullptr };
    QWidget     *m_details { nullptr };
    QLabel      *m_chevron { nullptr };
    std::function<void(bool)> m_toggled;
    std::function<void()>     m_action;
};

/// A block in a section's details that runs an action when clicked.
class ClickBlock : public QWidget
{
public:
    ClickBlock(std::function<void()> clicked, QWidget *parent)
        : QWidget(parent)
        , m_clicked(std::move(clicked))
    {
        setObjectName(QStringLiteral("InfoClickBlock"));
        setAttribute(Qt::WA_StyledBackground, true);
        setCursor(Qt::PointingHandCursor);
    }

protected:
    void mousePressEvent(QMouseEvent *event) override { event->accept(); }

    void mouseReleaseEvent(QMouseEvent *event) override
    {
        event->accept();
        if (event->button() == Qt::LeftButton && rect().contains(event->position().toPoint()))
            m_clicked();
    }

private:
    std::function<void()> m_clicked;
};

QWidget *detailBlock(const QString &title, const QString &status, const QColor &statusColor,
                     const QList<std::pair<QString, QString>> &rows, QWidget *parent,
                     std::function<void()> clicked = {}, const QString &icon = {})
{
    QWidget *block = clicked ? new ClickBlock(std::move(clicked), parent) : new QWidget(parent);
    auto *grid = new QGridLayout(block);
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setHorizontalSpacing(8);
    grid->setVerticalSpacing(2);
    grid->setColumnMinimumWidth(0, kKeyWidth);
    grid->setColumnStretch(1, 1);
    auto *head = new QHBoxLayout;
    head->setContentsMargins(0, 0, 0, 0);
    head->addWidget(textLabel(title, "InfoDetailTitle", block), 1);
    if (!icon.isEmpty())
        head->addWidget(iconLabel(icon, Theme::IconMuted, 16, block), 0, Qt::AlignTop);
    grid->addLayout(head, 0, 0, 1, 2);
    if (!status.isEmpty()) {
        QLabel *label = textLabel(status, "InfoValue", block);
        label->setStyleSheet(QStringLiteral("color: %1;").arg(statusColor.name()));
        grid->addWidget(label, 1, 0, 1, 2);
    }
    for (const auto &[key, value] : rows) {
        if (value.isEmpty()) continue;
        const int row = grid->rowCount();
        grid->addWidget(keyLabel(key, block), row, 0, Qt::AlignTop);
        grid->addWidget(textLabel(value, "InfoValue", block), row, 1, Qt::AlignTop);
    }
    return block;
}

QString dateText(const QDateTime &time)
{
    return time.isValid() ? QLocale().toString(time.toLocalTime(), QLocale::ShortFormat)
                          : QString();
}


using Fold = std::function<QVBoxLayout *(Section *, const QString &)>;

QWidget *fileRow(const QString &name, const QString &path, QWidget *parent)
{
    auto *file = new QWidget(parent);
    auto *row = new QHBoxLayout(file);
    row->setContentsMargins(10, 4, 10, 10);
    row->setSpacing(12);
    row->addWidget(iconLabel(QStringLiteral("file-badge"), InfoText::errorColor(), 32, file),
                   0, Qt::AlignTop);
    auto *names = new QVBoxLayout;
    names->setSpacing(2);
    names->addWidget(textLabel(name, "InfoFileName", file));
    QLabel *where = textLabel(QString(), "InfoFilePath", file);
    where->setWordWrap(false);
    where->setText(where->fontMetrics().elidedText(path, Qt::ElideMiddle, kCardWidth - 90));
    where->setToolTip(path);
    names->addWidget(where);
    row->addLayout(names, 1);
    return file;
}

Section *generalSection(const DocumentInfo &info, int pages, QWidget *parent)
{
    auto *general = new Section(QStringLiteral("info"), Theme::Primary,
                                InfoCenter::tr("General"), parent);
    general->addRow(InfoCenter::tr("PDF version"),
                    info.pdfVersion.isEmpty() ? InfoCenter::tr("Unknown") : info.pdfVersion);
    general->addRow(InfoCenter::tr("Pages"), QString::number(pages));
    general->addRow(InfoCenter::tr("File size"),
                    QLocale().formattedDataSize(info.fileSize, 1,
                                                QLocale::DataSizeTraditionalFormat));
    return general;
}

Section *securitySection(const DocumentInfo &info, const Fold &fold,
                         std::function<void()> protect, QWidget *parent)
{
    auto *security = new Section(QStringLiteral("lock"),
                                 info.encrypted ? Theme::Primary : Theme::IconMuted,
                                 InfoCenter::tr("Security"), parent);
    security->setAction(std::move(protect));
    security->addRow(InfoCenter::tr("Encrypted"),
                     !info.encrypted              ? InfoCenter::tr("No")
                     : info.encryption.isEmpty() ? InfoCenter::tr("Yes")
                                                 : info.encryption);
    if (!info.encrypted) return security;

    security->addRow(InfoCenter::tr("Permissions"), info.restricted()
                                                        ? InfoCenter::tr("Restricted")
                                                        : InfoCenter::tr("All allowed"));
    const DocumentInfo::Permissions &p = info.permissions;
    const auto allowed = [](bool on) {
        return on ? InfoCenter::tr("Allowed") : InfoCenter::tr("Not allowed");
    };
    fold(security, QStringLiteral("security"))->addWidget(detailBlock(
        info.passwordUsed ? InfoCenter::tr("Opened with password")
                          : InfoCenter::tr("Opens without password"),
        {}, {},
        { { InfoCenter::tr("Printing"), allowed(p.print) },
          { InfoCenter::tr("High-quality printing"), allowed(p.printHighRes) },
          { InfoCenter::tr("Changing content"), allowed(p.modify) },
          { InfoCenter::tr("Copying text and images"), allowed(p.copy) },
          { InfoCenter::tr("Comments"), allowed(p.annotate) },
          { InfoCenter::tr("Filling in forms"), allowed(p.fillForms) },
          { InfoCenter::tr("Assembling pages"), allowed(p.assemble) },
          { InfoCenter::tr("Accessibility"), allowed(p.accessibility) } },
        security));
    return security;
}

Section *signatureSection(const DocumentInfo &info, const Fold &fold,
                          const std::function<void(const SignatureStatus &)> &remove,
                          const std::function<void(const SignatureStatus &)> &trust,
                          QWidget *parent)
{
    using State = DocumentInfo::SignatureState;
    const State state = info.signatureState();
    auto *signatures = new Section(InfoText::signatureIcon(info), InfoText::signatureColor(info),
                                   InfoCenter::tr("Digital signatures"), parent);
    signatures->addLine(InfoText::signatureSummary(info));
    if (state == State::Valid || state == State::Untrusted)
        signatures->addLine(InfoCenter::tr("Document unchanged since signing"), "InfoMuted");
    else if (state == State::Unchecked && !SignatureManager::available())
        signatures->addLine(InfoCenter::tr("This version cannot check signatures"), "InfoMuted");
    if (info.signatures.isEmpty()) return signatures;

    QVBoxLayout *more = fold(signatures, QStringLiteral("signatures"));
    for (const SignatureStatus &s : info.signatures) {
        using S = SignatureStatus::State;
        const QString status = s.state == S::Valid     ? InfoCenter::tr("Valid")
                             : s.state == S::Untrusted ? InfoCenter::tr("Valid, signer not trusted")
                                                       : InfoCenter::tr("Invalid");
        const QColor color = s.state == S::Valid     ? Theme::Success
                           : s.state == S::Untrusted ? InfoText::warningColor()
                                                     : InfoText::errorColor();
        more->addWidget(detailBlock(
            s.signer.isEmpty() ? InfoCenter::tr("Unknown signer") : s.signer, status, color,
            { { InfoCenter::tr("Signed"), dateText(s.time) },
              { InfoCenter::tr("Reason"), s.reason },
              { InfoCenter::tr("Location"), s.location },
              { InfoCenter::tr("Field"), s.field } },
            signatures, [remove, s]() { remove(s); }, QStringLiteral("trash-2")));
        if (s.state == S::Untrusted && !s.fingerprint.isEmpty()) {
            auto *button = new QPushButton(InfoCenter::tr("Trust this signer..."), signatures);
            button->setObjectName(QStringLiteral("InfoActionLink"));
            button->setCursor(Qt::PointingHandCursor);
            QObject::connect(button, &QPushButton::clicked, signatures, [trust, s]() { trust(s); });
            more->addWidget(button, 0, Qt::AlignLeft);
        }
    }
    return signatures;
}

Section *featureSection(const DocumentInfo &info, QWidget *parent)
{
    const QString none = InfoCenter::tr("None");
    auto *features = new Section(QStringLiteral("layers"), Theme::IconMuted,
                                 InfoCenter::tr("PDF features"), parent);
    features->addRow(InfoCenter::tr("Forms"),
                     info.formFields == 0 ? none
                     : info.formFields == 1 ? InfoCenter::tr("1 field")
                                            : InfoCenter::tr("%1 fields").arg(info.formFields));
    features->addRow(InfoCenter::tr("Attachments"),
                     info.attachments == 0 ? none : QString::number(info.attachments));
    features->addRow(InfoCenter::tr("PDF/A"),
                     info.pdfA.isEmpty() ? InfoCenter::tr("No") : info.pdfA);
    return features;
}

}

void InfoCenter::toggle(DocumentView *view, QWidget *host, QWidget *anchor,
                        const QString &unfold)
{
    if (s_open) {
        s_open->close();
        return;
    }
    if (!view || !host || view->pageCount() <= 0) return;
    s_open = new InfoCenter(view, host, anchor, unfold);
    s_open->place();
    s_open->show();
    s_open->raise();
    s_open->setFocus(Qt::PopupFocusReason);
    s_open->scrollToUnfolded();
    qApp->installEventFilter(s_open);
}

void InfoCenter::dismiss()
{
    if (s_open) s_open->close();
}

InfoCenter::InfoCenter(DocumentView *view, QWidget *host, QWidget *anchor,
                       const QString &unfold)
    : QFrame(host)
    , m_view(view)
    , m_anchor(anchor)
    , m_unfold(unfold)
{
    if (!unfold.isEmpty()) m_expanded.insert(unfold);
    setObjectName(QStringLiteral("InfoCenterCard"));
    setAttribute(Qt::WA_DeleteOnClose);
    setAttribute(Qt::WA_StyledBackground, true);
    setFocusPolicy(Qt::StrongFocus);
    setFixedWidth(kCardWidth);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(10, 12, 10, 10);
    root->setSpacing(6);

    auto *head = new QHBoxLayout;
    head->setContentsMargins(10, 0, 0, 0);
    head->addWidget(textLabel(tr("Document information"), "InfoCenterTitle", this), 1);
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
    head->addWidget(closeBtn);
    root->addLayout(head);

    m_scroll = new QScrollArea(this);
    m_scroll->setObjectName(QStringLiteral("InfoCenterScroll"));
    m_scroll->setFrameShape(QFrame::NoFrame);
    m_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_scroll->setWidgetResizable(true);
    root->addWidget(m_scroll);

    auto *shadow = new QGraphicsDropShadowEffect(this);
    shadow->setBlurRadius(28);
    shadow->setOffset(0, 6);
    shadow->setColor(QColor(0, 0, 0, Theme::DarkMode ? 160 : 45));
    setGraphicsEffect(shadow);

    connect(view->documentInfo(), &DocumentInfoLoader::changed, this, [this]() {
        rebuild();
        place();
        scrollToUnfolded();
    });
    rebuild();
}

void InfoCenter::rebuild()
{
    if (!m_view) return;
    const DocumentInfoLoader *loader = m_view->documentInfo();
    const DocumentInfo &info = loader->info();

    m_body = new QWidget;
    m_body->setObjectName(QStringLiteral("InfoCenterBody"));
    m_rows = new QVBoxLayout(m_body);
    m_rows->setContentsMargins(0, 0, 0, 0);
    m_rows->setSpacing(0);

    const Fold fold = [this](Section *section, const QString &key) {
        if (key == m_unfold) m_unfolded = section;
        return section->details(m_expanded.contains(key), [this, key](bool open) {
            if (open) m_expanded.insert(key); else m_expanded.remove(key);
            place();
        });
    };

    const QString shownPath = m_view->currentFile().isEmpty() ? info.path : m_view->currentFile();
    m_rows->addWidget(fileRow(m_view->displayName(), shownPath, m_body));
    m_rows->addWidget(generalSection(info, m_view->pageCount(), m_body));

    if (!loader->loaded()) {
        m_rows->addWidget(new Section(QStringLiteral("refresh-cw"), Theme::IconMuted,
                                      tr("Reading document..."), m_body));
    } else if (!info.complete) {
        auto *missing = new Section(QStringLiteral("info"), Theme::IconMuted,
                                    tr("Further details unavailable"), m_body);
        missing->addLine(tr("Protection, signatures and features could not be read."),
                         "InfoMuted");
        m_rows->addWidget(missing);
    } else {
        const QPointer<DocumentView> view = m_view;
        const QPointer<QWidget> host = parentWidget();
        m_rows->addWidget(securitySection(info, fold, [this, view, host]() {
            closeThen([view, host]() { ProtectDialog::run(view, host); });
        }, m_body));
        m_rows->addWidget(signatureSection(info, fold,
                                           [this, view, host](const SignatureStatus &s) {
            closeThen([view, host, s]() { SignatureRemoval::run(view, s, host); });
        }, [this, host](const SignatureStatus &s) {
            closeThen([host, s]() { SignerTrust::ask(s, host); });
        }, m_body));
        m_rows->addWidget(featureSection(info, m_body));
    }
    m_rows->addStretch(1);

    m_scroll->setWidget(m_body);
}

void InfoCenter::place()
{
    QWidget *host = parentWidget();
    if (!host) return;

    const QMargins margins = layout()->contentsMargins();
    const int chrome = margins.top() + margins.bottom()
                     + layout()->itemAt(0)->sizeHint().height() + layout()->spacing();
    const int content = m_rows->totalHeightForWidth(kCardWidth - margins.left() - margins.right());
    const int bottom = m_anchor
        ? host->mapFromGlobal(m_anchor->mapToGlobal(QPoint(0, 0))).y() - 6
        : host->height() - 8;
    const int height = qMin(chrome + content + 2, bottom - 8);
    const int left = m_anchor ? host->mapFromGlobal(m_anchor->mapToGlobal(QPoint(0, 0))).x() : 8;

    setGeometry(qBound(8, left, qMax(8, host->width() - kCardWidth - 8)),
                bottom - height, kCardWidth, height);
}

void InfoCenter::scrollToUnfolded()
{
    if (!m_unfolded) return;
    m_rows->activate();
    m_scroll->verticalScrollBar()->setValue(m_unfolded->y());
    m_unfold.clear();
    m_unfolded = nullptr;
}

void InfoCenter::closeThen(const std::function<void()> &next)
{
    close();
    QTimer::singleShot(0, qApp, next);
}

bool InfoCenter::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() == QEvent::KeyPress
            && static_cast<QKeyEvent *>(event)->key() == Qt::Key_Escape) {
        close();
        return true;
    }
    if (event->type() == QEvent::MouseButtonPress) {
        auto *w = qobject_cast<QWidget *>(watched);
        const QPoint at = static_cast<QMouseEvent *>(event)->globalPosition().toPoint();
        const bool onAnchor = m_anchor && m_anchor->rect().contains(m_anchor->mapFromGlobal(at));
        if (w && w->window() == window() && !onAnchor && !rect().contains(mapFromGlobal(at)))
            close();
    }
    return QFrame::eventFilter(watched, event);
}

void InfoCenter::hideEvent(QHideEvent *event)
{
    qApp->removeEventFilter(this);
    QFrame::hideEvent(event);
}
