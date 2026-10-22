#include "ui/protect/ProtectDialog.hpp"

#include "app/PdfPwStore.hpp"
#include "ui/DocumentView.hpp"
#include "ui/theme/Theme.hpp"
#include "ui/view/DocumentInfoLoader.hpp"
#include "ui/widgets/CheckBoxStyle.hpp"
#include "ui/widgets/IconTabBar.hpp"

#include <QAction>
#include <QCheckBox>
#include <QComboBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QStackedWidget>
#include <QVBoxLayout>

namespace {

QLabel *label(const QString &text, const char *objectName = nullptr)
{
    auto *l = new QLabel(text);
    if (objectName) l->setObjectName(QLatin1String(objectName));
    l->setWordWrap(true);
    return l;
}

QFrame *note(const QString &icon, const QColor &tint, QLabel *text)
{
    auto *box = new QFrame;
    box->setObjectName(QStringLiteral("PNote"));
    auto *row = new QHBoxLayout(box);
    row->setContentsMargins(16, 10, 16, 10);
    row->setSpacing(14);
    auto *glyph = new QLabel;
    glyph->setPixmap(Theme::renderSvg(icon, tint, 24, box->devicePixelRatioF()));
    row->addWidget(glyph, 0, Qt::AlignVCenter);
    row->addWidget(text, 1);
    return box;
}

QFrame *divider()
{
    auto *line = new QFrame;
    line->setObjectName(QStringLiteral("PDivider"));
    line->setFixedHeight(1);
    return line;
}

QString cipherText(int index)
{
    switch (index) {
    case 0:  return ProtectDialog::tr("AES-256 encryption provides strong protection and is "
                                      "supported by all modern PDF readers.");
    case 1:  return ProtectDialog::tr("AES-128 also opens in older readers, but is weaker "
                                      "than AES-256.");
    default: return ProtectDialog::tr("RC4 is outdated and can be broken. Use it only for "
                                      "very old PDF readers.");
    }
}

bool sameProtection(const PdfProtection &a, const PdfProtection &b)
{
    const DocumentInfo::Permissions &p = a.permissions, &q = b.permissions;
    return a.encrypt == b.encrypt && a.userPassword == b.userPassword && a.cipher == b.cipher
        && p.print == q.print && p.printHighRes == q.printHighRes && p.modify == q.modify
        && p.copy == q.copy && p.annotate == q.annotate && p.fillForms == q.fillForms
        && p.assemble == q.assemble && p.accessibility == q.accessibility;
}

PdfProtection::Cipher cipherOf(const QString &name)
{
    if (name.startsWith(QLatin1String("AES-128"))) return PdfProtection::Cipher::Aes128;
    if (name.startsWith(QLatin1String("RC4")))     return PdfProtection::Cipher::Rc4;
    return PdfProtection::Cipher::Aes256;
}

}

void ProtectDialog::run(DocumentView *view, QWidget *parent)
{
    if (!view || view->pageCount() <= 0) {
        ProtectDialog(PdfProtection{}, false, 0, false, parent).exec();
        return;
    }
    if (!ProtectionWriter::available()) {
        QMessageBox::information(parent, tr("Protect"),
            tr("Protecting a PDF needs qpdf, which this build does not include."));
        return;
    }

    const DocumentInfo &info = view->documentInfo()->info();
    const std::optional<PdfProtection> &pending = view->pendingProtection();
    PdfProtection current;
    if (pending) {
        current = *pending;
    } else {
        current.encrypt     = info.encrypted;
        current.cipher      = cipherOf(info.encryption);
        current.permissions = info.permissions;
        if (info.passwordUsed) current.userPassword = PdfPwStore::get(view->contentFile());
    }
    const bool isProtected = pending ? pending->encrypt : info.encrypted;

    ProtectDialog dlg(current, isProtected, info.signatureCount(), true, parent);
    if (dlg.exec() != QDialog::Accepted) return;
    const PdfProtection result = dlg.protection();
    if (!pending && (result.encrypt ? sameProtection(result, current) : !info.encrypted)) return;
    view->setProtection(result);
}

ProtectDialog::ProtectDialog(const PdfProtection &current, bool isProtected, int signatures,
                             bool hasDocument, QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Protect - OpenPDF Studio"));
    setModal(true);
    setFixedSize(780, 700);
    applyDialogStyle();

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    auto *body = new QVBoxLayout;
    body->setContentsMargins(20, 20, 20, 12);
    body->setSpacing(12);

    auto *card = new QFrame;
    card->setObjectName(QStringLiteral("PCard"));
    auto *cardLayout = new QVBoxLayout(card);
    cardLayout->setContentsMargins(0, 0, 0, 0);
    cardLayout->setSpacing(0);

    m_tabBar = new IconTabBar;
    m_tabBar->addIconTab(QStringLiteral("lock"), tr("Password & Encryption"));
    m_tabBar->addIconTab(QStringLiteral("shield-check"), tr("Permissions"));
    cardLayout->addWidget(m_tabBar);

    m_pages = new QStackedWidget;
    m_pages->addWidget(buildPasswordPage(isProtected, hasDocument));
    m_pages->addWidget(buildPermissionsPage());
    cardLayout->addWidget(m_pages, 1);
    connect(m_tabBar, &QTabBar::currentChanged, m_pages, &QStackedWidget::setCurrentIndex);
    body->addWidget(card, 1);

    if (signatures > 0) {
        body->addWidget(note(QStringLiteral("shield-alert"), Theme::IconMuted,
            label(tr("Saving with changed protection rewrites the file, so its digital "
                     "signatures will no longer be valid."), "PMuted")));
    }
    root->addLayout(body, 1);
    root->addWidget(buildFooter(hasDocument));

    auto *checkStyle = new CheckBoxStyle;
    checkStyle->setParent(this);
    for (QCheckBox *box : findChildren<QCheckBox *>()) {
        box->setStyle(checkStyle);
        box->setCursor(Qt::PointingHandCursor);
    }

    fill(current);
    updateStates();
}

QWidget *ProtectDialog::buildBanner(bool isProtected, bool hasDocument)
{
    auto *banner = new QFrame;
    banner->setObjectName(QStringLiteral("PBanner"));
    auto *row = new QHBoxLayout(banner);
    row->setContentsMargins(18, 10, 14, 10);
    row->setSpacing(16);
    auto *shield = new QLabel;
    shield->setPixmap(Theme::renderSvg(isProtected ? QStringLiteral("shield-check")
                                                   : QStringLiteral("shield-alert"),
                                       isProtected ? Theme::Success : Theme::IconMuted, 30,
                                       devicePixelRatioF()));
    row->addWidget(shield);
    auto *texts = new QVBoxLayout;
    texts->setSpacing(2);
    texts->addWidget(label(!hasDocument ? tr("No document is open.")
                           : isProtected ? tr("This document is currently protected.")
                                         : tr("This document is not protected."),
                           "PBannerTitle"));
    texts->addWidget(label(!hasDocument ? tr("Open a PDF to protect it.")
                           : isProtected
                               ? tr("You can change the password and encryption settings below.")
                               : tr("Set a password below, or restrict what readers may do."),
                           "PMuted"));
    row->addLayout(texts, 1);
    if (isProtected) {
        auto *remove = new QPushButton(tr("Remove protection"));
        remove->setObjectName(QStringLiteral("PCancel"));
        remove->setIcon(Theme::makeIcon(QStringLiteral("trash-2"), Theme::IconNormal,
                                        Theme::IconNormal, Theme::IconDisabled, 18));
        remove->setFixedHeight(38);
        remove->setCursor(Qt::PointingHandCursor);
        connect(remove, &QPushButton::clicked, this, [this]() {
            m_remove = true;
            accept();
        });
        row->addWidget(remove);
    }
    return banner;
}

QWidget *ProtectDialog::buildPasswordPage(bool isProtected, bool hasDocument)
{
    auto *content = new QWidget;
    content->setObjectName(QStringLiteral("PPage"));
    auto *vl = new QVBoxLayout(content);
    vl->setContentsMargins(20, 16, 20, 16);
    vl->setSpacing(8);
    vl->addWidget(buildBanner(isProtected, hasDocument));
    vl->addSpacing(4);

    addSection(vl, tr("Password"), tr("Require a password to open this PDF."));
    m_passwordChk = new QCheckBox(tr("Enable password protection"));
    vl->addWidget(m_passwordChk);
    auto *grid = new QGridLayout;
    grid->setHorizontalSpacing(24);
    grid->setVerticalSpacing(6);
    m_password = passwordField(tr("Password"));
    m_confirm  = passwordField(tr("Confirm password"));
    grid->addWidget(label(tr("Password"), "PField"), 0, 0);
    grid->addWidget(label(tr("Confirm password"), "PField"), 0, 1);
    grid->addWidget(m_password, 1, 0);
    grid->addWidget(m_confirm, 1, 1);
    vl->addLayout(grid);
    vl->addSpacing(4);
    vl->addWidget(divider());

    addSection(vl, tr("Encryption"), tr("Choose the encryption level for the PDF."));
    vl->addWidget(label(tr("Encryption level"), "PField"));
    m_cipher = new QComboBox;
    m_cipher->setObjectName(QStringLiteral("PInput"));
    m_cipher->addItems({ tr("AES-256 (Recommended)"), tr("AES-128"),
                         tr("RC4 128-bit (Legacy)") });
    m_cipher->setMaximumWidth(360);
    vl->addWidget(m_cipher);
    m_cipherNote = label(QString(), "PMuted");
    vl->addWidget(note(QStringLiteral("shield-check"), Theme::Primary, m_cipherNote));
    vl->addStretch(1);

    connect(m_passwordChk, &QCheckBox::toggled, this, &ProtectDialog::updateStates);
    connect(m_cipher, &QComboBox::currentIndexChanged, this, &ProtectDialog::updateStates);

    auto *scroll = new QScrollArea;
    scroll->setObjectName(QStringLiteral("PScroll"));
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setWidget(content);
    return scroll;
}

QWidget *ProtectDialog::buildPermissionsPage()
{
    auto *page = new QWidget;
    page->setObjectName(QStringLiteral("PPage"));
    auto *vl = new QVBoxLayout(page);
    vl->setContentsMargins(20, 20, 20, 20);
    vl->setSpacing(10);

    addSection(vl, tr("Permissions"), tr("Choose what readers may do with the PDF."));
    m_restrictChk = new QCheckBox(tr("Restrict printing, editing and copying"));
    vl->addWidget(m_restrictChk);

    auto *grid = new QGridLayout;
    grid->setHorizontalSpacing(24);
    grid->setVerticalSpacing(6);
    m_owner        = passwordField(tr("Permissions password"));
    m_ownerConfirm = passwordField(tr("Confirm password"));
    grid->addWidget(label(tr("Permissions password"), "PField"), 0, 0);
    grid->addWidget(label(tr("Confirm password"), "PField"), 0, 1);
    grid->addWidget(m_owner, 1, 0);
    grid->addWidget(m_ownerConfirm, 1, 1);
    vl->addLayout(grid);
    vl->addWidget(label(tr("Needed to lift the restrictions later. If you leave it empty, "
                           "nobody can lift them."), "PMuted"));
    vl->addSpacing(8);
    vl->addWidget(divider());
    vl->addSpacing(4);

    vl->addWidget(label(tr("Allowed actions"), "PSectionTitle"));
    auto *allow = new QGridLayout;
    allow->setHorizontalSpacing(24);
    allow->setVerticalSpacing(10);
    const QStringList actions {
        tr("Printing"), tr("High-quality printing"), tr("Changing content"),
        tr("Copying text and images"), tr("Adding comments"), tr("Filling in forms"),
        tr("Inserting, deleting and rotating pages"), tr("Reading aloud for accessibility"),
    };
    for (int i = 0; i < actions.size(); ++i) {
        auto *box = new QCheckBox(actions.at(i));
        m_allow.append(box);
        allow->addWidget(box, i / 2, i % 2);
        connect(box, &QCheckBox::toggled, this, &ProtectDialog::updateStates);
    }
    vl->addLayout(allow);
    vl->addSpacing(6);
    vl->addWidget(note(QStringLiteral("info"), Theme::Primary,
        label(tr("Most PDF readers respect these restrictions, but they are not a strong "
                 "protection on their own. Use an open password for confidential "
                 "documents."), "PMuted")));
    vl->addStretch(1);

    connect(m_restrictChk, &QCheckBox::toggled, this, &ProtectDialog::updateStates);
    return page;
}

QWidget *ProtectDialog::buildFooter(bool hasDocument)
{
    auto *footer = new QWidget;
    footer->setObjectName(QStringLiteral("PFooter"));
    footer->setFixedHeight(72);
    auto *hl = new QHBoxLayout(footer);
    hl->setContentsMargins(24, 0, 24, 0);
    hl->setSpacing(12);
    hl->addWidget(label(tr("Changes are applied when you save the document."), "PMuted"), 1);
    auto *cancel = new QPushButton(tr("Cancel"));
    cancel->setObjectName(QStringLiteral("PCancel"));
    cancel->setFixedSize(130, 42);
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    hl->addWidget(cancel);
    auto *apply = new QPushButton(tr("Apply"));
    apply->setObjectName(QStringLiteral("PPrimary"));
    apply->setFixedSize(170, 42);
    apply->setDefault(true);
    apply->setEnabled(hasDocument);
    connect(apply, &QPushButton::clicked, this, &ProtectDialog::onApply);
    hl->addWidget(apply);
    return footer;
}

QLineEdit *ProtectDialog::passwordField(const QString &placeholder)
{
    auto *edit = new QLineEdit;
    edit->setObjectName(QStringLiteral("PInput"));
    edit->setEchoMode(QLineEdit::Password);
    edit->setPlaceholderText(placeholder);
    QAction *eye = edit->addAction(Theme::makeIcon(QStringLiteral("eye-off"), Theme::IconMuted,
                                                   Theme::IconMuted, Theme::IconDisabled, 18),
                                   QLineEdit::TrailingPosition);
    eye->setToolTip(tr("Show password"));
    connect(eye, &QAction::triggered, edit, [edit, eye]() {
        const bool hidden = edit->echoMode() == QLineEdit::Password;
        edit->setEchoMode(hidden ? QLineEdit::Normal : QLineEdit::Password);
        eye->setIcon(Theme::makeIcon(hidden ? QStringLiteral("eye") : QStringLiteral("eye-off"),
                                     Theme::IconMuted, Theme::IconMuted, Theme::IconDisabled,
                                     18));
        eye->setToolTip(hidden ? tr("Hide password") : tr("Show password"));
    });
    return edit;
}

void ProtectDialog::addSection(QVBoxLayout *layout, const QString &title,
                               const QString &subtitle)
{
    layout->addWidget(label(title, "PSectionTitle"));
    layout->addWidget(label(subtitle, "PMuted"));
}

void ProtectDialog::fill(const PdfProtection &current)
{
    const bool hasPassword = current.encrypt && !current.userPassword.isEmpty();
    m_passwordChk->setChecked(hasPassword);
    m_password->setText(current.userPassword);
    m_confirm->setText(current.userPassword);
    m_cipher->setCurrentIndex(int(current.cipher));

    const DocumentInfo::Permissions &p = current.permissions;
    const bool values[] = { p.print, p.printHighRes, p.modify, p.copy,
                            p.annotate, p.fillForms, p.assemble, p.accessibility };
    bool restricted = false;
    for (int i = 0; i < m_allow.size(); ++i) {
        m_allow.at(i)->setChecked(values[i]);
        restricted = restricted || !values[i];
    }
    m_restrictChk->setChecked(current.encrypt && restricted);
    if (current.ownerPassword != current.userPassword) {
        m_owner->setText(current.ownerPassword);
        m_ownerConfirm->setText(current.ownerPassword);
    }
}

void ProtectDialog::updateStates()
{
    const bool password = m_passwordChk->isChecked();
    m_password->setEnabled(password);
    m_confirm->setEnabled(password);
    m_cipherNote->setText(cipherText(m_cipher->currentIndex()));

    const bool restrict = m_restrictChk->isChecked();
    m_owner->setEnabled(restrict);
    m_ownerConfirm->setEnabled(restrict);
    for (QCheckBox *box : std::as_const(m_allow)) box->setEnabled(restrict);
    m_allow.at(1)->setEnabled(restrict && m_allow.at(0)->isChecked());
    if (!m_allow.at(0)->isChecked()) m_allow.at(1)->setChecked(false);
}

void ProtectDialog::onApply()
{
    if (m_passwordChk->isChecked()) {
        if (m_password->text().isEmpty()) {
            QMessageBox::warning(this, tr("Missing password"),
                tr("Please enter a password, or switch password protection off."));
            return;
        }
        if (m_password->text() != m_confirm->text()) {
            QMessageBox::warning(this, tr("Passwords do not match"),
                tr("The password and its confirmation are different."));
            return;
        }
    }
    if (m_restrictChk->isChecked()) {
        if (m_owner->text() != m_ownerConfirm->text()) {
            QMessageBox::warning(this, tr("Passwords do not match"),
                tr("The permissions password and its confirmation are different."));
            return;
        }
        if (m_passwordChk->isChecked() && !m_owner->text().isEmpty()
                && m_owner->text() == m_password->text()) {
            QMessageBox::warning(this, tr("Same password twice"),
                tr("The permissions password must differ from the open password, otherwise "
                   "everyone who can open the PDF can lift the restrictions."));
            return;
        }
    }
    accept();
}

PdfProtection ProtectDialog::protection() const
{
    PdfProtection result;
    const bool password = m_passwordChk->isChecked();
    const bool restrict = m_restrictChk->isChecked();
    result.encrypt = !m_remove && (password || restrict);
    if (!result.encrypt) return result;

    result.userPassword    = password ? m_password->text() : QString();
    result.ownerPassword   = restrict ? m_owner->text() : result.userPassword;
    result.cipher          = PdfProtection::Cipher(m_cipher->currentIndex());
    if (restrict) {
        DocumentInfo::Permissions &p = result.permissions;
        bool *fields[] = { &p.print, &p.printHighRes, &p.modify, &p.copy,
                           &p.annotate, &p.fillForms, &p.assemble, &p.accessibility };
        for (int i = 0; i < m_allow.size(); ++i) *fields[i] = m_allow.at(i)->isChecked();
        p.printHighRes = p.printHighRes && p.print;
    }
    return result;
}

void ProtectDialog::applyDialogStyle()
{
    setStyleSheet(QStringLiteral(R"css(
        QDialog { background: palette(window); }
        QFrame#PCard {
            border: 1px solid palette(mid);
            border-radius: 10px;
            background: palette(base);
        }
        QWidget#PPage, QScrollArea#PScroll { background: palette(base); }
        QFrame#PBanner, QFrame#PNote {
            border: 1px solid palette(mid);
            border-radius: 8px;
            background: palette(window);
        }
        QLabel#PBannerTitle { font-size: 16px; font-weight: 600; }
        QLabel#PSectionTitle { font-size: 17px; font-weight: 600; }
        QLabel#PField { font-weight: 500; }
        QLabel#PMuted { color: palette(placeholder-text); }
        QFrame#PDivider { background: palette(mid); border: none; }
        QLineEdit#PInput, QComboBox#PInput {
            border: 1px solid palette(mid);
            border-radius: 6px;
            padding: 0 10px;
            min-height: 36px;
            background: palette(base);
        }
        QLineEdit#PInput:focus, QComboBox#PInput:focus { border-color: #3B82F6; }
        QLineEdit#PInput:disabled, QComboBox#PInput:disabled { color: palette(placeholder-text); }
        QComboBox#PInput::drop-down { border: none; width: 28px; }
        QWidget#PFooter { border-top: 1px solid palette(mid); }
        QPushButton#PCancel {
            border: 1px solid palette(mid);
            border-radius: 6px;
            padding: 0 14px;
            background: palette(base);
            color: palette(text);
            font-weight: 600;
        }
        QPushButton#PCancel:hover { background: palette(alternate-base); }
        QPushButton#PPrimary {
            border: none;
            border-radius: 6px;
            background: #2563EB;
            color: white;
            font-weight: 700;
        }
        QPushButton#PPrimary:hover   { background: #1D4ED8; }
        QPushButton#PPrimary:pressed { background: #1E40AF; }
        QPushButton#PPrimary:disabled {
            background: rgba(37, 99, 235, 0.35);
            color: rgba(255, 255, 255, 0.75);
        }
    )css"));
}
