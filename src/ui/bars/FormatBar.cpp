#include "ui/bars/FormatBar.hpp"

#include <QButtonGroup>
#include <QColorDialog>
#include <QFontComboBox>
#include <QLineEdit>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QLabel>
#include <QComboBox>
#include <QPushButton>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QIcon>
#include <QMenu>

// ── Icon painters ─────────────────────────────────────────────────────────────

// Draw horizontal text-line bars representing a text-alignment icon.
// widths: fraction of available width per line (0..1). xOffset: left indent (0..1).
static QIcon makeAlignIcon(std::initializer_list<float> widths,
                           std::initializer_list<float> xOffsets,
                           const QColor &color = QColor(0x37, 0x41, 0x51))
{
    const int S = 16, pad = 2, barH = 2, gap = 3;
    QPixmap px(S, S);
    px.fill(Qt::transparent);
    QPainter p(&px);
    p.setRenderHint(QPainter::Antialiasing, false);
    p.setBrush(color);
    p.setPen(Qt::NoPen);

    const int inner = S - 2 * pad;
    int y = pad;
    auto wIt = widths.begin();
    auto xIt = xOffsets.begin();
    while (wIt != widths.end() && y + barH <= S - pad) {
        const int w = qRound(*wIt * inner);
        const int x = pad + qRound(*xIt * (inner - w));
        p.drawRoundedRect(x, y, w, barH, 1, 1);
        y += barH + gap;
        ++wIt; ++xIt;
    }
    return QIcon(px);
}

static QIcon makeListIcon(bool ordered, const QColor &color = QColor(0x37, 0x41, 0x51))
{
    const int S = 16, pad = 2;
    QPixmap px(S, S);
    px.fill(Qt::transparent);
    QPainter p(&px);
    p.setRenderHint(QPainter::Antialiasing);
    p.setBrush(color);
    p.setPen(Qt::NoPen);

    const int lineH = 2, gap = 4, dotSz = 3;
    const int lineX = pad + dotSz + 3;
    int y = pad;
    for (int i = 0; i < 3 && y + lineH <= S - pad; ++i) {
        if (ordered) {
            p.setFont(QFont(QStringLiteral("sans-serif"), 5));
            p.setPen(color);
            p.drawText(pad, y + lineH + 1, QString::number(i+1));
            p.setPen(Qt::NoPen);
        } else {
            p.drawEllipse(pad, y, dotSz, dotSz);
        }
        p.drawRoundedRect(lineX, y, S - pad - lineX, lineH, 1, 1);
        y += lineH + gap;
    }
    return QIcon(px);
}

static QIcon makeIndentIcon(const QColor &color = QColor(0x37, 0x41, 0x51))
{
    const int S = 16, pad = 2, barH = 2, gap = 3;
    QPixmap px(S, S);
    px.fill(Qt::transparent);
    QPainter p(&px);
    p.setBrush(color);
    p.setPen(Qt::NoPen);

    // Arrow right then 3 indented lines
    // Arrow: small triangle pointing right
    QPolygon arrow;
    arrow << QPoint(pad, pad+1) << QPoint(pad+4, pad+4) << QPoint(pad, pad+7);
    p.drawPolygon(arrow);

    int y = pad;
    for (int i = 0; i < 3 && y + barH <= S - pad; ++i) {
        p.drawRoundedRect(pad + 5, y, S - pad - (pad+5), barH, 1, 1);
        y += barH + gap;
    }
    return QIcon(px);
}

// Bold: two vertical stems connected by two curved arms → geometric "B" shape.
static QIcon makeBoldIcon(const QColor &color = QColor(0x37, 0x41, 0x51))
{
    const int S = 16;
    QPixmap px(S, S);
    px.fill(Qt::transparent);
    QPainter p(&px);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);
    p.setBrush(color);

    // Vertical left stem (thick)
    p.drawRoundedRect(QRectF(2.5, 1.5, 2.5, 13), 1, 1);
    // Top bump
    QPainterPath top;
    top.moveTo(5, 1.5);
    top.lineTo(9, 1.5);
    top.quadTo(12.5, 1.5, 12.5, 5.0);
    top.quadTo(12.5, 8.0, 5, 8.0);
    top.lineTo(5, 1.5);
    p.fillPath(top, color);
    // Bottom bump (slightly wider)
    QPainterPath bot;
    bot.moveTo(5, 8.0);
    bot.lineTo(9.5, 8.0);
    bot.quadTo(13.5, 8.0, 13.5, 11.5);
    bot.quadTo(13.5, 14.5, 5, 14.5);
    bot.lineTo(5, 8.0);
    p.fillPath(bot, color);
    // Transparent cutouts work on both light and dark toolbar backgrounds.
    p.setCompositionMode(QPainter::CompositionMode_Clear);
    p.setBrush(Qt::transparent);
    p.drawEllipse(QRectF(5.5, 2.5, 5.5, 4.5));
    // White cutout bottom bump interior
    p.drawEllipse(QRectF(5.5, 8.8, 6.5, 4.5));

    return QIcon(px);
}

// Italic: a slanted bar with top/bottom serifs.
static QIcon makeItalicIcon(const QColor &color = QColor(0x37, 0x41, 0x51))
{
    const int S = 16;
    QPixmap px(S, S);
    px.fill(Qt::transparent);
    QPainter p(&px);
    p.setRenderHint(QPainter::Antialiasing);

    QPen pen(color, 2.0, Qt::SolidLine, Qt::RoundCap);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);

    // Slanted stem (top-right to bottom-left)
    p.drawLine(QLineF(9.5, 2.0, 6.5, 14.0));
    // Top serif
    p.drawLine(QLineF(7.0, 2.0, 12.0, 2.0));
    // Bottom serif
    p.drawLine(QLineF(4.0, 14.0, 9.0, 14.0));

    return QIcon(px);
}

// Underline: horizontal bar at bottom + short vertical lines (like "U" crossbar).
static QIcon makeUnderlineIcon(const QColor &color = QColor(0x37, 0x41, 0x51))
{
    const int S = 16;
    QPixmap px(S, S);
    px.fill(Qt::transparent);
    QPainter p(&px);
    p.setRenderHint(QPainter::Antialiasing);

    QPen pen(color, 2.0, Qt::SolidLine, Qt::RoundCap);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);

    // Left vertical stroke
    p.drawLine(QLineF(4.0, 2.0, 4.0, 10.0));
    // Right vertical stroke
    p.drawLine(QLineF(12.0, 2.0, 12.0, 10.0));
    // Bottom arc of U
    p.drawArc(QRectF(4.0, 6.0, 8.0, 6.0), 180*16, -180*16);
    // Underline bar (full width)
    p.setPen(QPen(color, 2.0, Qt::SolidLine, Qt::FlatCap));
    p.drawLine(QLineF(2.5, 14.5, 13.5, 14.5));

    return QIcon(px);
}

// ── Static helpers ────────────────────────────────────────────────────────────

QFrame *FormatBar::makeSep(QWidget *parent)
{
    auto *sep = new QFrame(parent);
    sep->setFrameShape(QFrame::VLine);
    sep->setFixedSize(1, 32);
    sep->setObjectName(QStringLiteral("FormatBarSeparator"));
    sep->setStyleSheet(QStringLiteral("background:#E5E7EB; border:none;"));
    return sep;
}

QPushButton *FormatBar::makeFmtBtn(const QIcon &icon, QWidget *parent)
{
    auto *btn = new QPushButton(parent);
    btn->setCheckable(true);
    btn->setFlat(true);
    btn->setFixedSize(30, 30);
    btn->setIcon(icon);
    btn->setIconSize({16, 16});
    btn->setProperty("formatButton", true);
    btn->setStyleSheet(QStringLiteral(
        "QPushButton { background:#FFFFFF; border:1px solid #D1D5DB; border-radius:5px; }"
        "QPushButton:checked { background:#EFF6FF; border-color:#BFDBFE; }"
        "QPushButton:hover:!checked { background:#F3F4F6; }"));
    return btn;
}

QPushButton *FormatBar::makeAlignBtn(const QString &/*iconName*/, const QString &/*fallback*/,
                                     const QString &tip, QWidget *parent)
{
    auto *btn = new QPushButton(parent);
    btn->setCheckable(true);
    btn->setFlat(true);
    btn->setFixedSize(30, 30);
    btn->setToolTip(tip);
    btn->setIconSize({16, 16});
    btn->setProperty("alignButton", true);
    btn->setStyleSheet(QStringLiteral(
        "QPushButton { background:transparent; border:none; border-radius:5px; }"
        "QPushButton:checked { background:#EFF6FF; }"
        "QPushButton:hover:!checked { background:#F3F4F6; }"));
    return btn;
}

// Build a labeled group: small label above, controls below.
// outLabel (optional): receives the QLabel* so callers can update it on retranslate.
static QWidget *makeGroup(const QString &label, QLayout *controls, QWidget *parent,
                          QLabel **outLabel = nullptr)
{
    auto *w  = new QWidget(parent);
    auto *vl = new QVBoxLayout(w);
    vl->setContentsMargins(0, 0, 0, 0);
    vl->setSpacing(0);

    auto *lbl = new QLabel(label, w);
    lbl->setProperty("formatLabel", true);
    lbl->setMinimumHeight(13);
    lbl->setMaximumHeight(18);
    lbl->setStyleSheet(QStringLiteral(
        "font-size:9px; font-weight:600; color:#9CA3AF;"
        " letter-spacing:0.2px; padding:0 0 1px 0;"));
    vl->addWidget(lbl);
    if (outLabel) *outLabel = lbl;

    auto *row = new QWidget(w);
    row->setLayout(controls);
    controls->setContentsMargins(0, 0, 0, 0);
    vl->addWidget(row);

    return w;
}

// ── FormatBar ─────────────────────────────────────────────────────────────────

FormatBar::FormatBar(QWidget *parent) : QFrame(parent)
{
    setObjectName(QStringLiteral("FormatBar"));
    setFixedHeight(60);
    setFrameShape(QFrame::NoFrame);
    setStyleSheet(QStringLiteral(
        "QFrame#FormatBar { background:#FFFFFF; border-bottom:1px solid #E5E7EB; }"));

    auto *h = new QHBoxLayout(this);
    h->setContentsMargins(16, 6, 16, 6);
    h->setSpacing(0);

    h->addStretch(1);

    // ── Schriftart ────────────────────────────────────────────────────────────
    {
        m_fontFamily = new QFontComboBox(this);
        m_fontFamily->setObjectName(QStringLiteral("FormatBarCombo"));
        m_fontFamily->setCurrentFont(QFont(QStringLiteral("Noto Sans")));
        m_fontFamily->setFixedWidth(192);
        m_fontFamily->setFixedHeight(30);
        m_fontFamily->setStyleSheet(QStringLiteral(
            "QFontComboBox { border:1px solid #D1D5DB; border-radius:6px;"
            "  padding-left:8px; padding-right:0px; font-size:12px; background:white; color:#111827; }"
            "QFontComboBox::drop-down { border-left:1px solid #D1D5DB; width:24px;"
            "  subcontrol-origin:border; subcontrol-position:right center; background:transparent; }"
            "QFontComboBox::down-arrow { image:url(:/icons/chevron-down.svg); width:12px; height:12px; }"
            "QFontComboBox:focus { border-color:#6366F1; outline:none; }"));
        connect(m_fontFamily, &QFontComboBox::currentFontChanged, this,
                [this](const QFont &f) { Q_EMIT fontFamilyChanged(f.family()); });
        auto *hl = new QHBoxLayout();
        hl->setSpacing(0);
        hl->addWidget(m_fontFamily);
        h->addWidget(makeGroup(tr("Font"), hl, this, &m_lblFont));
    }

    h->addSpacing(8);

    // ── Schriftgröße ──────────────────────────────────────────────────────────
    {
        m_fontSize = new QComboBox(this);
        m_fontSize->setObjectName(QStringLiteral("FormatBarCombo"));
        for (const int s : {8,9,10,11,12,14,16,18,20,24,28,32,36,48,64,72})
            m_fontSize->addItem(QString::number(s));
        m_fontSize->setCurrentText(QStringLiteral("14"));
        m_fontSize->setEditable(true);
        m_fontSize->setFixedWidth(66);
        m_fontSize->setFixedHeight(30);
        m_fontSize->setStyleSheet(QStringLiteral(
            "QComboBox { border:1px solid #D1D5DB; border-radius:6px;"
            "  padding-left:6px; padding-right:0px; font-size:12px; background:white; color:#111827; }"
            "QComboBox::drop-down { border-left:1px solid #D1D5DB; width:22px;"
            "  subcontrol-origin:border; subcontrol-position:right center; background:transparent; }"
            "QComboBox::down-arrow { image:url(:/icons/chevron-down.svg); width:12px; height:12px; }"
            "QComboBox:focus { border-color:#6366F1; }"));
        connect(m_fontSize, &QComboBox::currentTextChanged, this, [this](const QString &t) {
            bool ok; const int pt = t.toInt(&ok);
            if (ok && pt >= 4 && pt <= 400) {
                m_lastFontSize = pt;
                Q_EMIT fontSizeChanged(pt);
            }
        });
        // Restore last valid value when user leaves an empty or invalid field.
        connect(m_fontSize->lineEdit(), &QLineEdit::editingFinished, this, [this]() {
            const QString t = m_fontSize->currentText().trimmed();
            bool ok; const int pt = t.toInt(&ok);
            if (!ok || pt < 4 || pt > 400) {
                QSignalBlocker b(m_fontSize);
                m_fontSize->setCurrentText(QString::number(m_lastFontSize));
            }
        });
        auto *hl = new QHBoxLayout();
        hl->setSpacing(0);
        hl->addWidget(m_fontSize);
        h->addWidget(makeGroup(tr("Size"), hl, this, &m_lblSize));
    }

    h->addSpacing(10);
    h->addWidget(makeSep(this));
    h->addSpacing(10);

    // ── Fett / Kursiv / Unterstrichen ────────────────────────────────────────
    {
        m_bold      = makeFmtBtn(makeBoldIcon(),      this);
        m_italic    = makeFmtBtn(makeItalicIcon(),    this);
        m_underline = makeFmtBtn(makeUnderlineIcon(), this);
        m_bold->setToolTip(tr("Bold"));
        m_italic->setToolTip(tr("Italic"));
        m_underline->setToolTip(tr("Underline"));
        connect(m_bold,      &QPushButton::toggled, this, &FormatBar::boldToggled);
        connect(m_italic,    &QPushButton::toggled, this, &FormatBar::italicToggled);
        connect(m_underline, &QPushButton::toggled, this, &FormatBar::underlineToggled);
        auto *hl = new QHBoxLayout();
        hl->setSpacing(2);
        hl->addWidget(m_bold);
        hl->addWidget(m_italic);
        hl->addWidget(m_underline);
        // Use an invisible label so the group aligns vertically with labeled groups.
        h->addWidget(makeGroup(QStringLiteral(" "), hl, this));
    }

    h->addSpacing(8);

    // ── Textfarbe ─────────────────────────────────────────────────────────────
    {
        m_color = new QPushButton(this);
        m_color->setProperty("formatButton", true);
        m_color->setFlat(true);
        m_color->setFixedSize(44, 30);
        m_color->setToolTip(tr("Color"));
        m_color->setStyleSheet(QStringLiteral(
            "QPushButton { background:transparent; border:none; border-radius:5px;"
            "  font-size:12px; color:#111827; }"
            "QPushButton:hover { background:#F3F4F6; }"));
        updateColorSwatch(Qt::black);
        connect(m_color, &QPushButton::clicked, this, [this]() {
            const QColor c = QColorDialog::getColor(m_currentColor, this, tr("Choose text color"));
            if (c.isValid()) {
                m_currentColor = c;
                updateColorSwatch(c);
                Q_EMIT textColorChanged(c);
            }
        });
        auto *hl = new QHBoxLayout();
        hl->setSpacing(0);
        hl->addWidget(m_color);
        h->addWidget(makeGroup(tr("Color"), hl, this, &m_lblColor));
    }

    h->addSpacing(10);
    h->addWidget(makeSep(this));
    h->addSpacing(10);

    // ── Ausrichtung ───────────────────────────────────────────────────────────
    {
        // Icons built here with literal initializer_list — not stored in structs
        // (std::initializer_list does not own its backing array, so storing it
        // in a struct produces dangling pointers after the initializer statement).
        struct AlignDef { const char *tip; Qt::Alignment align; };
        const AlignDef defs[] = {
            { QT_TR_NOOP("Align Left"),  Qt::AlignLeft    },
            { QT_TR_NOOP("Center"),      Qt::AlignHCenter },
            { QT_TR_NOOP("Align Right"), Qt::AlignRight   },
            { QT_TR_NOOP("Justify"),     Qt::AlignJustify },
        };
        const QIcon icons[] = {
            makeAlignIcon({1.f, 0.7f, 0.85f, 0.6f},  {0.f, 0.f,  0.f,  0.f }),
            makeAlignIcon({1.f, 0.6f, 0.8f,  0.55f}, {0.f, 0.5f, 0.5f, 0.5f}),
            makeAlignIcon({1.f, 0.7f, 0.85f, 0.6f},  {0.f, 1.f,  1.f,  1.f }),
            makeAlignIcon({1.f, 1.f,  1.f,   0.65f}, {0.f, 0.f,  0.f,  0.f }),
        };

        auto *grp = new QButtonGroup(this);
        grp->setExclusive(true);
        auto *hl = new QHBoxLayout();
        hl->setSpacing(2);
        for (int i = 0; i < 4; ++i) {
            auto *btn = makeAlignBtn({}, {}, tr(defs[i].tip), this);
            btn->setIcon(icons[i]);
            if (i == 0) btn->setChecked(true);
            grp->addButton(btn);
            m_alignButtons.append(btn);
            hl->addWidget(btn);
            const Qt::Alignment al = defs[i].align;
            connect(btn, &QPushButton::clicked, this, [this, al]() {
                Q_EMIT alignmentChanged(al);
            });
        }
        h->addWidget(makeGroup(tr("Alignment"), hl, this, &m_lblAlign));
    }

    h->addSpacing(10);
    h->addWidget(makeSep(this));
    h->addSpacing(10);

    // ── Aufzählung / Abstand ──────────────────────────────────────────────────
    {
        const QString btnStyle = QStringLiteral(
            "QPushButton { background:transparent; border:1px solid #D1D5DB;"
            "  border-radius:5px; color:#374151; font-size:11px; padding:0 4px; }"
            "QPushButton:hover { background:#F3F4F6; border-color:#9CA3AF; }"
            "QPushButton:pressed { background:#E5E7EB; }");

        m_list = new QPushButton(this);
        m_list->setFixedSize(36, 30);
        m_list->setToolTip(tr("List"));
        m_list->setIcon(makeListIcon(false));
        m_list->setIconSize({14, 14});
        m_list->setText(QStringLiteral(" ▾"));
        m_list->setProperty("menuButton", true);
        m_list->setStyleSheet(btnStyle);

        m_indent = new QPushButton(this);
        m_indent->setFixedSize(36, 30);
        m_indent->setToolTip(tr("Indent"));
        m_indent->setIcon(makeIndentIcon());
        m_indent->setIconSize({14, 14});
        m_indent->setText(QStringLiteral(" ▾"));
        m_indent->setProperty("menuButton", true);
        m_indent->setStyleSheet(btnStyle);

        auto *listMenu = new QMenu(m_list);
        listMenu->setObjectName(QStringLiteral("FormatBarMenu"));
        auto *none = listMenu->addAction(tr("No List"));
        auto *bullets = listMenu->addAction(tr("Bulleted List"));
        auto *numbers = listMenu->addAction(tr("Numbered List"));
        connect(none, &QAction::triggered, this, [this] { Q_EMIT listStyleChanged(TextBoxProperties::ListStyle::None); });
        connect(bullets, &QAction::triggered, this, [this] { Q_EMIT listStyleChanged(TextBoxProperties::ListStyle::Bullets); });
        connect(numbers, &QAction::triggered, this, [this] { Q_EMIT listStyleChanged(TextBoxProperties::ListStyle::Numbered); });
        connect(m_list, &QPushButton::clicked, this, [this, listMenu] {
            listMenu->popup(m_list->mapToGlobal(QPoint(0, m_list->height() + 4)));
        });

        auto *indentMenu = new QMenu(m_indent);
        indentMenu->setObjectName(QStringLiteral("FormatBarMenu"));
        auto *decrease = indentMenu->addAction(tr("Decrease Indent"));
        auto *increase = indentMenu->addAction(tr("Increase Indent"));
        connect(decrease, &QAction::triggered, this, [this] { Q_EMIT indentChanged(-1); });
        connect(increase, &QAction::triggered, this, [this] { Q_EMIT indentChanged(1); });
        connect(m_indent, &QPushButton::clicked, this, [this, indentMenu] {
            indentMenu->popup(m_indent->mapToGlobal(QPoint(0, m_indent->height() + 4)));
        });

        auto *hl = new QHBoxLayout();
        hl->setSpacing(4);
        hl->addWidget(m_list);
        hl->addWidget(m_indent);
        h->addWidget(makeGroup(tr("List / Spacing"), hl, this, &m_lblList));
    }

    h->addSpacing(8);

    // ── Zeilenabstand ─────────────────────────────────────────────────────────
    {
        m_spacing = new QComboBox(this);
        m_spacing->setObjectName(QStringLiteral("FormatBarCombo"));
        m_spacing->addItems({QStringLiteral("1,0"), QStringLiteral("1,15"),
                             QStringLiteral("1,5"), QStringLiteral("2,0")});
        m_spacing->setCurrentIndex(2);
        m_spacing->setFixedWidth(68);
        m_spacing->setFixedHeight(30);
        m_spacing->setStyleSheet(QStringLiteral(
            "QComboBox { border:1px solid #D1D5DB; border-radius:6px;"
            "  padding-left:6px; padding-right:0px; font-size:12px; background:white; color:#111827; }"
            "QComboBox::drop-down { border-left:1px solid #D1D5DB; width:22px;"
            "  subcontrol-origin:border; subcontrol-position:right center; background:transparent; }"
            "QComboBox::down-arrow { image:url(:/icons/chevron-down.svg); width:12px; height:12px; }"));
        connect(m_spacing, &QComboBox::currentIndexChanged, this, [this](int index) {
            static constexpr double values[] = {1.0, 1.15, 1.5, 2.0};
            if (index >= 0 && index < 4) Q_EMIT lineSpacingChanged(values[index]);
        });
        auto *hl = new QHBoxLayout();
        hl->setSpacing(0);
        hl->addWidget(m_spacing);
        h->addWidget(makeGroup(tr("Line Spacing"), hl, this, &m_lblSpacing));
    }

    h->addStretch(1);

    // Theme styles belong to the application stylesheet. The old per-widget
    // white styles overrode Dark Mode completely.
    setStyleSheet({});
    for (QWidget *child : findChildren<QWidget *>())
        child->setStyleSheet({});
    refreshTheme();
}

// ── Public setters ────────────────────────────────────────────────────────────

void FormatBar::setFontSize(int ptSize)
{
    const QString s = QString::number(ptSize);
    if (m_fontSize->currentText() != s) {
        QSignalBlocker b(m_fontSize);
        m_fontSize->setCurrentText(s);
    }
}

void FormatBar::setTextColor(const QColor &c)
{
    if (!c.isValid()) return;
    m_currentColor = c;
    updateColorSwatch(c);
}

void FormatBar::setFontFamily(const QString &family)
{
    if (family.isEmpty() || m_fontFamily->currentFont().family() == family)
        return;
    QSignalBlocker b(m_fontFamily);
    m_fontFamily->setCurrentFont(QFont(family));
}

void FormatBar::setBoldChecked(bool on)
{
    if (m_bold->isChecked() == on) return;
    QSignalBlocker b(m_bold);
    m_bold->setChecked(on);
}

void FormatBar::setItalicChecked(bool on)
{
    if (m_italic->isChecked() == on) return;
    QSignalBlocker b(m_italic);
    m_italic->setChecked(on);
}

void FormatBar::setAlignment(TextBoxProperties::HorizontalAlign alignment)
{
    const int index = static_cast<int>(alignment);
    if (index < 0 || index >= m_alignButtons.size()) return;
    QSignalBlocker blocker(m_alignButtons[index]);
    m_alignButtons[index]->setChecked(true);
}

void FormatBar::setLineSpacing(double multiplier)
{
    static constexpr double values[] = {1.0, 1.15, 1.5, 2.0};
    int closest = 0;
    for (int i = 1; i < 4; ++i)
        if (qAbs(values[i] - multiplier) < qAbs(values[closest] - multiplier))
            closest = i;
    QSignalBlocker blocker(m_spacing);
    m_spacing->setCurrentIndex(closest);
}

void FormatBar::refreshTheme()
{
    const bool dark = palette().color(QPalette::Window).lightness() < 128;
    const QColor iconColor = dark ? QColor(QStringLiteral("#D8D8D8"))
                                  : QColor(QStringLiteral("#374151"));
    m_bold->setIcon(makeBoldIcon(iconColor));
    m_italic->setIcon(makeItalicIcon(iconColor));
    m_underline->setIcon(makeUnderlineIcon(iconColor));
    const QIcon alignIcons[] = {
        makeAlignIcon({1.f,.7f,.85f,.6f},{0.f,0.f,0.f,0.f},iconColor),
        makeAlignIcon({1.f,.6f,.8f,.55f},{0.f,.5f,.5f,.5f},iconColor),
        makeAlignIcon({1.f,.7f,.85f,.6f},{0.f,1.f,1.f,1.f},iconColor),
        makeAlignIcon({1.f,1.f,1.f,.65f},{0.f,0.f,0.f,0.f},iconColor)
    };
    for (int i = 0; i < m_alignButtons.size() && i < 4; ++i)
        m_alignButtons[i]->setIcon(alignIcons[i]);
    if (m_list) m_list->setIcon(makeListIcon(false, iconColor));
    if (m_indent) m_indent->setIcon(makeIndentIcon(iconColor));
    updateColorSwatch(m_currentColor);
}

void FormatBar::updateColorSwatch(const QColor &c)
{
    // Compound icon: 14×14 color swatch + 4px gap + 10px chevron, all on 28×14 canvas.
    const int swatchW = 14, chevW = 10, gap = 4;
    const int totalW = swatchW + gap + chevW;
    QPixmap px(totalW, 14);
    px.fill(Qt::transparent);
    QPainter p(&px);

    // Color swatch with border
    p.setPen(QPen(QColor(0xD1, 0xD5, 0xDB), 1));
    p.setBrush(c);
    p.drawRoundedRect(QRectF(0.5, 0.5, swatchW - 1, 13), 2, 2);

    const bool dark = palette().color(QPalette::Window).lightness() < 128;
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(dark ? QColor(QStringLiteral("#D8D8D8"))
                        : QColor(QStringLiteral("#6B7280")),
                  1.8, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.setBrush(Qt::NoBrush);
    const int cx = swatchW + gap;
    QPolygonF chev;
    chev << QPointF(cx + 1, 5) << QPointF(cx + 5, 9) << QPointF(cx + 9, 5);
    p.drawPolyline(chev);

    m_color->setIcon(QIcon(px));
    m_color->setIconSize({totalW, 14});
    m_color->setText({});
}

void FormatBar::retranslateUi()
{
    if (m_lblFont)    m_lblFont->setText(tr("Font"));
    if (m_lblSize)    m_lblSize->setText(tr("Size"));
    if (m_lblColor)   m_lblColor->setText(tr("Color"));
    if (m_lblAlign)   m_lblAlign->setText(tr("Alignment"));
    if (m_lblList)    m_lblList->setText(tr("List / Spacing"));
    if (m_lblSpacing) m_lblSpacing->setText(tr("Line Spacing"));
    m_bold->setToolTip(tr("Bold"));
    m_italic->setToolTip(tr("Italic"));
    m_underline->setToolTip(tr("Underline"));
    m_color->setToolTip(tr("Color"));
}

void FormatBar::changeEvent(QEvent *e)
{
    QFrame::changeEvent(e);
    if (e->type() == QEvent::LanguageChange)
        retranslateUi();
}
