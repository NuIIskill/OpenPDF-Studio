#include "ui/sign/SignDialog.hpp"
#include "app/SignatureStore.hpp"
#include "ui/sign/DigitalSignPage.hpp"
#include "ui/sign/SavedSignaturesView.hpp"
#include "ui/sign/SignaturePad.hpp"
#include "ui/sign/SignTabBar.hpp"
#include "ui/theme/Theme.hpp"

#include <QAction>
#include <QButtonGroup>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontInfo>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QImageReader>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QSlider>
#include <QStackedWidget>
#include <QToolButton>
#include <QVBoxLayout>

namespace {

enum Mode { Draw, Type, Import, Saved };

constexpr qreal kImageScale  = 4.0;
constexpr int   kMaxImageDim = 1600;

QPixmap colorSwatch(const QColor &color)
{
    QPixmap px(40, 20);
    px.fill(Qt::transparent);
    QPainter p(&px);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);
    p.setBrush(color);
    p.drawEllipse(QRectF(1, 1, 18, 18));
    p.drawPixmap(24, 3, Theme::renderSvg(QStringLiteral("chevron-down"), Theme::IconNormal, 14));
    return px;
}

QImage capped(const QImage &img)
{
    if (img.width() <= kMaxImageDim && img.height() <= kMaxImageDim) return img;
    return img.scaled(kMaxImageDim, kMaxImageDim, Qt::KeepAspectRatio, Qt::SmoothTransformation);
}

QImage loadCapped(const QString &path)
{
    QImageReader reader(path);
    const QSize size = reader.size();
    if (size.width() > kMaxImageDim || size.height() > kMaxImageDim)
        reader.setScaledSize(size.scaled(kMaxImageDim, kMaxImageDim, Qt::KeepAspectRatio));
    return reader.read();
}

QImage typedImage(const QString &text, QFont font, const QColor &color)
{
    font.setPixelSize(48);
    QPainterPath path;
    path.addText(0, 0, font, text);
    const QRectF bounds = path.boundingRect().adjusted(-4, -4, 4, 4);

    QImage img((bounds.size() * kImageScale).toSize().expandedTo(QSize(1, 1)),
               QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::transparent);
    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing);
    p.scale(kImageScale, kImageScale);
    p.translate(-bounds.topLeft());
    p.fillPath(path, color);
    return img;
}

QFont scriptFont()
{
    for (const char *name : { "Segoe Script", "Brush Script MT", "Lucida Handwriting",
                              "URW Chancery L", "Z003", "Dancing Script" }) {
        const QFont font{ QLatin1String(name) };
        if (QFontInfo(font).family().section(QStringLiteral(" ["), 0, 0) == QLatin1String(name))
            return font;
    }
    QFont font(QStringLiteral("cursive"));
    font.setStyleHint(QFont::Cursive);
    return font;
}

}

SignDialog::SignDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Sign - OpenPDF Studio"));
    setModal(true);
    setFixedSize(780, 640);
    applyDialogStyle();

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    auto *body = new QVBoxLayout;
    body->setContentsMargins(20, 20, 20, 20);
    auto *card = new QFrame;
    card->setObjectName(QStringLiteral("SCard"));
    auto *cardLayout = new QVBoxLayout(card);
    cardLayout->setContentsMargins(0, 0, 0, 0);
    cardLayout->setSpacing(0);

    cardLayout->addWidget(buildTabs());
    m_pages = new QStackedWidget;
    m_pages->addWidget(buildHandwrittenPage());
    m_digital = new DigitalSignPage;
    connect(m_digital, &DigitalSignPage::changed, this, &SignDialog::updateButtons);
    m_pages->addWidget(m_digital);
    cardLayout->addWidget(m_pages);

    body->addWidget(card);
    root->addLayout(body);
    root->addWidget(buildFooter());

    setPenColor(m_penColor);
    updateButtons();
}

QImage SignDialog::signatureImage() const
{
    switch (m_modeGroup->checkedId()) {
    case Draw:
        return capped(m_pad->toImage(kImageScale));
    case Type: {
        const QString text = m_typedEdit->text().trimmed();
        return text.isEmpty() ? QImage() : capped(typedImage(text, m_typedEdit->font(), m_penColor));
    }
    case Import:
        return capped(m_image);
    case Saved:
        return m_saved->currentPath().isEmpty() ? QImage() : loadCapped(m_saved->currentPath());
    default:
        return {};
    }
}

bool SignDialog::isDigital() const
{
    return m_tabBar->currentIndex() == 1;
}

SignRequest SignDialog::signRequest() const
{
    return m_digital->signRequest();
}

SignatureAppearance SignDialog::appearance() const
{
    return m_digital->appearance();
}

QWidget *SignDialog::buildTabs()
{
    m_tabBar = new SignTabBar;
    m_tabBar->addIconTab(QStringLiteral("signature"), tr("Add signature"));
    m_tabBar->addIconTab(QStringLiteral("shield-check"), tr("Sign digitally"));
    if (!SignatureManager::available()) {
        m_tabBar->setTabEnabled(1, false);
        m_tabBar->setTabToolTip(1, tr("Not available in this build"));
    }
    connect(m_tabBar, &QTabBar::currentChanged, this, [this](int index) {
        m_pages->setCurrentIndex(index);
        if (index == 1) m_digital->loadCertificatesOnce();
        updateButtons();
    });
    return m_tabBar;
}

QWidget *SignDialog::buildHandwrittenPage()
{
    auto *page = new QWidget;
    auto *vl = new QVBoxLayout(page);
    vl->setContentsMargins(24, 20, 24, 20);
    vl->setSpacing(14);

    auto *prompt = new QLabel(tr("Create, import or choose a signature, then place it freely in the PDF."));
    prompt->setWordWrap(true);
    vl->addWidget(prompt);
    vl->addWidget(buildModeRow());
    vl->addWidget(buildCanvas(), 1);
    m_penRow = buildPenRow();
    vl->addWidget(m_penRow);

    m_infoRow = new QWidget;
    auto *info = new QHBoxLayout(m_infoRow);
    info->setContentsMargins(0, 0, 0, 0);
    info->setSpacing(8);
    auto *infoIcon = new QLabel;
    infoIcon->setPixmap(Theme::renderSvg(QStringLiteral("info"), Theme::IconMuted, 16));
    auto *infoText = new QLabel(tr("Once created, the signature can be placed freely in the PDF and adjusted later."));
    infoText->setWordWrap(true);
    infoText->setObjectName(QStringLiteral("SMuted"));
    info->addWidget(infoIcon);
    info->addWidget(infoText, 1);
    vl->addWidget(m_infoRow);
    return page;
}

QPushButton *SignDialog::makeModeButton(const QString &icon, const QString &text)
{
    auto *btn = new QPushButton(text);
    btn->setObjectName(QStringLiteral("SMode"));
    btn->setCheckable(true);
    btn->setCursor(Qt::PointingHandCursor);
    btn->setFixedHeight(44);
    btn->setIcon(Theme::makeIcon(icon, Theme::IconNormal, Theme::Primary, Theme::IconDisabled, 20));
    btn->setIconSize(QSize(20, 20));
    return btn;
}

QWidget *SignDialog::buildModeRow()
{
    auto *bar = new QFrame;
    bar->setObjectName(QStringLiteral("SModeBar"));
    auto *hl = new QHBoxLayout(bar);
    hl->setContentsMargins(0, 0, 0, 0);
    hl->setSpacing(0);

    m_modeGroup = new QButtonGroup(this);
    const struct { const char *icon; const char *text; } modes[] = {
        { "pencil",      QT_TR_NOOP("Draw")         },
        { "keyboard",    QT_TR_NOOP("Type")         },
        { "image",       QT_TR_NOOP("Import image") },
        { "folder-open", QT_TR_NOOP("Saved")        },
    };
    for (int i = 0; i < 4; ++i) {
        auto *btn = makeModeButton(QLatin1String(modes[i].icon), tr(modes[i].text));
        if (i == 0) btn->setProperty("edge", QStringLiteral("left"));
        if (i == 3) btn->setProperty("edge", QStringLiteral("right"));
        m_modeGroup->addButton(btn, i);
        hl->addWidget(btn, 1);
    }
    m_modeGroup->button(Draw)->setChecked(true);
    connect(m_modeGroup, &QButtonGroup::idClicked, this, &SignDialog::onModeChanged);
    return bar;
}

QWidget *SignDialog::buildCanvas()
{
    auto *frame = new QFrame;
    frame->setObjectName(QStringLiteral("SPadFrame"));
    frame->setMinimumHeight(170);
    auto *grid = new QGridLayout(frame);
    grid->setContentsMargins(1, 1, 1, 1);

    m_pad = new SignaturePad;
    connect(m_pad, &SignaturePad::changed, this, &SignDialog::updateButtons);


    m_canvas = new QStackedWidget;
    m_canvas->addWidget(m_pad);
    m_canvas->addWidget(buildTypedCanvas());
    m_canvas->addWidget(buildImageCanvas());
    m_saved = new SavedSignaturesView;
    connect(m_saved, &SavedSignaturesView::currentChanged, this, &SignDialog::updateButtons);
    m_canvas->addWidget(m_saved);
    grid->addWidget(m_canvas, 0, 0);

    m_clearBtn = new QPushButton(tr("Clear"));
    m_clearBtn->setObjectName(QStringLiteral("SClear"));
    m_clearBtn->setCursor(Qt::PointingHandCursor);
    m_clearBtn->setIcon(Theme::renderSvg(QStringLiteral("x"), Theme::IconNormal, 12));
    m_clearBtn->setIconSize(QSize(12, 12));
    connect(m_clearBtn, &QPushButton::clicked, this, &SignDialog::onClear);
    grid->addWidget(m_clearBtn, 0, 0, Qt::AlignTop | Qt::AlignRight);
    return frame;
}

QWidget *SignDialog::buildTypedCanvas()
{
    m_typedEdit = new QLineEdit;
    m_typedEdit->setObjectName(QStringLiteral("STyped"));
    m_typedEdit->setAlignment(Qt::AlignCenter);
    m_typedEdit->setPlaceholderText(tr("Type your name"));
    m_typedEdit->setMaxLength(32);

    QFont font = scriptFont();
    font.setPointSize(28);
    font.setItalic(true);
    m_typedEdit->setFont(font);
    connect(m_typedEdit, &QLineEdit::textChanged, this, &SignDialog::updateButtons);
    return m_typedEdit;
}

QWidget *SignDialog::buildImageCanvas()
{
    auto *page = new QWidget;
    auto *vl = new QVBoxLayout(page);
    vl->setAlignment(Qt::AlignCenter);

    m_imageLabel = new QLabel;
    m_imageLabel->setAlignment(Qt::AlignCenter);
    m_imageLabel->hide();
    m_chooseImage = new QPushButton(tr("Choose image..."));
    m_chooseImage->setObjectName(QStringLiteral("SSecondary"));
    m_chooseImage->setCursor(Qt::PointingHandCursor);
    m_chooseImage->setFixedHeight(34);
    connect(m_chooseImage, &QPushButton::clicked, this, &SignDialog::onChooseImage);

    vl->addWidget(m_imageLabel);
    vl->addWidget(m_chooseImage, 0, Qt::AlignCenter);
    return page;
}

QWidget *SignDialog::buildPenRow()
{
    auto *row = new QWidget;
    auto *hl = new QHBoxLayout(row);
    hl->setContentsMargins(0, 0, 0, 0);
    hl->setSpacing(10);

    m_colorBtn = new QToolButton;
    m_colorBtn->setObjectName(QStringLiteral("SColor"));
    m_colorBtn->setPopupMode(QToolButton::InstantPopup);
    m_colorBtn->setIconSize(QSize(40, 20));
    m_colorBtn->setCursor(Qt::PointingHandCursor);
    auto *menu = new QMenu(m_colorBtn);
    const struct { const char *hex; const char *name; } colors[] = {
        { "#1D4ED8", QT_TR_NOOP("Blue")      },
        { "#1E3A8A", QT_TR_NOOP("Dark blue") },
        { "#111827", QT_TR_NOOP("Black")     },
        { "#B91C1C", QT_TR_NOOP("Red")       },
    };
    for (const auto &c : colors) {
        const QColor color(QLatin1String(c.hex));
        QAction *act = menu->addAction(QIcon(colorSwatch(color).copy(0, 0, 20, 20)), tr(c.name));
        connect(act, &QAction::triggered, this, [this, color]() { setPenColor(color); });
    }
    m_colorBtn->setMenu(menu);

    m_widthSlider = new QSlider(Qt::Horizontal);
    m_widthSlider->setObjectName(QStringLiteral("SWidth"));
    m_widthSlider->setRange(1, 6);
    m_widthSlider->setValue(2);
    m_widthSlider->setFixedWidth(180);
    m_widthLabel = new QLabel;
    m_widthLabel->setFixedWidth(40);
    connect(m_widthSlider, &QSlider::valueChanged, this, [this](int px) {
        m_pad->setPenWidth(px);
        m_widthLabel->setText(tr("%1 px").arg(px));
    });
    m_widthLabel->setText(tr("%1 px").arg(m_widthSlider->value()));

    hl->addWidget(new QLabel(tr("Color")));
    hl->addWidget(m_colorBtn);
    hl->addSpacing(14);
    hl->addWidget(new QLabel(tr("Line width")));
    hl->addWidget(m_widthSlider);
    hl->addWidget(m_widthLabel);
    hl->addStretch(1);
    return row;
}

QWidget *SignDialog::buildFooter()
{
    auto *footer = new QWidget;
    footer->setObjectName(QStringLiteral("SFooter"));
    footer->setFixedHeight(68);
    auto *hl = new QHBoxLayout(footer);
    hl->setContentsMargins(20, 0, 20, 0);
    hl->setSpacing(10);

    auto *cancel = new QPushButton(tr("Cancel"));
    cancel->setObjectName(QStringLiteral("SCancel"));
    cancel->setFixedSize(116, 40);
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);

    m_saveBtn = new QPushButton(tr("Save"));
    m_saveBtn->setObjectName(QStringLiteral("SCancel"));
    m_saveBtn->setFixedSize(116, 40);
    connect(m_saveBtn, &QPushButton::clicked, this, &SignDialog::onSave);

    m_placeBtn = new QPushButton(tr("Place"));
    m_placeBtn->setObjectName(QStringLiteral("SPrimary"));
    m_placeBtn->setFixedSize(116, 40);
    m_placeBtn->setDefault(true);
    connect(m_placeBtn, &QPushButton::clicked, this, &QDialog::accept);

    for (QPushButton *btn : { cancel, m_saveBtn, m_placeBtn })
        btn->setCursor(Qt::PointingHandCursor);
    hl->addStretch(1);
    hl->addWidget(cancel);
    hl->addWidget(m_saveBtn);
    hl->addWidget(m_placeBtn);
    return footer;
}

void SignDialog::setPenColor(const QColor &color)
{
    m_penColor = color;
    m_colorBtn->setIcon(QIcon(colorSwatch(color)));
    m_pad->setPenColor(color);
    m_typedEdit->setStyleSheet(QStringLiteral("QLineEdit#STyped { color: %1; }")
                                   .arg(color.name()));
}

void SignDialog::onModeChanged(int mode)
{
    m_canvas->setCurrentIndex(mode);
    m_clearBtn->setVisible(mode != Saved);
    m_penRow->setVisible(mode != Saved);
    m_infoRow->setVisible(mode != Saved);
    m_colorBtn->setEnabled(mode == Draw || mode == Type);
    m_widthSlider->setEnabled(mode == Draw);
    if (mode == Type) m_typedEdit->setFocus();
    updateButtons();
}

void SignDialog::onClear()
{
    switch (m_modeGroup->checkedId()) {
    case Draw: m_pad->clear(); break;
    case Type: m_typedEdit->clear(); break;
    case Import:
        m_image = QImage();
        m_imageLabel->clear();
        m_imageLabel->hide();
        m_chooseImage->show();
        break;
    default: break;
    }
    updateButtons();
}

void SignDialog::onSave()
{
    QString name;
    switch (m_modeGroup->checkedId()) {
    case Type:   name = m_typedEdit->text().trimmed(); break;
    case Import: name = tr("%1 (Image)").arg(QFileInfo(m_imagePath).completeBaseName()); break;
    default:     name = tr("Signature %1").arg(SignatureStore::list().size() + 1); break;
    }
    const QString path = SignatureStore::save(signatureImage(), name);
    if (path.isEmpty()) {
        QMessageBox::warning(this, windowTitle(), tr("Could not save the signature."));
        return;
    }
    m_saved->refresh(path);
    m_modeGroup->button(Saved)->setChecked(true);
    onModeChanged(Saved);
}

void SignDialog::onChooseImage()
{
    QStringList patterns;
    for (const QByteArray &fmt : QImageReader::supportedImageFormats())
        patterns << QStringLiteral("*.") + QString::fromLatin1(fmt);
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Import signature image"), QString(),
        tr("Images (%1)").arg(patterns.join(QLatin1Char(' '))));
    if (path.isEmpty()) return;

    QImage img(path);
    if (img.isNull()) return;
    m_image = img;
    m_imagePath = path;
    m_imageLabel->setPixmap(QPixmap::fromImage(
        img.scaled(600, 100, Qt::KeepAspectRatio, Qt::SmoothTransformation)));
    m_imageLabel->show();
    m_chooseImage->hide();
    updateButtons();
}

void SignDialog::updateButtons()
{
    if (!m_placeBtn || !m_modeGroup) return;
    const bool handwritten = m_tabBar->currentIndex() == 0;
    if (!handwritten) {
        m_saveBtn->setVisible(false);
        m_placeBtn->setText(m_digital->isInvisible() ? tr("Sign") : tr("Place"));
        m_placeBtn->setEnabled(m_digital->isReady());
        return;
    }
    m_placeBtn->setText(tr("Place"));

    bool hasSignature = false;
    switch (m_modeGroup->checkedId()) {
    case Draw:   hasSignature = !m_pad->isEmpty(); break;
    case Type:   hasSignature = !m_typedEdit->text().trimmed().isEmpty(); break;
    case Import: hasSignature = !m_image.isNull(); break;
    case Saved:  hasSignature = !m_saved->currentPath().isEmpty(); break;
    default: break;
    }

    m_saveBtn->setVisible(handwritten);
    m_saveBtn->setEnabled(hasSignature && m_modeGroup->checkedId() != Saved);
    m_placeBtn->setEnabled(handwritten && hasSignature);
}

void SignDialog::applyDialogStyle()
{
    setStyleSheet(QStringLiteral(R"css(
        QDialog { background: palette(window); }
        QFrame#SCard {
            border: 1px solid palette(mid);
            border-radius: 10px;
            background: palette(base);
        }
        QFrame#SModeBar {
            border: 1px solid palette(mid);
            border-radius: 8px;
            background: palette(window);
        }
        QPushButton#SMode {
            border: none;
            border-right: 1px solid palette(mid);
            border-bottom: 3px solid transparent;
            background: transparent;
            color: palette(text);
            font-size: 15px;
        }
        QPushButton#SMode[edge="left"] {
            border-top-left-radius: 8px;
            border-bottom-left-radius: 8px;
        }
        QPushButton#SMode[edge="right"] {
            border-right: none;
            border-top-right-radius: 8px;
            border-bottom-right-radius: 8px;
        }
        QPushButton#SMode:checked {
            background: rgba(37, 99, 235, 0.12);
            border-bottom-color: #2563EB;
            color: #2563EB;
            font-weight: 600;
        }
        QPushButton#SMode:hover:!checked { background: palette(alternate-base); }
        QFrame#SPadFrame {
            border: 1px dashed palette(mid);
            border-radius: 8px;
            background: palette(base);
        }
        QLineEdit#STyped { border: none; background: transparent; }
        QScrollArea#SSavedScroll, QWidget#SSavedContent { background: transparent; }
        QScrollArea#SSavedScroll QScrollBar:vertical {
            width: 8px;
            margin: 0 0 0 4px;
            background: transparent;
        }
        QScrollArea#SSavedScroll QScrollBar::handle:vertical {
            min-height: 30px;
            border-radius: 2px;
            background: palette(mid);
        }
        QScrollArea#SSavedScroll QScrollBar::add-line:vertical,
        QScrollArea#SSavedScroll QScrollBar::sub-line:vertical { height: 0; }
        QScrollArea#SSavedScroll QScrollBar::add-page:vertical,
        QScrollArea#SSavedScroll QScrollBar::sub-page:vertical { background: transparent; }

        QLabel#SCardName { font-size: 14px; font-weight: 600; }
        QToolButton#SCardMenu { border: none; border-radius: 4px; background: transparent; }
        QToolButton#SCardMenu:hover { background: palette(alternate-base); }
        QToolButton#SCardMenu::menu-indicator { image: none; width: 0; }
        QPushButton#SClear {
            border: 1px solid palette(mid);
            border-radius: 6px;
            padding: 4px 10px;
            margin: 8px;
            background: palette(base);
            color: palette(text);
            font-size: 12px;
        }
        QPushButton#SClear:hover { background: palette(alternate-base); }
        QToolButton#SColor {
            border: 1px solid palette(mid);
            border-radius: 6px;
            padding: 4px 6px;
            background: palette(base);
        }
        QToolButton#SColor::menu-indicator { image: none; width: 0; }
        QSlider#SWidth::groove:horizontal {
            height: 4px;
            background: palette(mid);
            border-radius: 2px;
        }
        QSlider#SWidth::sub-page:horizontal { background: #2563EB; border-radius: 2px; }
        QSlider#SWidth::handle:horizontal {
            background: #2563EB;
            width: 16px;
            margin: -6px 0;
            border-radius: 8px;
        }
        QSlider#SWidth::sub-page:horizontal:disabled,
        QSlider#SWidth::handle:horizontal:disabled { background: palette(mid); }
        QLabel#SMuted { color: palette(placeholder-text); }
        QLabel#SField { font-weight: 500; }
        QLineEdit#SInput, QComboBox#SInput {
            border: 1px solid palette(mid);
            border-radius: 6px;
            padding: 0 10px;
            min-height: 36px;
            background: palette(base);
        }
        QLineEdit#SInput:focus, QComboBox#SInput:focus { border-color: #3B82F6; }
        QLineEdit#SInput:disabled, QComboBox#SInput:disabled { color: palette(placeholder-text); }
        QComboBox#SInput::drop-down { border: none; width: 24px; }
        QPushButton#SIconBtn {
            border: 1px solid palette(mid);
            border-radius: 6px;
            background: palette(base);
        }
        QPushButton#SIconBtn:hover { background: palette(alternate-base); }
        QFrame#SDivider { background: palette(mid); border: none; }
        QPushButton#SAdvHeader { border: none; background: transparent; text-align: left; }
        QLabel#SAdvTitle { font-size: 15px; font-weight: 600; }
        QFrame#SAdvCard {
            border: 1px solid palette(mid);
            border-radius: 8px;
            background: palette(base);
        }
        QPushButton#SAppearance {
            border: 1px solid palette(mid);
            border-radius: 6px;
            padding: 0 14px;
            background: palette(base);
            color: palette(text);
            text-align: left;
        }
        QPushButton#SAppearance:hover { background: palette(alternate-base); }
        QPushButton#SAppearance:disabled { color: palette(placeholder-text); }
        QPushButton#SDanger {
            border: 1px solid rgba(220, 38, 38, 0.35);
            border-radius: 6px;
            padding: 0 14px;
            background: palette(base);
            color: #DC2626;
            font-weight: 600;
        }
        QPushButton#SDanger:hover { background: rgba(220, 38, 38, 0.08); }
        QFrame#SChip { background: rgba(37, 99, 235, 0.08); border-radius: 8px; }
        QFrame#SLogoField {
            border: 1px solid palette(mid);
            border-radius: 6px;
            background: palette(base);
        }
        QLabel#SLogoIcon {
            border-right: 1px solid palette(mid);
            border-top-left-radius: 6px;
            border-bottom-left-radius: 6px;
            background: palette(alternate-base);
        }
        QLineEdit#SLogoName { border: none; padding: 0 12px; background: transparent; }
        QWidget#SFooter { border-top: 1px solid palette(mid); }
        QPushButton#SCancel {
            border: 1px solid palette(mid);
            border-radius: 6px;
            background: palette(base);
            color: palette(text);
            font-weight: 600;
        }
        QPushButton#SCancel:hover { background: palette(alternate-base); }
        QPushButton#SCancel:disabled {
            background: palette(window);
            color: palette(placeholder-text);
        }
        QPushButton#SSecondary {
            border: 1px solid rgba(37, 99, 235, 0.25);
            border-radius: 6px;
            padding: 0 14px;
            background: rgba(37, 99, 235, 0.10);
            color: #2563EB;
            font-weight: 600;
        }
        QPushButton#SSecondary:hover { background: rgba(37, 99, 235, 0.18); }
        QPushButton#SSecondary:disabled {
            border-color: palette(mid);
            background: palette(alternate-base);
            color: palette(placeholder-text);
        }
        QPushButton#SPrimary {
            border: none;
            border-radius: 6px;
            background: #2563EB;
            color: white;
            font-weight: 700;
        }
        QPushButton#SPrimary:hover   { background: #1D4ED8; }
        QPushButton#SPrimary:pressed { background: #1E40AF; }
        QPushButton#SPrimary:disabled {
            background: rgba(37, 99, 235, 0.35);
            color: rgba(255, 255, 255, 0.75);
        }
    )css"));
}
