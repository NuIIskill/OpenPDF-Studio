#include "FormatBar.hpp"

#include <QButtonGroup>
#include <QFontComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QComboBox>
#include <QPushButton>
#include <QPixmap>

// ── Helpers ───────────────────────────────────────────────────────────────────

QFrame *FormatBar::makeSep(QWidget *parent)
{
    auto *sep = new QFrame(parent);
    sep->setFrameShape(QFrame::VLine);
    sep->setFixedWidth(1);
    sep->setFixedHeight(22);
    sep->setStyleSheet(QStringLiteral("background:#D1D5DB; border:none;"));
    return sep;
}

QPushButton *FormatBar::makeFmtBtn(const QString &text, QWidget *parent,
                                   bool bold, bool italic, bool underline)
{
    auto *btn = new QPushButton(text, parent);
    btn->setCheckable(true);
    btn->setFlat(true);
    btn->setFixedSize(34, 34);
    QString extra;
    if (bold)      extra += QStringLiteral("font-weight:700;");
    if (italic)    extra += QStringLiteral("font-style:italic;");
    if (underline) extra += QStringLiteral("text-decoration:underline;");
    btn->setStyleSheet(QString(
        "QPushButton { background:transparent; border:none; border-radius:5px;"
        "  font-size:14px; color:#374151; %1 }"
        "QPushButton:checked { background:#EFF6FF; color:#3B82F6; }"
        "QPushButton:hover:!checked { background:#F3F4F6; }").arg(extra));
    return btn;
}

QPushButton *FormatBar::makeAlignBtn(const QString &text, QWidget *parent)
{
    auto *btn = new QPushButton(text, parent);
    btn->setCheckable(true);
    btn->setFlat(true);
    btn->setFixedSize(34, 34);
    btn->setStyleSheet(QStringLiteral(
        "QPushButton { background:transparent; border:none; border-radius:5px;"
        "  font-size:15px; color:#374151; }"
        "QPushButton:checked { background:#EFF6FF; color:#3B82F6; }"
        "QPushButton:hover:!checked { background:#F3F4F6; }"));
    return btn;
}

// ── FormatBar ─────────────────────────────────────────────────────────────────

FormatBar::FormatBar(QWidget *parent) : QFrame(parent)
{
    setObjectName(QStringLiteral("FormatBar"));
    setFixedHeight(48);
    setFrameShape(QFrame::NoFrame);
    setStyleSheet(QStringLiteral(
        "QFrame#FormatBar {"
        "  background:white;"
        "  border-bottom:1px solid #E5E7EB;"
        "}"));

    auto *h = new QHBoxLayout(this);
    h->setContentsMargins(12, 0, 12, 0);
    h->setSpacing(4);

    h->addStretch(1);   // center the controls

    // ── Font family ───────────────────────────────────────────────────────
    m_fontFamily = new QFontComboBox(this);
    m_fontFamily->setCurrentFont(QFont(QStringLiteral("Inter")));
    m_fontFamily->setFixedWidth(140);
    m_fontFamily->setFixedHeight(34);
    m_fontFamily->setStyleSheet(QStringLiteral(
        "QFontComboBox { border:1px solid #D1D5DB; border-radius:6px;"
        "  padding:0 8px; font-size:13px; background:white; }"
        "QFontComboBox::drop-down { border:none; width:20px; }"));
    h->addWidget(m_fontFamily);

    // ── Font size ─────────────────────────────────────────────────────────
    m_fontSize = new QComboBox(this);
    for (const int s : { 8, 9, 10, 11, 12, 14, 16, 18, 20, 24, 28, 32, 36, 48, 64, 72 })
        m_fontSize->addItem(QString::number(s));
    m_fontSize->setCurrentText(QStringLiteral("14"));
    m_fontSize->setEditable(true);
    m_fontSize->setFixedWidth(64);
    m_fontSize->setFixedHeight(34);
    m_fontSize->setStyleSheet(QStringLiteral(
        "QComboBox { border:1px solid #D1D5DB; border-radius:6px;"
        "  padding:0 6px; font-size:13px; background:white; }"
        "QComboBox::drop-down { border:none; width:20px; }"));
    h->addWidget(m_fontSize);

    h->addWidget(makeSep(this));

    // ── B / I / U ─────────────────────────────────────────────────────────
    m_bold      = makeFmtBtn(QStringLiteral("B"), this, true,  false, false);
    m_italic    = makeFmtBtn(QStringLiteral("I"), this, false, true,  false);
    m_underline = makeFmtBtn(QStringLiteral("U"), this, false, false, true);
    h->addWidget(m_bold);
    h->addWidget(m_italic);
    h->addWidget(m_underline);

    // ── Color button ──────────────────────────────────────────────────────
    m_color = new QPushButton(this);
    m_color->setFlat(true);
    m_color->setFixedSize(40, 34);
    m_color->setStyleSheet(QStringLiteral(
        "QPushButton { background:transparent; border:none; border-radius:5px; }"
        "QPushButton:hover { background:#F3F4F6; }"));
    QPixmap swatch(14, 14);
    swatch.fill(Qt::black);
    m_color->setIcon(QIcon(swatch));
    m_color->setIconSize({14, 14});
    m_color->setText(QStringLiteral(" ▾"));
    h->addWidget(m_color);

    h->addWidget(makeSep(this));

    // ── Alignment buttons ─────────────────────────────────────────────────
    auto *alignLeft    = makeAlignBtn(QStringLiteral("⇤"), this);
    auto *alignCenter  = makeAlignBtn(QStringLiteral("↔"), this);
    auto *alignRight   = makeAlignBtn(QStringLiteral("⇥"), this);
    auto *alignJustify = makeAlignBtn(QStringLiteral("⇔"), this);
    // Use proper align icons via SVG-style unicode block chars
    alignLeft->setText(QStringLiteral("≡"));
    alignCenter->setText(QStringLiteral("≡"));
    alignRight->setText(QStringLiteral("≡"));
    alignJustify->setText(QStringLiteral("≡"));
    alignLeft->setToolTip(tr("Align Left"));
    alignCenter->setToolTip(tr("Center"));
    alignRight->setToolTip(tr("Align Right"));
    alignJustify->setToolTip(tr("Justify"));
    alignLeft->setChecked(true);
    auto *alignGroup = new QButtonGroup(this);
    alignGroup->setExclusive(true);
    for (auto *b : {alignLeft, alignCenter, alignRight, alignJustify}) {
        alignGroup->addButton(b);
        h->addWidget(b);
    }

    h->addWidget(makeSep(this));

    // ── Line spacing ──────────────────────────────────────────────────────
    m_spacing = new QComboBox(this);
    m_spacing->addItems({ QStringLiteral("1,0"), QStringLiteral("1,15"),
                          QStringLiteral("1,5"), QStringLiteral("2,0") });
    m_spacing->setCurrentIndex(2);
    m_spacing->setFixedHeight(34);
    m_spacing->setFixedWidth(80);
    m_spacing->setStyleSheet(QStringLiteral(
        "QComboBox { border:1px solid #D1D5DB; border-radius:6px;"
        "  padding:0 6px; font-size:13px; background:white; }"
        "QComboBox::drop-down { border:none; width:20px; }"));
    auto *spacingIcon = new QLabel(QStringLiteral("↕"), this);
    spacingIcon->setStyleSheet(QStringLiteral("font-size:15px; color:#374151; margin-right:2px;"));
    h->addWidget(spacingIcon);
    h->addWidget(m_spacing);

    h->addStretch(1);

    // ── More button ───────────────────────────────────────────────────────
    auto *moreBtn = new QPushButton(QStringLiteral("···"), this);
    moreBtn->setFlat(true);
    moreBtn->setFixedSize(34, 34);
    moreBtn->setStyleSheet(QStringLiteral(
        "QPushButton { background:transparent; border:none; border-radius:5px;"
        "  font-size:16px; color:#374151; letter-spacing:2px; }"
        "QPushButton:hover { background:#F3F4F6; }"));
    h->addWidget(moreBtn);
}

void FormatBar::retranslateUi()
{
    // Button tooltips only — labels are symbols that don't change
}
