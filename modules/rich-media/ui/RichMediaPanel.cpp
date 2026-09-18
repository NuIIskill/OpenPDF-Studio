// SPDX-License-Identifier: LicenseRef-OpenPDF-Business
#include "rich-media/ui/RichMediaPanel.hpp"

#include "rich-media/engine/MediaDrop.hpp"
#include "rich-media/engine/PosterFrame.hpp"
#include "ui/theme/Theme.hpp"

#include <QButtonGroup>
#include <QCheckBox>
#include <QEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QPushButton>
#include <QRadioButton>
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

QLabel *sectionLabel(const QString &text, QWidget *parent)
{
    auto *label = new QLabel(text, parent);
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

void addSwitchRow(QGridLayout *layout, int row, const QString &text,
                  QWidget *parent, QLabel **label, QCheckBox **toggle)
{
    *label = new QLabel(text, parent);
    *toggle = new Switch(parent);
    (*toggle)->setObjectName(QStringLiteral("MediaPanelSwitch"));
    (*toggle)->setFixedSize(32, 20);
    (*toggle)->setCursor(Qt::PointingHandCursor);
    layout->addWidget(*label, row, 0);
    layout->addWidget(*toggle, row, 1, Qt::AlignRight | Qt::AlignVCenter);
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
    applyStyle();
    updateInsertEnabled();
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
    root->setContentsMargins(16, 16, 16, 18);
    root->setSpacing(10);

    auto *header = new QHBoxLayout;
    header->setContentsMargins(0, 0, 0, 0);
    header->setSpacing(8);
    m_title = new QLabel(tr("Rich Media"), content);
    m_title->setObjectName(QStringLiteral("MediaPanelTitle"));
    header->addWidget(m_title);
    header->addStretch(1);
    m_close = new QPushButton(content);
    m_close->setObjectName(QStringLiteral("MediaPanelClose"));
    m_close->setFixedSize(24, 24);
    m_close->setCursor(Qt::PointingHandCursor);
    m_close->setIcon(Theme::makeIcon(QStringLiteral("x"), Theme::IconMuted));
    m_close->setToolTip(tr("Close"));
    connect(m_close, &QPushButton::clicked, this,
            [this]() { Q_EMIT closeRequested(); });
    header->addWidget(m_close);
    root->addLayout(header);
    m_subtitle = new QLabel(tr("Edit media or add a new source"), content);
    m_subtitle->setObjectName(QStringLiteral("MediaPanelSubtitle"));
    root->addWidget(m_subtitle);
    root->addSpacing(12);

    m_sourceLabel = sectionLabel(tr("Source"), content);
    root->addWidget(m_sourceLabel);

    auto *sourceRow = new QHBoxLayout;
    sourceRow->setSpacing(8);
    m_source = new QLineEdit(content);
    m_source->setObjectName(QStringLiteral("MediaPanelSource"));
    m_source->setPlaceholderText(tr("Choose a media file"));
    m_source->setReadOnly(true);
    m_source->setFixedHeight(32);
    m_source->setMinimumWidth(1);
    sourceRow->addWidget(m_source, 1);

    m_browse = new QPushButton(tr("Browse"), content);
    m_browse->setObjectName(QStringLiteral("MediaPanelSecondary"));
    m_browse->setFixedHeight(32);
    m_browse->setFixedWidth(88);
    m_browse->setCursor(Qt::PointingHandCursor);
    m_browse->setIcon(Theme::makeIcon(QStringLiteral("folder-open"), Theme::IconMuted));
    connect(m_browse, &QPushButton::clicked, this, &RichMediaPanel::chooseSource);
    sourceRow->addWidget(m_browse);
    root->addLayout(sourceRow);
    root->addSpacing(6);

    m_posterLabel = sectionLabel(tr("Poster / Thumbnail"), content);
    root->addWidget(m_posterLabel);

    auto *posterRow = new QHBoxLayout;
    posterRow->setSpacing(10);
    m_posterView = new QLabel(content);
    m_posterView->setObjectName(QStringLiteral("MediaPanelPoster"));
    m_posterView->setFixedSize(174, 86);
    m_posterView->setAlignment(Qt::AlignCenter);
    m_posterView->setScaledContents(false);
    posterRow->addWidget(m_posterView);

    auto *posterActions = new QVBoxLayout;
    posterActions->setContentsMargins(0, 0, 0, 0);
    posterActions->setSpacing(8);

    m_posterChange = new QPushButton(tr("Change"), content);
    m_posterChange->setObjectName(QStringLiteral("MediaPanelSecondary"));
    m_posterChange->setFixedHeight(34);
    m_posterChange->setCursor(Qt::PointingHandCursor);
    m_posterChange->setIcon(Theme::makeIcon(QStringLiteral("image"), Theme::IconMuted));
    connect(m_posterChange, &QPushButton::clicked, this, &RichMediaPanel::choosePoster);
    posterActions->addWidget(m_posterChange);

    m_posterRemove = new QPushButton(tr("Remove"), content);
    m_posterRemove->setObjectName(QStringLiteral("MediaPanelSecondary"));
    m_posterRemove->setFixedHeight(34);
    m_posterRemove->setCursor(Qt::PointingHandCursor);
    m_posterRemove->setIcon(Theme::makeIcon(QStringLiteral("trash-2"), Theme::IconMuted));
    connect(m_posterRemove, &QPushButton::clicked, this, [this]() {
        m_posterFromUser = false;
        m_poster = QImage();
        refreshPoster();
    });
    posterActions->addWidget(m_posterRemove);
    posterActions->addStretch(1);
    posterRow->addLayout(posterActions, 1);
    root->addLayout(posterRow);

    root->addSpacing(6);
    root->addWidget(divider(content));

    m_triggerLabel = sectionLabel(tr("Trigger"), content);
    root->addWidget(m_triggerLabel);

    auto *triggerRow = new QHBoxLayout;
    triggerRow->setSpacing(18);
    m_onClick    = new QRadioButton(tr("On click"), content);
    m_onPageOpen = new QRadioButton(tr("On page open"), content);

    auto *triggerGroup = new QButtonGroup(this);
    triggerGroup->addButton(m_onClick);
    triggerGroup->addButton(m_onPageOpen);
    m_onClick->setChecked(true);
    triggerRow->addWidget(m_onClick);
    triggerRow->addWidget(m_onPageOpen);
    triggerRow->addStretch(1);
    root->addLayout(triggerRow);

    root->addWidget(divider(content));

    m_playbackLabel = sectionLabel(tr("Playback options"), content);
    root->addWidget(m_playbackLabel);

    auto *playbackGrid = new QGridLayout;
    playbackGrid->setContentsMargins(0, 0, 0, 0);
    playbackGrid->setHorizontalSpacing(12);
    playbackGrid->setVerticalSpacing(8);
    addSwitchRow(playbackGrid, 0, tr("Autoplay"), content,
                 &m_autoPlayText, &m_autoPlay);
    addSwitchRow(playbackGrid, 1, tr("Loop"), content,
                 &m_loopText, &m_loop);
    addSwitchRow(playbackGrid, 2, tr("Muted"), content,
                 &m_mutedText, &m_muted);
    addSwitchRow(playbackGrid, 3, tr("Show controls"), content,
                 &m_controlsText, &m_controls);
    m_controls->setChecked(true);
    playbackGrid->setColumnStretch(0, 1);
    root->addLayout(playbackGrid);

    root->addWidget(divider(content));

    m_placementLabel = sectionLabel(tr("Placement"), content);
    root->addWidget(m_placementLabel);

    auto *placementRow = new QHBoxLayout;
    placementRow->setSpacing(18);
    m_inline   = new QRadioButton(tr("Inline"), content);
    m_floating = new QRadioButton(tr("Floating"), content);
    auto *placementGroup = new QButtonGroup(this);
    placementGroup->addButton(m_inline);
    placementGroup->addButton(m_floating);
    m_inline->setChecked(true);
    placementRow->addWidget(m_inline);
    placementRow->addWidget(m_floating);
    placementRow->addStretch(1);
    root->addLayout(placementRow);

    root->addWidget(divider(content));

    root->addSpacing(10);

    m_hint = new QLabel(tr("Drag a frame on the page to place the media."), content);
    m_hint->setObjectName(QStringLiteral("MediaPanelHint"));
    m_hint->setWordWrap(true);
    root->addWidget(m_hint);

    m_insert = new QPushButton(tr("Apply"), content);
    m_insert->setObjectName(QStringLiteral("MediaPanelPrimary"));
    m_insert->setFixedHeight(38);
    m_insert->setCursor(Qt::PointingHandCursor);
    connect(m_insert, &QPushButton::clicked, this, [this]() {
        const MediaSpec current = spec();
        if (current.isValid()) Q_EMIT applyRequested(current);
    });
    root->addWidget(m_insert);

    root->addStretch(1);
    refreshPoster();
}

void RichMediaPanel::applyStyle()
{
    if (m_stylingNow) return;
    m_stylingNow = true;

    const bool dark = Theme::DarkMode;

    struct Palette {
        const char *panel, *text, *muted, *line, *field, *fieldLine,
                   *accent, *accentSoft, *primaryText, *sunken;
    };
    const Palette light {
        "#FFFFFF", "#0F172A", "#64748B", "#E5EAF1", "#FFFFFF", "#CBD5E1",
        "#2563EB", "#EFF4FF", "#FFFFFF", "#F1F5F9"
    };
    const Palette night {
        "#353535", "#E5E7EB", "#9CA3AF", "#484848", "#2C2C2C", "#565656",
        "#3B82F6", "#24303F", "#FFFFFF", "#2C2C2C"
    };
    const Palette &p = dark ? night : light;

    setStyleSheet(QStringLiteral(R"(
QFrame#MediaPanel, QWidget#MediaPanelContent, QScrollArea#MediaPanelScroll {
    background: %1; border: none;
}
QFrame#MediaPanel { border-left: 1px solid %4; }
QLabel#MediaPanelTitle { color: %2; font-size: 16px; font-weight: 700; }
QLabel#MediaPanelSubtitle { color: %3; font-size: 12px; }
QLabel#MediaPanelSection { color: %2; font-size: 12px; font-weight: 600; }
QLabel#MediaPanelFieldLabel { color: %3; font-size: 11px; }
QLabel#MediaPanelHint { color: %3; font-size: 11px; }
QFrame#MediaPanelDivider { color: %4; background: %4; max-height: 1px;
    min-height: 1px; border: none; }
QFrame#MediaPanel QLabel { color: %2; font-size: 12px; }
QFrame#MediaPanel QLineEdit {
    background: %5; color: %2; border: 1px solid %6; border-radius: 6px;
    padding: 4px 8px; font-size: 12px;
}
QFrame#MediaPanel QLineEdit:focus {
    border: 1px solid %7;
}
QFrame#MediaPanel QRadioButton {
    color: %2; font-size: 12px; spacing: 7px;
}
QLabel#MediaPanelPoster {
    background: %10; border: 1px solid %6; border-radius: 6px;
}
QPushButton#MediaPanelSecondary {
    background: %5; color: %2; border: 1px solid %6; border-radius: 6px;
    padding: 0 12px; font-size: 12px;
}
QPushButton#MediaPanelSecondary:hover { border-color: %7; color: %7; }
QPushButton#MediaPanelSecondary:disabled { color: %3; border-color: %4; }
QPushButton#MediaPanelPrimary {
    background: %7; color: %9; border: none; border-radius: 7px;
    font-size: 13px; font-weight: 600;
}
QPushButton#MediaPanelPrimary:hover { background: %7; }
QPushButton#MediaPanelPrimary:disabled { background: %4; color: %3; }
QPushButton#MediaPanelClose {
    background: transparent; border: none; border-radius: 5px; padding: 2px;
}
QPushButton#MediaPanelClose:hover { background: %8; }
)").arg(QLatin1String(p.panel), QLatin1String(p.text), QLatin1String(p.muted),
        QLatin1String(p.line), QLatin1String(p.field), QLatin1String(p.fieldLine),
        QLatin1String(p.accent), QLatin1String(p.accentSoft),
        QLatin1String(p.primaryText), QLatin1String(p.sunken)));

    if (m_browse)
        m_browse->setIcon(Theme::makeIcon(QStringLiteral("folder-open"), Theme::IconMuted));
    if (m_posterChange)
        m_posterChange->setIcon(Theme::makeIcon(QStringLiteral("image"), Theme::IconMuted));
    if (m_posterRemove)
        m_posterRemove->setIcon(Theme::makeIcon(QStringLiteral("trash-2"), Theme::IconMuted));
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

void RichMediaPanel::setSource(const QString &path, const QString &displayName)
{
    m_sourcePath = path;
    m_sourceDisplayName = displayName.isEmpty() ? QFileInfo(path).fileName()
                                                : displayName;
    const QString mime = path.isEmpty() ? QString() : MediaDrop::mimeTypeFor(path);
    m_type = mime.startsWith(QLatin1String("audio/")) ? MediaSpec::Type::Audio
                                                      : MediaSpec::Type::Video;
    if (m_source) m_source->setText(m_sourceDisplayName);
    m_posterFromUser = false;
    refreshPoster();
    updateInsertEnabled();
}

void RichMediaPanel::chooseSource()
{
    const QString filter =
        tr("Media files (*.mp4 *.m4v *.mov *.webm *.mkv *.avi *.mp3 *.m4a *.wav *.ogg *.opus *.flac);;All files (*)");
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Choose media"), QString(), filter);
    if (path.isEmpty()) return;
    setSource(path);
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
        ? PosterFrame::placeholder(QSize(216, 120))
        : m_poster;
    if (m_posterView) {
        m_posterView->setPixmap(QPixmap::fromImage(shown).scaled(
            m_posterView->size() - QSize(2, 2),
            Qt::KeepAspectRatio, Qt::SmoothTransformation));
    }
    if (m_posterRemove)
        m_posterRemove->setEnabled(m_posterFromUser);
    Q_EMIT previewChanged(shown);
}

void RichMediaPanel::setPlacement(int page, const QRectF &pdfBounds)
{
    m_page   = page;
    m_bounds = pdfBounds;
    updateInsertEnabled();
}

void RichMediaPanel::clearPlacement()
{
    m_page = -1;
    m_bounds = QRectF();
    updateInsertEnabled();
}

void RichMediaPanel::editMedia(const MediaSpec &media)
{
    m_editing = true;
    m_type = media.type;
    m_sourcePath = media.source;
    m_sourceDisplayName = media.displayName.isEmpty()
        ? QFileInfo(media.source).fileName() : media.displayName;
    m_source->setText(m_sourceDisplayName);
    m_poster = media.poster;
    m_posterFromUser = !media.poster.isNull();
    m_onPageOpen->setChecked(media.activateOnPageOpen);
    m_onClick->setChecked(!media.activateOnPageOpen);
    m_autoPlay->setChecked(media.autoPlay);
    m_loop->setChecked(media.loop);
    m_muted->setChecked(media.muted);
    m_controls->setChecked(media.showControls);
    m_floating->setChecked(media.floating);
    m_inline->setChecked(!media.floating);
    setPlacement(media.page, media.bounds);
    refreshPoster();
}

void RichMediaPanel::resetForInsert()
{
    m_editing = false;
    m_type = MediaSpec::Type::Video;
    m_sourcePath.clear();
    m_sourceDisplayName.clear();
    m_source->clear();
    m_poster = QImage();
    m_posterFromUser = false;
    m_onClick->setChecked(true);
    m_autoPlay->setChecked(false);
    m_loop->setChecked(false);
    m_muted->setChecked(false);
    m_controls->setChecked(true);
    m_inline->setChecked(true);
    clearPlacement();
    refreshPoster();
}

void RichMediaPanel::updateInsertEnabled()
{
    const bool hasSource = !m_sourcePath.isEmpty() && QFileInfo::exists(m_sourcePath);
    const bool hasPlace  = m_page >= 0 && !m_bounds.isEmpty();
    const bool supported = m_type == MediaSpec::Type::Video
                        || m_type == MediaSpec::Type::Audio;
    if (m_insert) m_insert->setEnabled(hasSource && hasPlace && supported);

    if (!m_hint) return;
    if (!hasPlace)
        m_hint->setText(tr("Drag a frame on the page to place the media."));
    else if (!hasSource)
        m_hint->setText(tr("Choose a file to embed."));
    else if (m_editing)
        m_hint->setText(tr("Drag the handles on the page to resize the media."));
    else
        m_hint->setText(tr("Page %1. The file is copied into the document.")
                            .arg(m_page + 1));
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
    out.poster      = m_poster;
    out.activateOnPageOpen = m_onPageOpen && m_onPageOpen->isChecked();
    out.autoPlay     = m_autoPlay && m_autoPlay->isChecked();
    out.muted        = m_muted && m_muted->isChecked();
    out.loop         = m_loop && m_loop->isChecked();
    out.showControls = m_controls && m_controls->isChecked();
    out.floating     = m_floating && m_floating->isChecked();
    out.page         = m_page;
    out.bounds       = m_bounds;
    return out;
}

void RichMediaPanel::retranslateUi()
{
    m_title->setText(tr("Rich Media"));
    m_subtitle->setText(tr("Edit media or add a new source"));
    m_sourceLabel->setText(tr("Source"));
    m_posterLabel->setText(tr("Poster / Thumbnail"));
    m_triggerLabel->setText(tr("Trigger"));
    m_playbackLabel->setText(tr("Playback options"));
    m_placementLabel->setText(tr("Placement"));

    m_source->setPlaceholderText(tr("Choose a media file"));
    m_browse->setText(tr("Browse"));
    m_posterChange->setText(tr("Change"));
    m_posterRemove->setText(tr("Remove"));
    m_onClick->setText(tr("On click"));
    m_onPageOpen->setText(tr("On page open"));
    m_autoPlayText->setText(tr("Autoplay"));
    m_mutedText->setText(tr("Muted"));
    m_loopText->setText(tr("Loop"));
    m_controlsText->setText(tr("Show controls"));
    m_inline->setText(tr("Inline"));
    m_floating->setText(tr("Floating"));
    m_insert->setText(tr("Apply"));
    m_close->setToolTip(tr("Close"));
    updateInsertEnabled();
}
