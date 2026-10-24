// SPDX-License-Identifier: LicenseRef-OpenPDF-Business
#include "rich-media/ui/RichMediaPanel.hpp"

#include "rich-media/engine/MediaDrop.hpp"
#include "rich-media/engine/PosterFrame.hpp"
#include "ui/theme/Theme.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>

namespace {

class Switch : public QCheckBox
{
public:
    explicit Switch(QWidget *parent = nullptr) : QCheckBox(parent)
    {
        setFocusPolicy(Qt::StrongFocus);
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        const QRectF track = QRectF(rect()).adjusted(1, 2, -1, -2);
        const QColor off = Theme::DarkMode ? QColor(QStringLiteral("#5A5A5A"))
                                            : QColor(QStringLiteral("#CBD5E1"));
        const QColor on(QStringLiteral("#2563EB"));
        painter.setPen(Qt::NoPen);
        painter.setBrush(isChecked() ? on : off);
        painter.drawRoundedRect(track, track.height() / 2.0, track.height() / 2.0);

        const qreal diameter = track.height() - 4.0;
        const qreal x = isChecked() ? track.right() - diameter - 2.0
                                    : track.left() + 2.0;
        painter.setBrush(Qt::white);
        painter.drawEllipse(QRectF(x, track.top() + 2.0, diameter, diameter));
    }
};

QLabel *sectionLabel(QWidget *parent)
{
    auto *label = new QLabel(parent);
    label->setObjectName(QStringLiteral("MediaPanelSection"));
    return label;
}

QFrame *divider(QWidget *parent)
{
    auto *line = new QFrame(parent);
    line->setObjectName(QStringLiteral("MediaPanelDivider"));
    line->setFrameShape(QFrame::HLine);
    return line;
}

void addSwitchRow(QGridLayout *layout, int row, QWidget *parent,
                  QLabel **label, QCheckBox **toggle)
{
    *label = new QLabel(parent);
    *toggle = new Switch(parent);
    (*toggle)->setObjectName(QStringLiteral("MediaPanelSwitch"));
    (*toggle)->setFixedSize(32, 20);
    (*toggle)->setCursor(Qt::PointingHandCursor);
    layout->addWidget(*label, row, 0);
    layout->addWidget(*toggle, row, 1, Qt::AlignRight | Qt::AlignVCenter);
}

QPushButton *actionButton(const QString &name, QWidget *parent)
{
    auto *button = new QPushButton(parent);
    button->setObjectName(name);
    button->setFixedHeight(34);
    button->setCursor(Qt::PointingHandCursor);
    button->setIconSize(QSize(16, 16));
    return button;
}

}

RichMediaPanel::RichMediaPanel(QWidget *parent)
    : QFrame(parent)
{
    setObjectName(QStringLiteral("MediaPanel"));
    setAttribute(Qt::WA_StyledBackground, true);
    setFixedWidth(kWidth);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
    buildUi();
    retranslateUi();
    applyStyle();
}

void RichMediaPanel::buildUi()
{
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);

    auto *scroll = new QScrollArea(this);
    scroll->setObjectName(QStringLiteral("MediaPanelScroll"));
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    outer->addWidget(scroll, 1);

    auto *content = new QWidget(scroll);
    content->setObjectName(QStringLiteral("MediaPanelContent"));
    scroll->setWidget(content);

    auto *root = new QVBoxLayout(content);
    root->setContentsMargins(16, 16, 16, 16);
    root->setSpacing(0);

    auto *header = new QHBoxLayout;
    header->setContentsMargins(0, 0, 0, 0);
    header->setSpacing(12);
    m_icon = new QLabel(content);
    m_icon->setFixedSize(24, 24);
    header->addWidget(m_icon, 0, Qt::AlignTop);
    auto *titles = new QVBoxLayout;
    titles->setContentsMargins(0, 0, 0, 0);
    titles->setSpacing(4);
    m_title = new QLabel(content);
    m_title->setObjectName(QStringLiteral("MediaPanelTitle"));
    titles->addWidget(m_title);
    m_subtitle = new QLabel(content);
    m_subtitle->setObjectName(QStringLiteral("MediaPanelSubtitle"));
    titles->addWidget(m_subtitle);
    header->addLayout(titles, 1);
    m_close = new QPushButton(content);
    m_close->setObjectName(QStringLiteral("MediaPanelClose"));
    m_close->setFixedSize(24, 24);
    m_close->setCursor(Qt::PointingHandCursor);
    connect(m_close, &QPushButton::clicked, this,
            [this]() { Q_EMIT closeRequested(); });
    header->addWidget(m_close, 0, Qt::AlignTop);
    root->addLayout(header);
    root->addSpacing(22);

    m_posterLabel = sectionLabel(content);
    root->addWidget(m_posterLabel);
    root->addSpacing(10);

    m_posterView = new QLabel(content);
    m_posterView->setObjectName(QStringLiteral("MediaPanelPoster"));
    m_posterView->setFixedSize(kWidth - 32, 150);
    m_posterView->setAlignment(Qt::AlignCenter);
    root->addWidget(m_posterView);
    root->addSpacing(12);

    auto *posterActions = new QHBoxLayout;
    posterActions->setContentsMargins(0, 0, 0, 0);
    posterActions->setSpacing(20);
    m_posterChange = actionButton(QStringLiteral("MediaPanelPrimary"), content);
    connect(m_posterChange, &QPushButton::clicked, this, &RichMediaPanel::choosePoster);
    posterActions->addWidget(m_posterChange, 1);
    m_posterReset = actionButton(QStringLiteral("MediaPanelDanger"), content);
    connect(m_posterReset, &QPushButton::clicked, this, [this]() {
        m_posterFromUser = false;
        refreshPoster();
    });
    posterActions->addWidget(m_posterReset, 1);
    root->addLayout(posterActions);

    root->addSpacing(22);
    root->addWidget(divider(content));
    root->addSpacing(20);

    m_playbackLabel = new QLabel(content);
    m_playbackLabel->setObjectName(QStringLiteral("MediaPanelHeading"));
    root->addWidget(m_playbackLabel);
    root->addSpacing(14);

    m_triggerLabel = new QLabel(content);
    m_triggerLabel->setObjectName(QStringLiteral("MediaPanelFieldLabel"));
    root->addWidget(m_triggerLabel);
    root->addSpacing(8);
    m_trigger = new QComboBox(content);
    m_trigger->setObjectName(QStringLiteral("MediaPanelCombo"));
    m_trigger->setFixedHeight(34);
    m_trigger->setCursor(Qt::PointingHandCursor);
    m_trigger->addItem(QString());
    m_trigger->addItem(QString());
    connect(m_trigger, &QComboBox::currentIndexChanged,
            this, &RichMediaPanel::updateApplyEnabled);
    root->addWidget(m_trigger);
    root->addSpacing(16);

    auto *playbackGrid = new QGridLayout;
    playbackGrid->setContentsMargins(0, 0, 0, 0);
    playbackGrid->setHorizontalSpacing(12);
    playbackGrid->setVerticalSpacing(12);
    addSwitchRow(playbackGrid, 0, content, &m_autoPlayText, &m_autoPlay);
    addSwitchRow(playbackGrid, 1, content, &m_loopText, &m_loop);
    addSwitchRow(playbackGrid, 2, content, &m_mutedText, &m_muted);
    addSwitchRow(playbackGrid, 3, content, &m_controlsText, &m_controls);
    m_controls->setChecked(true);
    playbackGrid->setColumnStretch(0, 1);
    root->addLayout(playbackGrid);
    for (QCheckBox *toggle : { m_autoPlay, m_loop, m_muted, m_controls })
        connect(toggle, &QCheckBox::toggled, this, &RichMediaPanel::updateApplyEnabled);

    root->addStretch(1);

    auto *footer = new QWidget(this);
    footer->setObjectName(QStringLiteral("MediaPanelFooter"));
    auto *footerLayout = new QVBoxLayout(footer);
    footerLayout->setContentsMargins(16, 12, 16, 16);
    m_apply = new QPushButton(footer);
    m_apply->setObjectName(QStringLiteral("MediaPanelApply"));
    m_apply->setFixedHeight(36);
    m_apply->setCursor(Qt::PointingHandCursor);
    connect(m_apply, &QPushButton::clicked, this, [this]() {
        const MediaSpec current = spec();
        if (m_editing && current.isValid()) Q_EMIT applyRequested(current);
    });
    footerLayout->addWidget(m_apply);
    outer->addWidget(footer);

    refreshPoster();
}

void RichMediaPanel::applyStyle()
{
    if (m_stylingNow) return;
    m_stylingNow = true;

    const bool dark = Theme::DarkMode;

    struct Palette {
        const char *panel, *text, *muted, *line, *field, *fieldLine,
                   *accent, *accentSoft, *primaryText, *sunken, *danger;
    };
    const Palette light {
        "#FFFFFF", "#0F172A", "#64748B", "#E5EAF1", "#FFFFFF", "#CBD5E1",
        "#2563EB", "#EFF4FF", "#FFFFFF", "#F1F5F9", "#DC2626"
    };
    const Palette night {
        "#353535", "#E5E7EB", "#9CA3AF", "#484848", "#2C2C2C", "#565656",
        "#2563EB", "#24303F", "#FFFFFF", "#2C2C2C", "#DC2626"
    };
    const Palette &p = dark ? night : light;

    setStyleSheet(QStringLiteral(R"(
QFrame#MediaPanel, QWidget#MediaPanelContent, QScrollArea#MediaPanelScroll,
QWidget#MediaPanelFooter {
    background: %1; border: none;
}
QFrame#MediaPanel { border-left: 1px solid %4; }
QFrame#MediaPanel QLabel { color: %2; font-size: 13px; }
QFrame#MediaPanel QLabel#MediaPanelTitle { font-size: 15px; font-weight: 700; }
QFrame#MediaPanel QLabel#MediaPanelSubtitle { color: %3; font-size: 12px; }
QFrame#MediaPanel QLabel#MediaPanelSection { font-weight: 600; }
QFrame#MediaPanel QLabel#MediaPanelHeading { font-size: 17px; font-weight: 600; }
QFrame#MediaPanel QLabel#MediaPanelFieldLabel { color: %3; }
QFrame#MediaPanelDivider { color: %4; background: %4; max-height: 1px;
    min-height: 1px; border: none; }
QLabel#MediaPanelPoster {
    background: %10; border: 1px solid %4; border-radius: 8px;
}
QComboBox#MediaPanelCombo {
    background: %5; color: %2; border: 1px solid %6; border-radius: 6px;
    padding: 0 10px; font-size: 13px;
}
QComboBox#MediaPanelCombo:focus { border-color: %7; }
QComboBox#MediaPanelCombo::drop-down { border: none; width: 30px; }
QComboBox#MediaPanelCombo::down-arrow { image: url(:/icons/chevron-down.svg);
    width: 12px; height: 12px; }
QPushButton#MediaPanelPrimary, QPushButton#MediaPanelDanger {
    color: %9; border: none; border-radius: 6px;
    padding: 0 12px; font-size: 13px; font-weight: 600;
}
QPushButton#MediaPanelPrimary { background: %7; }
QPushButton#MediaPanelPrimary:hover { background: #1D4ED8; }
QPushButton#MediaPanelDanger { background: %11; }
QPushButton#MediaPanelDanger:hover { background: #B91C1C; }
QPushButton#MediaPanelDanger:disabled { background: rgba(220, 38, 38, 0.4);
    color: rgba(255, 255, 255, 0.75); }
QPushButton#MediaPanelApply {
    background: %7; color: %9; border: none; border-radius: 6px;
    font-size: 13px; font-weight: 600;
}
QPushButton#MediaPanelApply:hover { background: #1D4ED8; }
QPushButton#MediaPanelApply:disabled { background: %4; color: %3; }
QPushButton#MediaPanelClose {
    background: transparent; border: none; border-radius: 5px; padding: 2px;
}
QPushButton#MediaPanelClose:hover { background: %8; }
)").arg(QLatin1String(p.panel), QLatin1String(p.text), QLatin1String(p.muted),
        QLatin1String(p.line), QLatin1String(p.field), QLatin1String(p.fieldLine),
        QLatin1String(p.accent), QLatin1String(p.accentSoft),
        QLatin1String(p.primaryText), QLatin1String(p.sunken),
        QLatin1String(p.danger)));

    const QColor white(Qt::white);
    if (m_icon)
        m_icon->setPixmap(Theme::renderSvg(QStringLiteral("film"), Theme::IconNormal,
                                           24, devicePixelRatioF()));
    if (m_posterChange)
        m_posterChange->setIcon(Theme::makeIcon(QStringLiteral("image"), white, white, white));
    if (m_posterReset)
        m_posterReset->setIcon(Theme::makeIcon(QStringLiteral("rotate-ccw"), white, white,
                                               QColor(255, 255, 255, 190)));
    if (m_close)
        m_close->setIcon(Theme::makeIcon(QStringLiteral("x"), Theme::IconMuted));

    m_stylingNow = false;
}

void RichMediaPanel::changeEvent(QEvent *event)
{
    if (event->type() == QEvent::StyleChange || event->type() == QEvent::PaletteChange)
        applyStyle();
    else if (event->type() == QEvent::LanguageChange)
        retranslateUi();
    QFrame::changeEvent(event);
}

void RichMediaPanel::choosePoster()
{
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Choose a poster image"), QString(),
        tr("Images (*.png *.jpg *.jpeg *.webp *.bmp);;All files (*)"));
    if (path.isEmpty()) return;
    QImage image(path);
    if (image.isNull()) return;
    m_poster = image;
    m_posterFromUser = true;
    refreshPoster();
}

void RichMediaPanel::refreshPoster()
{
    if (!m_posterFromUser) {
        m_poster = QImage();
        const QString source = m_sourcePath;
        if (m_type == MediaSpec::Type::Video && !source.isEmpty()
            && QFileInfo::exists(source))
            m_poster = PosterFrame::grab(source, 640);
    }

    QImage shown = m_poster.isNull()
        ? PosterFrame::placeholder(m_posterView->size() * 2,
                                   Theme::DarkMode ? QColor(0x2C, 0x2C, 0x2C) : QColor())
        : m_poster;
    m_posterView->setPixmap(QPixmap::fromImage(shown).scaled(
        m_posterView->size() - QSize(2, 2),
        Qt::KeepAspectRatio, Qt::SmoothTransformation));
    m_posterReset->setEnabled(m_posterFromUser);
    updateApplyEnabled();
}

void RichMediaPanel::setPlacement(int page, const QRectF &pdfBounds)
{
    m_page   = page;
    m_bounds = pdfBounds;
}

void RichMediaPanel::editMedia(const MediaSpec &media)
{
    m_editing = false;
    m_type = media.type;
    m_sourcePath = media.source;
    m_sourceDisplayName = media.displayName.isEmpty()
        ? QFileInfo(media.source).fileName() : media.displayName;
    m_poster = media.poster;
    m_posterFromUser = !media.poster.isNull();
    m_trigger->setCurrentIndex(media.activateOnPageOpen ? 1 : 0);
    m_autoPlay->setChecked(media.autoPlay);
    m_loop->setChecked(media.loop);
    m_muted->setChecked(media.muted);
    m_controls->setChecked(media.showControls);
    m_floating = media.floating;
    setPlacement(media.page, media.bounds);
    refreshPoster();
    m_loaded = spec();
    m_editing = true;
    updateApplyEnabled();
}

void RichMediaPanel::resetForInsert()
{
    m_editing = false;
    m_type = MediaSpec::Type::Video;
    m_sourcePath.clear();
    m_sourceDisplayName.clear();
    m_poster = QImage();
    m_posterFromUser = false;
    m_trigger->setCurrentIndex(0);
    m_autoPlay->setChecked(false);
    m_loop->setChecked(false);
    m_muted->setChecked(false);
    m_controls->setChecked(true);
    m_floating = false;
    setPlacement(-1, QRectF());
    refreshPoster();
}

bool RichMediaPanel::changed() const
{
    const MediaSpec now = spec();
    return now.activateOnPageOpen != m_loaded.activateOnPageOpen
        || now.autoPlay != m_loaded.autoPlay
        || now.loop != m_loaded.loop
        || now.muted != m_loaded.muted
        || now.showControls != m_loaded.showControls
        || now.poster.cacheKey() != m_loaded.poster.cacheKey();
}

void RichMediaPanel::updateApplyEnabled()
{
    if (m_apply) m_apply->setEnabled(m_editing && spec().isValid() && changed());
}

MediaSpec RichMediaPanel::spec() const
{
    MediaSpec out;
    out.type   = m_type;
    out.source = m_sourcePath;
    out.displayName = m_sourceDisplayName.isEmpty()
        ? QFileInfo(out.source).fileName() : m_sourceDisplayName;
    out.mimeType = out.source.isEmpty() ? QString()
                                         : MediaDrop::mimeTypeFor(out.source);
    out.poster       = m_poster;
    out.activateOnPageOpen = m_trigger && m_trigger->currentIndex() == 1;
    out.autoPlay     = m_autoPlay && m_autoPlay->isChecked();
    out.muted        = m_muted && m_muted->isChecked();
    out.loop         = m_loop && m_loop->isChecked();
    out.showControls = m_controls && m_controls->isChecked();
    out.floating     = m_floating;
    out.page         = m_page;
    out.bounds       = m_bounds;
    return out;
}

void RichMediaPanel::retranslateUi()
{
    m_title->setText(tr("Rich Media"));
    m_subtitle->setText(tr("Embed video and audio"));
    m_posterLabel->setText(tr("Poster / Thumbnail"));
    m_posterChange->setText(QLatin1Char(' ') + tr("Change image"));
    m_posterReset->setText(QLatin1Char(' ') + tr("Reset"));
    m_playbackLabel->setText(tr("Start & playback"));
    m_triggerLabel->setText(tr("Activation"));
    m_trigger->setItemText(0, tr("On click"));
    m_trigger->setItemText(1, tr("On page open"));
    m_autoPlayText->setText(tr("Autoplay"));
    m_loopText->setText(tr("Loop"));
    m_mutedText->setText(tr("Muted"));
    m_controlsText->setText(tr("Show controls"));
    m_apply->setText(tr("Update"));
    m_close->setToolTip(tr("Close"));
}
