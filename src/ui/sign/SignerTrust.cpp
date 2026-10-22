#include "ui/sign/SignerTrust.hpp"

#include "app/TrustedSigners.hpp"
#include "engine/sign/SignatureManager.hpp"
#include "engine/sign/SignatureVerifier.hpp"
#include "ui/theme/Theme.hpp"

#include <QDialog>
#include <QFile>
#include <QFileDialog>
#include <QFontDatabase>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLocale>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>

namespace {

constexpr qint64 kMaxCertificateBytes = 1 << 20;

struct Fact {
    QString key;
    QString value;
    bool    fixed { false };
};

QString dateText(const QDateTime &time)
{
    return time.isValid() ? QLocale().toString(time.toLocalTime().date(), QLocale::ShortFormat)
                          : QString();
}

QString fingerprintText(const QByteArray &fingerprint)
{
    const QString hex = QString::fromLatin1(fingerprint.toHex(':').toUpper());
    // 32 bytes in two rows of 16.
    return hex.size() == 95 ? hex.left(47) + QLatin1Char('\n') + hex.mid(48) : hex;
}

QList<Fact> facts(const QString &issuer, const QDateTime &validTo, const QByteArray &fingerprint)
{
    QString until = dateText(validTo);
    if (validTo.isValid() && validTo < QDateTime::currentDateTimeUtc())
        until = SignerTrust::tr("%1 (expired)").arg(until);
    return { { SignerTrust::tr("Issued by"), issuer },
             { SignerTrust::tr("Valid until"), until },
             { SignerTrust::tr("Fingerprint (SHA-256)"), fingerprintText(fingerprint), true } };
}

QString factsHtml(const QList<Fact> &list)
{
    QString html = QStringLiteral("<table cellspacing='0' cellpadding='2'>");
    for (const Fact &f : list) {
        if (f.value.isEmpty()) continue;
        QString value = f.value.toHtmlEscaped().replace(QLatin1Char('\n'), QStringLiteral("<br>"));
        if (f.fixed) value = QStringLiteral("<tt>%1</tt>").arg(value);
        html += QStringLiteral("<tr><td style='padding-right:12px'>%1</td><td>%2</td></tr>")
                    .arg(f.key.toHtmlEscaped(), value);
    }
    return html + QStringLiteral("</table>");
}

QLabel *textLabel(const QString &text, const char *objectName, QWidget *parent)
{
    auto *label = new QLabel(text, parent);
    label->setObjectName(QLatin1String(objectName));
    label->setWordWrap(true);
    return label;
}

bool confirm(const TrustedSigners::Entry &entry, QWidget *parent)
{
    const QString signer = entry.name.isEmpty() ? SignerTrust::tr("Unknown signer") : entry.name;
    QMessageBox box(parent);
    box.setWindowTitle(SignerTrust::tr("Trust signer"));
    box.setIcon(QMessageBox::Question);
    box.setText(SignerTrust::tr("Trust %1?").arg(signer.toHtmlEscaped()));
    box.setInformativeText(
        factsHtml(facts(entry.issuer, entry.validTo, entry.fingerprint))
        + QStringLiteral("<p>%1</p><p>%2</p>")
              .arg(SignerTrust::tr("Compare the fingerprint with the signer, for example by phone. "
                                   "Only trust a certificate whose origin you know.").toHtmlEscaped(),
                   SignerTrust::tr("Signatures made with this certificate then count as valid on "
                                   "this computer. Others who open the document still see the "
                                   "signer as not trusted.").toHtmlEscaped()));
    box.setTextFormat(Qt::RichText);
    QPushButton *trust = box.addButton(SignerTrust::tr("Trust"), QMessageBox::AcceptRole);
    box.addButton(QMessageBox::Cancel);
    box.setDefaultButton(QMessageBox::Cancel);
    box.exec();
    if (box.clickedButton() != trust) return false;

    if (!TrustedSigners::add(entry)) {
        QMessageBox::warning(parent, SignerTrust::tr("Trust signer"),
                             SignerTrust::tr("The signer could not be saved."));
        return false;
    }
    return true;
}

/// The list of trusted signers, each with a button that removes it.
class TrustedSignersDialog : public QDialog
{
public:
    explicit TrustedSignersDialog(QWidget *parent)
        : QDialog(parent)
    {
        setWindowTitle(SignerTrust::tr("Trusted signers"));
        setFixedWidth(560);
        auto *vl = new QVBoxLayout(this);
        vl->setContentsMargins(24, 20, 24, 16);
        vl->setSpacing(12);
        vl->addWidget(textLabel(SignerTrust::tr("Signatures by these signers count as valid on "
                                                "this computer, even when no certificate "
                                                "authority confirms who they are."),
                                "SMuted", this));

        auto *scroll = new QScrollArea(this);
        scroll->setFrameShape(QFrame::NoFrame);
        scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        scroll->setWidgetResizable(true);
        scroll->setMinimumHeight(220);
        auto *content = new QWidget;
        m_list = new QVBoxLayout(content);
        m_list->setContentsMargins(0, 0, 0, 0);
        m_list->setSpacing(12);
        scroll->setWidget(content);
        vl->addWidget(scroll, 1);

        auto *footer = new QHBoxLayout;
        auto *add = new QPushButton(SignerTrust::tr("Add certificate..."), this);
        add->setObjectName(QStringLiteral("SSecondary"));
        add->setFixedHeight(40);
        connect(add, &QPushButton::clicked, this, [this]() { addFromFile(); });
        auto *close = new QPushButton(SignerTrust::tr("Close"), this);
        close->setObjectName(QStringLiteral("SCancel"));
        close->setFixedSize(116, 40);
        connect(close, &QPushButton::clicked, this, &QDialog::accept);
        for (QPushButton *btn : { add, close })
            btn->setCursor(Qt::PointingHandCursor);
        footer->addWidget(add);
        footer->addStretch(1);
        footer->addWidget(close);
        vl->addLayout(footer);

        connect(TrustedSigners::instance(), &TrustedSigners::changed, this, [this]() { fill(); });
        fill();
    }

private:
    void addFromFile()
    {
        const QString path = QFileDialog::getOpenFileName(
            this, SignerTrust::tr("Choose a certificate"), QString(),
            SignerTrust::tr("Certificates (*.cer *.crt *.pem *.der);;All files (*)"));
        if (path.isEmpty()) return;
        QFile file(path);
        const CertificateFacts read = file.size() <= kMaxCertificateBytes
                                          && file.open(QIODevice::ReadOnly)
            ? SignatureManager().readCertificate(file.readAll())
            : CertificateFacts();
        if (read.fingerprint.isEmpty()) {
            QMessageBox::warning(this, SignerTrust::tr("Trusted signers"),
                                 SignerTrust::tr("The file is not a certificate."));
            return;
        }
        confirm({ read.fingerprint, read.subject, read.issuer, read.validTo, {} }, this);
    }

    void fill()
    {
        // Deleted later: a removal refills the list from its own button's click.
        while (QLayoutItem *item = m_list->takeAt(0)) {
            if (QWidget *w = item->widget()) {
                w->hide();
                w->deleteLater();
            }
            delete item;
        }
        QWidget *content = m_list->parentWidget();
        const QList<TrustedSigners::Entry> entries = TrustedSigners::list();
        if (entries.isEmpty())
            m_list->addWidget(textLabel(SignerTrust::tr("No trusted signers yet."),
                                        "SMuted", content));
        for (int i = 0; i < entries.size(); ++i) {
            if (i > 0) {
                auto *line = new QFrame(content);
                line->setObjectName(QStringLiteral("SDivider"));
                line->setFixedHeight(1);
                m_list->addWidget(line);
            }
            m_list->addWidget(row(entries[i], content));
        }
        m_list->addStretch(1);
    }

    QWidget *row(const TrustedSigners::Entry &entry, QWidget *parent)
    {
        auto *row = new QWidget(parent);
        auto *grid = new QGridLayout(row);
        grid->setContentsMargins(0, 0, 0, 0);
        grid->setHorizontalSpacing(12);
        grid->setVerticalSpacing(2);
        grid->setColumnStretch(1, 1);
        grid->addWidget(textLabel(entry.name.isEmpty() ? SignerTrust::tr("Unknown signer")
                                                       : entry.name,
                                  "SField", row), 0, 0, 1, 2);

        QList<Fact> list = facts(entry.issuer, entry.validTo, entry.fingerprint);
        list.append({ SignerTrust::tr("Trusted since"), dateText(entry.added) });
        for (const Fact &f : std::as_const(list)) {
            if (f.value.isEmpty()) continue;
            const int r = grid->rowCount();
            grid->addWidget(textLabel(f.key, "SMuted", row), r, 0, Qt::AlignTop);
            QLabel *value = textLabel(f.value, "SMuted", row);
            value->setTextInteractionFlags(Qt::TextSelectableByMouse);
            if (f.fixed) value->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
            grid->addWidget(value, r, 1, Qt::AlignTop);
        }

        auto *remove = new QPushButton(row);
        remove->setObjectName(QStringLiteral("SIconBtn"));
        remove->setFixedSize(36, 36);
        remove->setCursor(Qt::PointingHandCursor);
        remove->setToolTip(SignerTrust::tr("Remove"));
        remove->setIcon(Theme::makeIcon(QStringLiteral("trash-2"), Theme::IconNormal,
                                        Theme::IconNormal, Theme::IconDisabled, 16));
        remove->setIconSize(QSize(16, 16));
        const QByteArray fingerprint = entry.fingerprint;
        connect(remove, &QPushButton::clicked, this, [this, fingerprint]() {
            if (!TrustedSigners::remove(fingerprint))
                QMessageBox::warning(this, SignerTrust::tr("Trusted signers"),
                                     SignerTrust::tr("The signer could not be removed."));
        });
        grid->addWidget(remove, 0, 2, 3, 1, Qt::AlignTop);
        return row;
    }

    QVBoxLayout *m_list { nullptr };
};

}

bool SignerTrust::ask(const SignatureStatus &signature, QWidget *parent)
{
    if (signature.fingerprint.isEmpty()) return false;
    return confirm({ signature.fingerprint, signature.signer, signature.issuer,
                     signature.validTo, {} }, parent);
}

void SignerTrust::manage(QWidget *parent)
{
    TrustedSignersDialog dialog(parent);
    dialog.exec();
}
