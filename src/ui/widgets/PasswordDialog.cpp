#include "ui/widgets/PasswordDialog.hpp"

#include <QDialogButtonBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

PasswordDialog::PasswordDialog(const QString &fileName, bool retry, QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Password required"));
    setModal(true);

    setSizeGripEnabled(false);
    setWindowFlag(Qt::MSWindowsFixedSizeDialogHint, true);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(20, 18, 20, 16);
    root->setSpacing(12);
    root->setSizeConstraint(QLayout::SetFixedSize);

    auto *message = new QLabel(
        retry ? tr("Wrong password. Please try again:")
              : tr("\"%1\" is password protected.\n"
                   "Enter the password to open it:").arg(fileName));
    message->setObjectName(QStringLiteral("PwMessage"));
    message->setWordWrap(true);
    message->setMinimumWidth(320);
    root->addWidget(message);

    m_edit = new QLineEdit;
    m_edit->setObjectName(QStringLiteral("PwInput"));
    m_edit->setEchoMode(QLineEdit::Password);
    m_edit->setPlaceholderText(tr("Password"));
    m_edit->setMinimumHeight(30);
    root->addWidget(m_edit);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("Open"));
    buttons->button(QDialogButtonBox::Ok)->setDefault(true);
    root->addWidget(buttons);

    setStyleSheet(QStringLiteral(R"css(
        QLabel#PwMessage { font-size: 13px; }
        QLineEdit#PwInput {
            border: 1px solid palette(dark);
            border-radius: 6px;
            padding: 4px 10px;
            background: palette(base);
            color: palette(text);
            font-size: 13px;
        }
        QLineEdit#PwInput:focus { border: 2px solid #2563EB; padding: 3px 9px; }
    )css"));

    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    connect(m_edit, &QLineEdit::returnPressed, this, &QDialog::accept);

    m_edit->setFocus();
}

QString PasswordDialog::password() const
{
    return m_edit ? m_edit->text() : QString{};
}
