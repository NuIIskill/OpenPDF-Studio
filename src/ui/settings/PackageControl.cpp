#include "ui/settings/PackageControl.hpp"

#include <QApplication>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>

PackageControl::PackageControl(QWidget *parent)
    : QObject(parent)
{
    m_button = new QPushButton(parent);
    m_button->setObjectName(QStringLiteral("SettingsActionBtn"));
    m_button->setCursor(Qt::PointingHandCursor);
    m_button->setFixedHeight(28);
    connect(m_button, &QPushButton::clicked, this, &PackageControl::act);

    m_status = new QLabel(parent);
    m_status->setObjectName(QStringLiteral("SettingsRowDesc"));
    m_status->setWordWrap(true);
    m_status->setTextInteractionFlags(Qt::TextSelectableByMouse);

    m_packages = new SystemPackages(this);
    connect(m_packages, &SystemPackages::finished, this, &PackageControl::finish);
}

void PackageControl::setTarget(const SystemPackages::Names &names,
                               std::function<bool()> installed)
{
    m_names     = SystemPackages::namesFor(names);
    m_installed = std::move(installed);
    refresh();
}

void PackageControl::refresh()
{
    if (m_packages->isBusy()) return;
    m_present = m_installed && m_installed();
    QString text = m_present ? tr("Installed") : tr("Not installed");

    const bool direct = !m_names.isEmpty() && SystemPackages::canChange();
    m_button->setVisible(direct);
    m_button->setEnabled(true);
    m_button->setText(m_present ? tr("Remove") : tr("Install"));
    if (!direct) {
        const QString command = SystemPackages::command(m_names, !m_present);
        if (!command.isEmpty())
            text += u'\n' + (m_present ? tr("Remove with: %1") : tr("Install with: %1"))
                                .arg(command);
        else if (!m_present)
            text += u'\n' + tr("Install it with your system's package manager.");
    }
    m_status->setText(text);
}

void PackageControl::act()
{
    if (!m_present) {
        m_button->setEnabled(false);
        m_status->setText(tr("Installing…"));
        m_packages->install(m_names);
        return;
    }

    QApplication::setOverrideCursor(Qt::WaitCursor);
    const QStringList dependents = SystemPackages::dependents(m_names);
    QApplication::restoreOverrideCursor();
    if (!dependents.isEmpty()) {
        QMessageBox ask(QMessageBox::Warning, tr("Remove"),
            tr("Other programs need this package and would be removed with it:\n\n%1\n\n"
               "Remove them too?").arg(dependents.join(u'\n')),
            QMessageBox::NoButton, m_button->window());
        QAbstractButton *removeAll = ask.addButton(tr("Remove All"), QMessageBox::DestructiveRole);
        ask.addButton(QMessageBox::Cancel);
        ask.setDefaultButton(QMessageBox::Cancel);
        ask.exec();
        if (ask.clickedButton() != removeAll) return;
    }
    m_button->setEnabled(false);
    m_status->setText(tr("Removing…"));
    m_packages->remove(m_names, !dependents.isEmpty());
}

void PackageControl::finish(bool ok, const QString &error)
{
    refresh();
    if (!ok) {
        m_status->setText(m_status->text() + u'\n' + tr("Not done: %1").arg(error)
                          + u'\n' + SystemPackages::command(m_names, !m_present));
    }
    Q_EMIT changed();
}
