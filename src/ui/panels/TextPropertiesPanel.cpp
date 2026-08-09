#include "TextPropertiesPanel.hpp"

#include <QComboBox>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QSlider>
#include <QVBoxLayout>

// ── helpers ───────────────────────────────────────────────────────────────────

static QFrame *makeDivider(QWidget *parent)
{
    auto *line = new QFrame(parent);
    line->setFrameShape(QFrame::HLine);
    line->setFixedHeight(1);
    line->setStyleSheet(QStringLiteral("background:#E5E7EB; border:none;"));
    return line;
}

static QLabel *makeSectionLabel(const QString &text, QWidget *parent)
{
    auto *lbl = new QLabel(text, parent);
    lbl->setStyleSheet(QStringLiteral(
        "font-size:11px; color:#6B7280; font-weight:600; margin-top:6px;"));
    return lbl;
}

static QPushButton *makeToggle(QWidget *parent, bool on)
{
    auto *btn = new QPushButton(parent);
    btn->setCheckable(true);
    btn->setChecked(on);
    btn->setFixedSize(40, 22);
    const auto applyStyle = [btn](bool checked) {
        btn->setStyleSheet(checked
            ? QStringLiteral("QPushButton{background:#3B82F6;border-radius:11px;border:none;}")
            : QStringLiteral("QPushButton{background:#D1D5DB;border-radius:11px;border:none;}"));
    };
    applyStyle(on);
    QObject::connect(btn, &QPushButton::toggled, btn, [applyStyle](bool c){ applyStyle(c); });
    return btn;
}

// ── TextPropertiesPanel ───────────────────────────────────────────────────────

TextPropertiesPanel::TextPropertiesPanel(QWidget *parent)
    : QFrame(parent)
{
    setObjectName(QStringLiteral("TextPanel"));
    setAttribute(Qt::WA_StyledBackground, true);
    setFixedWidth(kWidth);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
    setStyleSheet(QStringLiteral(
        "QFrame#TextPanel{background:white;border-right:1px solid #E5E7EB;}"));

    auto *scroll = new QScrollArea(this);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    auto *content = new QWidget();

    auto *vl = new QVBoxLayout(content);
    vl->setContentsMargins(16, 12, 16, 16);
    vl->setSpacing(8);

    // Header
    auto *headerRow = new QHBoxLayout();
    m_title = new QLabel(tr("Text"), content);
    m_title->setStyleSheet(QStringLiteral(
        "font-size:14px;font-weight:700;color:#111827;"));
    headerRow->addWidget(m_title);
    headerRow->addStretch();
    auto *closeBtn = new QPushButton(QStringLiteral("✕"), content);
    closeBtn->setFlat(true);
    closeBtn->setFixedSize(24, 24);
    closeBtn->setStyleSheet(QStringLiteral(
        "QPushButton{background:transparent;border:none;color:#6B7280;font-size:13px;}"
        "QPushButton:hover{color:#111827;}"));
    connect(closeBtn, &QPushButton::clicked, this, &TextPropertiesPanel::closeRequested);
    headerRow->addWidget(closeBtn);
    vl->addLayout(headerRow);
    vl->addWidget(makeDivider(content));

    // Zeilenabstand
    m_lSpacing = makeSectionLabel(tr("Line Spacing"), content);
    vl->addWidget(m_lSpacing);
    m_spacing = new QComboBox(content);
    m_spacing->addItems({ QStringLiteral("1.0"), QStringLiteral("1.15"),
                          QStringLiteral("1.5"), QStringLiteral("2.0") });
    m_spacing->setCurrentIndex(2);
    m_spacing->setFixedHeight(34);
    m_spacing->setStyleSheet(QStringLiteral(
        "QComboBox{border:1px solid #D1D5DB;border-radius:6px;"
        "padding:4px 8px;font-size:13px;background:white;}"
        "QComboBox::drop-down{border:none;}"));
    vl->addWidget(m_spacing);
    vl->addWidget(makeDivider(content));

    // Deckkraft
    auto *opRow = new QHBoxLayout();
    m_lOpacity = makeSectionLabel(tr("Opacity"), content);
    opRow->addWidget(m_lOpacity);
    opRow->addStretch();
    m_opacityVal = new QLabel(QStringLiteral("100 %"), content);
    m_opacityVal->setStyleSheet(QStringLiteral("font-size:12px;color:#374151;"));
    opRow->addWidget(m_opacityVal);
    vl->addLayout(opRow);
    m_opacity = new QSlider(Qt::Horizontal, content);
    m_opacity->setRange(0, 100);
    m_opacity->setValue(100);
    m_opacity->setStyleSheet(QStringLiteral(
        "QSlider::groove:horizontal{height:4px;background:#E5E7EB;border-radius:2px;}"
        "QSlider::handle:horizontal{width:16px;height:16px;margin:-6px 0;"
        "background:#3B82F6;border-radius:8px;}"
        "QSlider::sub-page:horizontal{background:#3B82F6;border-radius:2px;}"));
    connect(m_opacity, &QSlider::valueChanged, this, [this](int v) {
        m_opacityVal->setText(QString::number(v) + QStringLiteral(" %"));
    });
    vl->addWidget(m_opacity);
    vl->addWidget(makeDivider(content));

    // Rahmen
    auto *borderRow = new QHBoxLayout();
    m_lBorder = makeSectionLabel(tr("Border"), content);
    borderRow->addWidget(m_lBorder);
    borderRow->addStretch();
    borderRow->addWidget(makeToggle(content, true));
    vl->addLayout(borderRow);
    auto *bsRow = new QHBoxLayout();
    bsRow->setSpacing(6);
    auto *borderStyle = new QComboBox(content);
    borderStyle->addItems({ tr("Solid"), tr("Dashed"), tr("Dotted") });
    borderStyle->setFixedHeight(30);
    borderStyle->setStyleSheet(QStringLiteral(
        "QComboBox{border:1px solid #D1D5DB;border-radius:4px;"
        "padding:2px 6px;font-size:12px;background:white;}"
        "QComboBox::drop-down{border:none;}"));
    auto *borderWidth = new QComboBox(content);
    borderWidth->addItems({ QStringLiteral("1 pt"), QStringLiteral("2 pt"),
                            QStringLiteral("3 pt"), QStringLiteral("4 pt") });
    borderWidth->setFixedHeight(30);
    borderWidth->setFixedWidth(60);
    borderWidth->setStyleSheet(borderStyle->styleSheet());
    bsRow->addWidget(borderStyle, 1);
    bsRow->addWidget(borderWidth);
    vl->addLayout(bsRow);
    vl->addWidget(makeDivider(content));

    // Hintergrund
    auto *bgRow = new QHBoxLayout();
    m_lBg = makeSectionLabel(tr("Background"), content);
    bgRow->addWidget(m_lBg);
    bgRow->addStretch();
    bgRow->addWidget(makeToggle(content, false));
    vl->addLayout(bgRow);
    auto *bgColorRow = new QHBoxLayout();
    bgColorRow->setSpacing(6);
    for (const QColor &c : { QColor(Qt::white), QColor("#FEF3C7"),
                              QColor("#DBEAFE"), QColor("#D1FAE5"), QColor("#FCE7F3") }) {
        auto *swatch = new QPushButton(content);
        swatch->setFixedSize(26, 26);
        swatch->setCheckable(true);
        swatch->setFlat(true);
        swatch->setStyleSheet(QString(
            "QPushButton{background:%1;border:1px solid #D1D5DB;border-radius:4px;}"
            "QPushButton:checked{border:2px solid #3B82F6;}").arg(c.name()));
        if (c == QColor(Qt::white)) swatch->setChecked(true);
        bgColorRow->addWidget(swatch);
    }
    bgColorRow->addStretch();
    vl->addLayout(bgColorRow);
    vl->addWidget(makeDivider(content));

    // Als Standard speichern
    auto *saveBtn = new QPushButton(tr("Save as Default"), content);
    saveBtn->setFixedHeight(36);
    saveBtn->setStyleSheet(QStringLiteral(
        "QPushButton{background:#3B82F6;color:white;border:none;"
        "border-radius:6px;font-size:13px;font-weight:600;}"
        "QPushButton:hover{background:#2563EB;}"
        "QPushButton:pressed{background:#1D4ED8;}"));
    vl->addWidget(saveBtn);
    vl->addStretch(1);

    scroll->setWidget(content);

    auto *panelVl = new QVBoxLayout(this);
    panelVl->setContentsMargins(0, 0, 0, 0);
    panelVl->addWidget(scroll);
}

void TextPropertiesPanel::retranslateUi()
{
    if (m_title)     m_title->setText(tr("Text"));
    if (m_lSpacing)  m_lSpacing->setText(tr("Line Spacing"));
    if (m_lOpacity)  m_lOpacity->setText(tr("Opacity"));
    if (m_lBorder)   m_lBorder->setText(tr("Border"));
    if (m_lBg)       m_lBg->setText(tr("Background"));
}
