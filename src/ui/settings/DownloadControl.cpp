#include "ui/settings/DownloadControl.hpp"
#include "app/FileDownload.hpp"

#include <QLabel>
#include <QPointer>
#include <QPushButton>

DownloadControl::DownloadControl(QWidget *parent)
    : QObject(parent)
{
    m_button = new QPushButton(parent);
    m_button->setObjectName(QStringLiteral("SettingsActionBtn"));
    m_button->setCursor(Qt::PointingHandCursor);
    m_button->setFixedHeight(28);
    connect(m_button, &QPushButton::clicked, this, &DownloadControl::act);

    m_status = new QLabel(parent);
    m_status->setObjectName(QStringLiteral("SettingsRowDesc"));
    m_status->setWordWrap(true);

    m_download = new FileDownload(this);
    connect(m_download, &FileDownload::progress, this, [this](int percent) {
        m_status->setText(tr("Downloading… %1 %").arg(percent));
    });
    connect(m_download, &FileDownload::finished, this, [this](bool ok, const QString &error) {
        if (!ok || !m_target.unpack) {
            finish(ok, error);
            return;
        }
        m_status->setText(tr("Unpacking…"));
        QPointer<DownloadControl> self(this);
        m_target.unpack([self](bool unpacked, const QString &why) {
            if (self) self->finish(unpacked, why);
        });
    });
}

void DownloadControl::setTarget(Target target)
{
    m_target = std::move(target);
    refresh();
}

void DownloadControl::refresh()
{
    if (m_busy) return;
    m_present = m_target.installed && m_target.installed();
    m_status->setText(!m_present ? tr("Not installed")
                      : m_target.describe ? m_target.describe() : tr("Installed"));
    m_button->setEnabled(true);
    m_button->setText(m_present ? tr("Remove") : tr("Download"));
}

void DownloadControl::act()
{
    m_busy = true;
    m_button->setEnabled(false);
    if (!m_present) {
        m_status->setText(tr("Downloading…"));
        if (m_target.prepare) m_target.prepare();
        m_download->start(m_target.url, m_target.sha256, m_target.file);
        return;
    }
    m_status->setText(tr("Removing…"));
    // The removal may wait for the UAC prompt; the page can close meanwhile.
    QPointer<DownloadControl> self(this);
    m_target.remove([self](bool ok, const QString &error) {
        if (self) self->finish(ok, error);
    });
}

void DownloadControl::finish(bool ok, const QString &error)
{
    m_busy = false;
    refresh();
    if (!ok)
        m_status->setText(m_status->text() + u'\n' + tr("Not done: %1").arg(error));
    Q_EMIT changed();
}
