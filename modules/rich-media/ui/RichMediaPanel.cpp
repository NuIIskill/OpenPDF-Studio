// SPDX-License-Identifier: LicenseRef-OpenPDF-Business
#include "rich-media/ui/RichMediaPanel.hpp"

#include "rich-media/engine/PosterFrame.hpp"
#include "ui/theme/Theme.hpp"

#include <QButtonGroup>
#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMimeDatabase>
#include <QPushButton>
#include <QRadioButton>
#include <QScrollArea>
#include <QTimer>
#include <QVBoxLayout>

namespace {

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

QDoubleSpinBox *ptSpin(QWidget *parent)
{
    auto *box = new QDoubleSpinBox(parent);
    box->setRange(0.0, 100000.0);
    box->setDecimals(0);
    box->setButtonSymbols(QAbstractSpinBox::NoButtons);
    box->setFixedHeight(30);
    box->setMinimumWidth(1);
    box->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    box->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    return box;
}

/// The MIME type the document should claim. What the system knows first, the
/// suffix otherwise; a viewer picks its decoder by this.
QString mimeFor(const QString &path)
{
    static QMimeDatabase db;
    const QString name = db.mimeTypeForFile(path).name();
    if (!name.isEmpty() && name != QLatin1String("application/octet-stream"))
        return name;
    const QString suffix = QFileInfo(path).suffix().toLower();
    if (suffix == QLatin1String("mp4") || suffix == QLatin1String("m4v"))
        return QStringLiteral("video/mp4");
    if (suffix == QLatin1String("webm")) return QStringLiteral("video/webm");
    if (suffix == QLatin1String("mov"))  return QStringLiteral("video/quicktime");
    if (suffix == QLatin1String("mp3"))  return QStringLiteral("audio/mpeg");
    return QStringLiteral("application/octet-stream");
}

} // namespace

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

// ── Building ─────────────────────────────────────────────────────────────────

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

    m_title = new QLabel(tr("Rich Media"), content);
    m_title->setObjectName(QStringLiteral("MediaPanelTitle"));
    root->addWidget(m_title);
    root->addSpacing(4);

    // ── Kind ─────────────────────────────────────────────────────────────────
    m_typeLabel = sectionLabel(tr("Insert type"), content);
    root->addWidget(m_typeLabel);

    auto *typeRow = new QHBoxLayout;
    typeRow->setSpacing(0);
    struct TypeDef { MediaSpec::Type type; const char *label; bool ready; };
    const TypeDef types[] = {
        { MediaSpec::Type::Video,    QT_TR_NOOP("Video"),     true  },
        { MediaSpec::Type::Audio,    QT_TR_NOOP("Audio"),     true  },
        { MediaSpec::Type::WebEmbed, QT_TR_NOOP("Web Embed"), false },
        { MediaSpec::Type::Button,   QT_TR_NOOP("Button"),    false },
    };
    auto *group = new QButtonGroup(this);
    group->setExclusive(true);
    int index = 0;
    for (const TypeDef &def : types) {
        auto *button = new QPushButton(tr(def.label), content);
        button->setCheckable(true);
        button->setCursor(Qt::PointingHandCursor);
        // Four labels in about 300 pixels: the buttons must be allowed below
        // their preferred width, or the longest one pushes the row off.
        button->setMinimumWidth(1);
        button->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
        button->setProperty("seg", index == 0 ? "first"
                                 : (index == 3 ? "last" : "mid"));
        button->setChecked(def.type == MediaSpec::Type::Video);
        // Web Embed and Button write structures other than an embedded file
        // and are not built yet. Visible, but not pretending to work.
        button->setEnabled(def.ready);
        if (!def.ready)
            button->setToolTip(tr("Not available yet. Embed a file instead."));
        group->addButton(button);
        typeRow->addWidget(button, 1);
        m_typeButtons.append(button);
        const MediaSpec::Type type = def.type;
        connect(button, &QPushButton::clicked, this, [this, type]() { setType(type); });
        ++index;
    }
    root->addLayout(typeRow);
    root->addSpacing(6);

    // ── Source ───────────────────────────────────────────────────────────────
    m_sourceLabel = sectionLabel(tr("Source"), content);
    root->addWidget(m_sourceLabel);

    auto *sourceRow = new QHBoxLayout;
    sourceRow->setSpacing(8);
    m_source = new QLineEdit(content);
    m_source->setObjectName(QStringLiteral("MediaPanelSource"));
    m_source->setPlaceholderText(tr("Choose a video file"));
    m_source->setFixedHeight(32);
    m_source->setMinimumWidth(1);
    connect(m_source, &QLineEdit::textChanged, this, [this]() {
        updateInsertEnabled();
        if (m_syncing) return;
        m_posterFromUser = false;
        // The still costs an ffmpeg run on this thread. Typing a path would
        // be one run per keystroke, so wait until the typing stops.
        m_posterDelay->start();
    });

    m_posterDelay = new QTimer(this);
    m_posterDelay->setSingleShot(true);
    m_posterDelay->setInterval(350);
    connect(m_posterDelay, &QTimer::timeout, this, &RichMediaPanel::refreshPoster);
    sourceRow->addWidget(m_source, 1);

    m_browse = new QPushButton(tr("Browse"), content);
    m_browse->setObjectName(QStringLiteral("MediaPanelSecondary"));
    m_browse->setFixedHeight(32);
    m_browse->setFixedWidth(74);
    m_browse->setCursor(Qt::PointingHandCursor);
    connect(m_browse, &QPushButton::clicked, this, &RichMediaPanel::chooseSource);
    sourceRow->addWidget(m_browse);
    root->addLayout(sourceRow);
    root->addSpacing(6);

    // ── Poster ───────────────────────────────────────────────────────────────
    m_posterLabel = sectionLabel(tr("Poster / Thumbnail"), content);
    root->addWidget(m_posterLabel);

    auto *posterRow = new QHBoxLayout;
    posterRow->setSpacing(10);
    m_posterView = new QLabel(content);
    m_posterView->setObjectName(QStringLiteral("MediaPanelPoster"));
    m_posterView->setFixedSize(108, 60);
    m_posterView->setAlignment(Qt::AlignCenter);
    m_posterView->setScaledContents(false);
    posterRow->addWidget(m_posterView);

    m_posterChange = new QPushButton(tr("Change"), content);
    m_posterChange->setObjectName(QStringLiteral("MediaPanelSecondary"));
    m_posterChange->setFixedHeight(30);
    m_posterChange->setFixedWidth(74);
    m_posterChange->setCursor(Qt::PointingHandCursor);
    connect(m_posterChange, &QPushButton::clicked, this, &RichMediaPanel::choosePoster);
    posterRow->addWidget(m_posterChange);

    m_posterRemove = new QPushButton(tr("Remove"), content);
    m_posterRemove->setObjectName(QStringLiteral("MediaPanelSecondary"));
    m_posterRemove->setFixedHeight(30);
    m_posterRemove->setFixedWidth(74);
    m_posterRemove->setCursor(Qt::PointingHandCursor);
    connect(m_posterRemove, &QPushButton::clicked, this, [this]() {
        m_posterFromUser = false;
        m_poster = QImage();
        m_posterDelay->stop();
        refreshPoster();
    });
    posterRow->addWidget(m_posterRemove);
    posterRow->addStretch(1);
    root->addLayout(posterRow);

    root->addSpacing(6);
    root->addWidget(divider(content));

    // ── Trigger ──────────────────────────────────────────────────────────────
    m_triggerLabel = sectionLabel(tr("Trigger"), content);
    root->addWidget(m_triggerLabel);

    auto *triggerRow = new QHBoxLayout;
    triggerRow->setSpacing(18);
    m_onClick    = new QRadioButton(tr("On click"), content);
    m_onPageOpen = new QRadioButton(tr("On page open"), content);
    // One group per question: radios sharing a parent otherwise form a single
    // exclusive set, and "Inline" would switch the trigger off.
    auto *triggerGroup = new QButtonGroup(this);
    triggerGroup->addButton(m_onClick);
    triggerGroup->addButton(m_onPageOpen);
    m_onClick->setChecked(true);
    triggerRow->addWidget(m_onClick);
    triggerRow->addWidget(m_onPageOpen);
    triggerRow->addStretch(1);
    root->addLayout(triggerRow);

    root->addWidget(divider(content));

    // ── Playback ─────────────────────────────────────────────────────────────
    m_playbackLabel = sectionLabel(tr("Playback options"), content);
    root->addWidget(m_playbackLabel);

    auto *playbackGrid = new QGridLayout;
    playbackGrid->setHorizontalSpacing(14);
    playbackGrid->setVerticalSpacing(8);
    m_autoPlay = new QCheckBox(tr("Autoplay"), content);
    m_muted    = new QCheckBox(tr("Muted"), content);
    m_loop     = new QCheckBox(tr("Loop"), content);
    m_controls = new QCheckBox(tr("Show controls"), content);
    m_controls->setChecked(true);
    playbackGrid->addWidget(m_autoPlay, 0, 0);
    playbackGrid->addWidget(m_muted,    0, 1);
    playbackGrid->addWidget(m_loop,     1, 0);
    playbackGrid->addWidget(m_controls, 1, 1);
    playbackGrid->setColumnStretch(0, 1);
    playbackGrid->setColumnStretch(1, 1);
    root->addLayout(playbackGrid);

    root->addWidget(divider(content));

    // ── Placement ────────────────────────────────────────────────────────────
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

    // ── Position and size ────────────────────────────────────────────────────
    m_geometryLabel = sectionLabel(tr("Position & Size"), content);
    root->addWidget(m_geometryLabel);

    auto *geometry = new QGridLayout;
    geometry->setHorizontalSpacing(6);
    geometry->setVerticalSpacing(4);
    const QStringList captions { QStringLiteral("X"), QStringLiteral("Y"),
                                 QStringLiteral("W"), QStringLiteral("H") };
    m_x = ptSpin(content); m_y = ptSpin(content);
    m_w = ptSpin(content); m_h = ptSpin(content);
    QDoubleSpinBox *boxes[] = { m_x, m_y, m_w, m_h };
    for (int i = 0; i < 4; ++i) {
        auto *caption = new QLabel(captions.at(i), content);
        caption->setObjectName(QStringLiteral("MediaPanelFieldLabel"));
        geometry->addWidget(caption, 0, i);
        geometry->addWidget(boxes[i], 1, i);
        geometry->setColumnStretch(i, 1);
        connect(boxes[i], &QDoubleSpinBox::valueChanged,
                this, &RichMediaPanel::pushGeometry);
    }
    m_w->setMinimum(8.0);
    m_h->setMinimum(8.0);

    m_lock = new QPushButton(content);
    m_lock->setObjectName(QStringLiteral("MediaPanelLock"));
    m_lock->setCheckable(true);
    m_lock->setChecked(true);
    m_lock->setFixedSize(28, 30);
    m_lock->setCursor(Qt::PointingHandCursor);
    m_lock->setToolTip(tr("Keep the aspect ratio"));
    m_lock->setIcon(Theme::makeIcon(QStringLiteral("lock"), Theme::IconMuted));
    geometry->addWidget(m_lock, 1, 4);
    root->addLayout(geometry);

    root->addSpacing(10);

    m_hint = new QLabel(tr("Drag a frame on the page to place the media."), content);
    m_hint->setObjectName(QStringLiteral("MediaPanelHint"));
    m_hint->setWordWrap(true);
    root->addWidget(m_hint);

    m_insert = new QPushButton(tr("Insert Rich Media"), content);
    m_insert->setObjectName(QStringLiteral("MediaPanelPrimary"));
    m_insert->setFixedHeight(38);
    m_insert->setCursor(Qt::PointingHandCursor);
    connect(m_insert, &QPushButton::clicked, this, [this]() {
        const MediaSpec current = spec();
        if (current.isValid()) Q_EMIT insertRequested(current);
    });
    root->addWidget(m_insert);

    root->addStretch(1);
    refreshPoster();
}

// ── Look ─────────────────────────────────────────────────────────────────────

void RichMediaPanel::applyStyle()
{
    if (m_stylingNow) return;
    m_stylingNow = true;

    // The module brings its own look rather than writing into the Core's
    // stylesheet, which keeps the Core free of media concerns.
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
QLabel#MediaPanelSection { color: %2; font-size: 12px; font-weight: 600; }
QLabel#MediaPanelFieldLabel { color: %3; font-size: 11px; }
QLabel#MediaPanelHint { color: %3; font-size: 11px; }
QFrame#MediaPanelDivider { color: %4; background: %4; max-height: 1px;
    min-height: 1px; border: none; }
QFrame#MediaPanel QLabel { color: %2; font-size: 12px; }
QFrame#MediaPanel QLineEdit, QFrame#MediaPanel QDoubleSpinBox {
    background: %5; color: %2; border: 1px solid %6; border-radius: 6px;
    padding: 4px 8px; font-size: 12px;
}
QFrame#MediaPanel QLineEdit:focus, QFrame#MediaPanel QDoubleSpinBox:focus {
    border: 1px solid %7;
}
QFrame#MediaPanel QCheckBox, QFrame#MediaPanel QRadioButton {
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
QPushButton#MediaPanelLock {
    background: %5; border: 1px solid %6; border-radius: 6px;
}
QPushButton#MediaPanelLock:checked { border-color: %7; background: %8; }
QFrame#MediaPanel QPushButton[seg] {
    background: %5; color: %3; border: 1px solid %6;
    padding: 6px 2px; font-size: 11px; min-height: 20px;
}
QFrame#MediaPanel QPushButton[seg="first"] {
    border-top-left-radius: 6px; border-bottom-left-radius: 6px;
}
QFrame#MediaPanel QPushButton[seg="last"] {
    border-top-right-radius: 6px; border-bottom-right-radius: 6px;
}
QFrame#MediaPanel QPushButton[seg="mid"] { border-left: none; border-right: none; }
QFrame#MediaPanel QPushButton[seg="last"] { border-left: none; }
QFrame#MediaPanel QPushButton[seg]:checked {
    background: %8; color: %7; border-color: %7; font-weight: 600;
}
QFrame#MediaPanel QPushButton[seg]:disabled { color: %4; }
)").arg(QLatin1String(p.panel), QLatin1String(p.text), QLatin1String(p.muted),
        QLatin1String(p.line), QLatin1String(p.field), QLatin1String(p.fieldLine),
        QLatin1String(p.accent), QLatin1String(p.accentSoft),
        QLatin1String(p.primaryText), QLatin1String(p.sunken)));

    if (m_lock)
        m_lock->setIcon(Theme::makeIcon(QStringLiteral("lock"), Theme::IconMuted));

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

// ── State ────────────────────────────────────────────────────────────────────

void RichMediaPanel::setType(MediaSpec::Type type)
{
    m_type = type;
    const bool audio = (type == MediaSpec::Type::Audio);
    m_source->setPlaceholderText(audio ? tr("Choose an audio file")
                                       : tr("Choose a video file"));
    // Audio has no still, but the box stays: the poster is what the page
    // shows for audio too.
    refreshPoster();
    updateInsertEnabled();
}

void RichMediaPanel::chooseSource()
{
    const bool audio = (m_type == MediaSpec::Type::Audio);
    // MP4 first and named as such: that is what plays everywhere. Anything
    // else stays selectable but is checked before it is inserted.
    const QString filter = audio
        ? tr("MP3 and AAC (*.mp3 *.m4a);;All audio (*.mp3 *.m4a *.wav *.ogg *.opus *.flac);;All files (*)")
        : tr("MP4 video, H.264 (*.mp4 *.m4v);;All video (*.mp4 *.m4v *.mov *.webm *.mkv *.avi);;All files (*)");
    const QString path = QFileDialog::getOpenFileName(
        this, audio ? tr("Choose audio") : tr("Choose video"), QString(), filter);
    if (path.isEmpty()) return;
    m_source->setText(path);
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
        const QString source = m_source ? m_source->text().trimmed() : QString();
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
        m_posterRemove->setEnabled(!m_poster.isNull());
    Q_EMIT previewChanged(shown);
}

void RichMediaPanel::setPlacement(int page, const QRectF &pdfBounds)
{
    m_page   = page;
    m_bounds = pdfBounds;
    m_aspect = pdfBounds.height() > 0.0 ? pdfBounds.width() / pdfBounds.height() : 0.0;

    m_syncing = true;
    m_x->setValue(pdfBounds.x());
    m_y->setValue(pdfBounds.y());
    m_w->setValue(pdfBounds.width());
    m_h->setValue(pdfBounds.height());
    m_syncing = false;

    updateInsertEnabled();
}

void RichMediaPanel::clearPlacement()
{
    m_page = -1;
    m_bounds = QRectF();
    m_syncing = true;
    for (QDoubleSpinBox *box : { m_x, m_y, m_w, m_h }) box->setValue(0.0);
    m_syncing = false;
    updateInsertEnabled();
}

void RichMediaPanel::pushGeometry()
{
    if (m_syncing || m_page < 0) return;

    QRectF bounds(m_x->value(), m_y->value(), m_w->value(), m_h->value());

    // Lock closed: the side the user did not touch follows. The sender says
    // which one that was.
    if (m_lock->isChecked() && m_aspect > 0.0) {
        auto *sender = qobject_cast<QDoubleSpinBox *>(QObject::sender());
        m_syncing = true;
        if (sender == m_w) {
            bounds.setHeight(bounds.width() / m_aspect);
            m_h->setValue(bounds.height());
        } else if (sender == m_h) {
            bounds.setWidth(bounds.height() * m_aspect);
            m_w->setValue(bounds.width());
        }
        m_syncing = false;
    } else if (bounds.height() > 0.0) {
        m_aspect = bounds.width() / bounds.height();
    }

    m_bounds = bounds;
    updateInsertEnabled();
    Q_EMIT placementEdited(bounds);
}

void RichMediaPanel::updateInsertEnabled()
{
    const bool hasSource = m_source && !m_source->text().trimmed().isEmpty()
                        && QFileInfo::exists(m_source->text().trimmed());
    const bool hasPlace  = m_page >= 0 && !m_bounds.isEmpty();
    const bool supported = m_type == MediaSpec::Type::Video
                        || m_type == MediaSpec::Type::Audio;
    if (m_insert) m_insert->setEnabled(hasSource && hasPlace && supported);

    if (!m_hint) return;
    if (!hasPlace)
        m_hint->setText(tr("Drag a frame on the page to place the media."));
    else if (!hasSource)
        m_hint->setText(tr("Choose a file to embed."));
    else
        m_hint->setText(tr("Page %1. The file is copied into the document.")
                            .arg(m_page + 1));
}

MediaSpec RichMediaPanel::spec() const
{
    MediaSpec out;
    out.type   = m_type;
    out.source = m_source ? m_source->text().trimmed() : QString();
    out.displayName = QFileInfo(out.source).fileName();
    out.mimeType    = out.source.isEmpty() ? QString() : mimeFor(out.source);
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
    m_typeLabel->setText(tr("Insert type"));
    m_sourceLabel->setText(tr("Source"));
    m_posterLabel->setText(tr("Poster / Thumbnail"));
    m_triggerLabel->setText(tr("Trigger"));
    m_playbackLabel->setText(tr("Playback options"));
    m_placementLabel->setText(tr("Placement"));
    m_geometryLabel->setText(tr("Position & Size"));

    static const char *kTypeLabels[] = { QT_TR_NOOP("Video"), QT_TR_NOOP("Audio"),
                                         QT_TR_NOOP("Web Embed"), QT_TR_NOOP("Button") };
    for (int i = 0; i < m_typeButtons.size() && i < 4; ++i)
        m_typeButtons.at(i)->setText(tr(kTypeLabels[i]));

    m_source->setPlaceholderText(m_type == MediaSpec::Type::Audio
                                 ? tr("Choose an audio file") : tr("Choose a video file"));
    m_browse->setText(tr("Browse"));
    m_posterChange->setText(tr("Change"));
    m_posterRemove->setText(tr("Remove"));
    m_onClick->setText(tr("On click"));
    m_onPageOpen->setText(tr("On page open"));
    m_autoPlay->setText(tr("Autoplay"));
    m_muted->setText(tr("Muted"));
    m_loop->setText(tr("Loop"));
    m_controls->setText(tr("Show controls"));
    m_inline->setText(tr("Inline"));
    m_floating->setText(tr("Floating"));
    m_insert->setText(tr("Insert Rich Media"));
    m_lock->setToolTip(tr("Keep the aspect ratio"));
    updateInsertEnabled();
}
