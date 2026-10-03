#include "ui/sign/SignatureAppearanceDialog.hpp"
#include "ui/sign/SignOption.hpp"
#include "ui/theme/Theme.hpp"

#include <QComboBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QImageReader>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

namespace {

constexpr QSize kPreviewSize(460, 138);
constexpr int   kMaxLogoDim = 800;

QLabel *fieldLabel(const QString &text)
{
    auto *label = new QLabel(text);
    label->setObjectName(QStringLiteral("SField"));
    return label;
}

QFrame *divider()
{
    auto *line = new QFrame;
    line->setObjectName(QStringLiteral("SDivider"));
    line->setFixedHeight(1);
    return line;
}

}

QStringList SignatureAppearanceDialog::reasons()
{
    return { tr("Document approved"), tr("I am the author of this document"),
             tr("I have reviewed this document"), tr("I agree to the terms") };
}

SignatureAppearanceDialog::SignatureAppearanceDialog(const SignatureAppearance &appearance,
                                                     const QString &profileName,
                                                     const QString &defaultName, bool isNew,
                                                     QWidget *parent)
    : QDialog(parent)
    , m_defaultName(defaultName)
    , m_logo(appearance.logo)
    , m_logoName(appearance.logoName)
{
    setWindowTitle(isNew ? tr("New profile - OpenPDF Studio") : tr("Edit profile - OpenPDF Studio"));
    setModal(true);
    setMinimumWidth(860);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(24, 20, 24, 20);
    root->setSpacing(14);

    m_profileName = new QLineEdit(profileName);
    m_profileName->setObjectName(QStringLiteral("SInput"));
    m_profileName->setPlaceholderText(tr("Profile name"));
    auto *profileRow = new QGridLayout;
    profileRow->setHorizontalSpacing(16);
    profileRow->setVerticalSpacing(6);
    profileRow->addWidget(fieldLabel(tr("Profile name")), 0, 0);
    profileRow->addWidget(m_profileName, 1, 0);
    for (int c = 0; c < 3; ++c) profileRow->setColumnStretch(c, 1);
    root->addLayout(profileRow);
    root->addWidget(buildFields(appearance));
    root->addWidget(divider());
    root->addWidget(fieldLabel(tr("Show in signature")));
    root->addWidget(buildOptions(appearance));
    root->addWidget(buildLogoRow());
    root->addWidget(divider());
    root->addWidget(fieldLabel(tr("Preview")));
    root->addWidget(buildPreview());
    root->addWidget(buildButtons());
    updatePreview();
    adjustSize();
    setFixedSize(size());
    setWindowFlag(Qt::WindowMaximizeButtonHint, false);
}

QWidget *SignatureAppearanceDialog::buildFields(const SignatureAppearance &appearance)
{
    m_name = new QLineEdit(appearance.name);
    m_name->setObjectName(QStringLiteral("SInput"));
    m_name->setPlaceholderText(m_defaultName.isEmpty() ? tr("Your name") : m_defaultName);
    m_reason = new QComboBox;
    m_reason->setObjectName(QStringLiteral("SInput"));
    m_reason->addItems(reasons());
    if (!appearance.reason.isEmpty()) m_reason->setCurrentText(appearance.reason);
    m_location = new QLineEdit(appearance.location);
    m_location->setObjectName(QStringLiteral("SInput"));
    m_location->setPlaceholderText(tr("City"));

    connect(m_name, &QLineEdit::textChanged, this, &SignatureAppearanceDialog::updatePreview);
    connect(m_location, &QLineEdit::textChanged, this, &SignatureAppearanceDialog::updatePreview);
    connect(m_reason, &QComboBox::currentIndexChanged, this, &SignatureAppearanceDialog::updatePreview);

    auto *box = new QWidget;
    auto *grid = new QGridLayout(box);
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setHorizontalSpacing(16);
    grid->setVerticalSpacing(6);
    grid->addWidget(fieldLabel(tr("Name")), 0, 0);
    grid->addWidget(fieldLabel(tr("Reason")), 0, 1);
    grid->addWidget(fieldLabel(tr("Location")), 0, 2);
    grid->addWidget(m_name, 1, 0);
    grid->addWidget(m_reason, 1, 1);
    grid->addWidget(m_location, 1, 2);
    for (int c = 0; c < 3; ++c) grid->setColumnStretch(c, 1);
    return box;
}

QWidget *SignatureAppearanceDialog::buildOptions(const SignatureAppearance &appearance)
{
    auto *row = new QWidget;
    auto *hl = new QHBoxLayout(row);
    hl->setContentsMargins(0, 0, 0, 0);
    hl->setSpacing(10);

    auto chip = [this, hl](const QString &title, bool checked) {
        auto *frame = new QFrame;
        frame->setObjectName(QStringLiteral("SChip"));
        auto *fl = new QHBoxLayout(frame);
        fl->setContentsMargins(12, 10, 14, 10);
        auto *option = new SignOption(title, QString());
        option->setChecked(checked);
        connect(option, &SignOption::toggled, this, &SignatureAppearanceDialog::updatePreview);
        fl->addWidget(option);
        frame->setMinimumWidth(frame->sizeHint().width());
        hl->addWidget(frame, 1);
        return option;
    };
    m_showName     = chip(tr("Show signer name"), appearance.showName);
    m_showDate     = chip(tr("Show date"), appearance.showDate);
    m_showReason   = chip(tr("Show reason"), appearance.showReason);
    m_showLocation = chip(tr("Show location"), appearance.showLocation);
    m_showLogo     = chip(tr("Show PNG logo"), appearance.showLogo && !m_logo.isNull());
    m_showLogo->setEnabled(!m_logo.isNull());
    return row;
}

QWidget *SignatureAppearanceDialog::buildLogoRow()
{
    auto *row = new QWidget;
    auto *hl = new QHBoxLayout(row);
    hl->setContentsMargins(0, 0, 0, 0);
    hl->setSpacing(16);

    auto *field = new QFrame;
    field->setObjectName(QStringLiteral("SLogoField"));
    field->setFixedHeight(40);
    auto *fl = new QHBoxLayout(field);
    fl->setContentsMargins(0, 0, 0, 0);
    fl->setSpacing(0);
    auto *icon = new QLabel;
    icon->setObjectName(QStringLiteral("SLogoIcon"));
    icon->setFixedWidth(46);
    icon->setAlignment(Qt::AlignCenter);
    icon->setPixmap(Theme::renderSvg(QStringLiteral("image"), Theme::IconNormal, 20));
    m_logoField = new QLineEdit(m_logoName);
    m_logoField->setObjectName(QStringLiteral("SLogoName"));
    m_logoField->setReadOnly(true);
    m_logoField->setFocusPolicy(Qt::NoFocus);
    m_logoField->setPlaceholderText(tr("No logo chosen"));
    fl->addWidget(icon);
    fl->addWidget(m_logoField, 1);

    auto *choose = new QPushButton(tr("Choose PNG..."));
    choose->setObjectName(QStringLiteral("SCancel"));
    choose->setCursor(Qt::PointingHandCursor);
    choose->setFixedHeight(40);
    choose->setMinimumWidth(150);
    connect(choose, &QPushButton::clicked, this, &SignatureAppearanceDialog::chooseLogo);

    hl->addWidget(fieldLabel(tr("PNG logo")));
    hl->addWidget(field, 1);
    hl->addWidget(choose);
    return row;
}

QWidget *SignatureAppearanceDialog::buildPreview()
{
    auto *frame = new QFrame;
    frame->setObjectName(QStringLiteral("SPadFrame"));
    auto *fl = new QVBoxLayout(frame);
    fl->setContentsMargins(16, 16, 16, 16);
    m_preview = new QLabel;
    m_preview->setAlignment(Qt::AlignCenter);
    m_preview->setMinimumHeight(kPreviewSize.height() + 20);
    // The stamp lands on a white page, also in dark mode.
    m_preview->setStyleSheet(QStringLiteral("background: white; border-radius: 6px;"));
    fl->addWidget(m_preview);
    return frame;
}

QWidget *SignatureAppearanceDialog::buildButtons()
{
    auto *row = new QWidget;
    auto *hl = new QHBoxLayout(row);
    hl->setContentsMargins(0, 4, 0, 0);
    hl->addStretch(1);
    auto *cancel = new QPushButton(tr("Cancel"));
    cancel->setObjectName(QStringLiteral("SCancel"));
    auto *apply = new QPushButton(tr("Apply"));
    apply->setObjectName(QStringLiteral("SPrimary"));
    apply->setDefault(true);
    for (QPushButton *btn : { cancel, apply }) {
        btn->setFixedSize(116, 40);
        btn->setCursor(Qt::PointingHandCursor);
        hl->addWidget(btn);
    }
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    connect(apply, &QPushButton::clicked, this, &QDialog::accept);
    return row;
}

QString SignatureAppearanceDialog::profileName() const
{
    return m_profileName->text().trimmed();
}

void SignatureAppearanceDialog::chooseLogo()
{
    const QString path = QFileDialog::getOpenFileName(this, tr("Choose logo"), QString(),
                                                      tr("PNG images (*.png)"));
    if (path.isEmpty()) return;
    QImageReader reader(path);
    const QSize size = reader.size();
    if (size.width() > kMaxLogoDim || size.height() > kMaxLogoDim)
        reader.setScaledSize(size.scaled(kMaxLogoDim, kMaxLogoDim, Qt::KeepAspectRatio));
    const QImage logo = reader.read();
    if (logo.isNull()) return;

    m_logo = logo;
    m_logoName = QFileInfo(path).fileName();
    m_logoField->setText(m_logoName);
    m_showLogo->setEnabled(true);
    m_showLogo->setChecked(true);
    updatePreview();
}

SignatureAppearance SignatureAppearanceDialog::appearance() const
{
    SignatureAppearance a;
    a.name         = m_name->text().trimmed();
    a.reason       = m_reason->currentText();
    a.location     = m_location->text().trimmed();
    a.logo         = m_logo;
    a.logoName     = m_logoName;
    a.showName     = m_showName->isChecked();
    a.showDate     = m_showDate->isChecked();
    a.showReason   = m_showReason->isChecked();
    a.showLocation = m_showLocation->isChecked();
    a.showLogo     = m_showLogo->isChecked();
    return a;
}

void SignatureAppearanceDialog::updatePreview()
{
    if (!m_preview || !m_showLogo) return;
    SignatureAppearance a = appearance();
    if (a.name.isEmpty()) a.name = m_defaultName;
    const qreal dpr = devicePixelRatioF();
    QPixmap px = QPixmap::fromImage(a.render(QDateTime::currentDateTime())
                                        .scaled(kPreviewSize * dpr, Qt::KeepAspectRatio,
                                                Qt::SmoothTransformation));
    px.setDevicePixelRatio(dpr);
    m_preview->setPixmap(px);
}
