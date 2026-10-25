#include "ui/settings/SettingsPanel.hpp"
#include "app/AppSettings.hpp"
#include "ui/settings/OptionCard.hpp"

#include <QFileDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

QWidget *SettingsPanel::buildMediaPage()
{
    auto *page = new QWidget(this);
    page->setObjectName(QStringLiteral("SettingsScrollContent"));
    auto *vbox = new QVBoxLayout(page);
    vbox->setContentsMargins(32, 28, 32, 32);
    vbox->setSpacing(0);

    auto *title = new QLabel(tr("Media Playback"), page);
    title->setObjectName(QStringLiteral("SettingsSectionTitle"));
    vbox->addWidget(title);
    vbox->addSpacing(6);

    auto *desc = new QLabel(tr("Choose how video embedded in a PDF is played."), page);
    desc->setObjectName(QStringLiteral("SettingsSectionDesc"));
    vbox->addWidget(desc);
    vbox->addSpacing(24);

    m_pendingMedia = m_settings ? m_settings->mediaPlayback() : QStringLiteral("inapp");

    auto *row = new QHBoxLayout;
    row->setSpacing(14);
    row->setAlignment(Qt::AlignLeft);

    struct M { const char *icon, *id, *title, *desc; };
    const M media[] = {
        { "play-circle", "inapp",  QT_TR_NOOP("In OpenPDF Studio"),
          QT_TR_NOOP("Plays on the page") },
        { "monitor",     "system", QT_TR_NOOP("System player"),
          QT_TR_NOOP("Opens in your video player") },
        { "folder-open", "custom", QT_TR_NOOP("Custom command"),
          QT_TR_NOOP("Run a player of your choice") },
    };
    for (const auto &m : media)
        row->addWidget(buildOptionCard(
            QLatin1String(m.icon), tr(m.title), tr(m.desc),
            QLatin1String(m.id), m_mediaCards, m_mediaIds, false));
    row->addStretch(1);
    vbox->addLayout(row);

#ifndef HAVE_QT_MULTIMEDIA

    vbox->addSpacing(12);
    auto *noPlayer = new QLabel(
        tr("This build has no built-in player. "
           "\"In OpenPDF Studio\" falls back to the system player."), page);
    noPlayer->setObjectName(QStringLiteral("SettingsSectionDesc"));
    noPlayer->setWordWrap(true);
    vbox->addWidget(noPlayer);
#endif

    vbox->addSpacing(20);
    m_customPlayerRow = new QWidget(page);
    auto *cmdLayout = new QHBoxLayout(m_customPlayerRow);
    cmdLayout->setContentsMargins(0, 0, 0, 0);
    cmdLayout->setSpacing(8);

    auto *cmdLabel = new QLabel(tr("Command:"), m_customPlayerRow);
    cmdLayout->addWidget(cmdLabel);

    m_customPlayerEdit = new QLineEdit(m_customPlayerRow);
    m_customPlayerEdit->setPlaceholderText(QStringLiteral("mpv --fs %1"));
    if (m_settings) m_customPlayerEdit->setText(m_settings->customPlayerCommand());
    m_customPlayerEdit->setToolTip(
        tr("%1 is replaced with the video file. Without it the file is appended."));
    cmdLayout->addWidget(m_customPlayerEdit, 1);

    auto *browse = new QPushButton(tr("Browse..."), m_customPlayerRow);
    connect(browse, &QPushButton::clicked, this, [this]() {
        const QString exe = QFileDialog::getOpenFileName(this, tr("Choose a media player"));
        if (!exe.isEmpty())
            m_customPlayerEdit->setText(QStringLiteral("%1 %2").arg(exe, QStringLiteral("%1")));
    });
    cmdLayout->addWidget(browse);
    vbox->addWidget(m_customPlayerRow);

    m_customPlayerRow->setEnabled(m_pendingMedia == QLatin1String("custom"));

    vbox->addStretch(1);

    selectCardGroup(m_pendingMedia, m_mediaCards, m_mediaIds);
    return page;
}
