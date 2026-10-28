// SPDX-License-Identifier: LicenseRef-OpenPDF-Business
#include "rich-media/ui/ConvertCard.hpp"

#include "rich-media/engine/MediaConvert.hpp"

#include "app/Elevation.hpp"
#include "ui/settings/DownloadControl.hpp"
#include "ui/settings/PackageControl.hpp"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QProcess>
#include <QPushButton>
#include <QVBoxLayout>

DownloadControl::Target ConvertCard::download()
{
    // Gyan's build of the same version, the one ffmpeg.org points to for
    // Windows; its tagged releases stay where they are. It is a GPL build with
    // Media Foundation, so h264_mf is there as in the shipped one.
    static const QString kFolder = QStringLiteral("ffmpeg-7.1.1-essentials_build");
    const QString dir = QFileInfo(MediaConvert::downloadedPath()).absolutePath();
    DownloadControl::Target target;
    target.url = QUrl(QStringLiteral("https://github.com/GyanD/codexffmpeg/releases/"
                                     "download/7.1.1/%1.zip").arg(kFolder));
    target.sha256 = "04861d3339c5ebe38b56c19a15cf2c0cc97f5de4fa8910e4d47e5e6404e4a2d4";
    target.file   = dir + u'/' + kFolder + QStringLiteral(".zip");
    target.unpack = [zip = target.file, dir](const DownloadControl::Done &done) {
        // Windows' own tar reads ZIP files; only ffmpeg.exe is taken out.
        auto *tar = new QProcess(QCoreApplication::instance());
        QObject::connect(tar, &QProcess::finished, tar, [tar, zip, done]() {
            tar->deleteLater();
            QFile::remove(zip);
            const QString exe = MediaConvert::downloadedPath();
            QFile file(exe);
            const bool ok = file.open(QIODevice::ReadOnly)
                && QCryptographicHash::hash(file.readAll(), QCryptographicHash::Sha256).toHex()
                       == "b90225987bdd042cca09a1efb5e34e9848f2d1dbf5fbcd388753a44145522997";
            file.close();
            if (!ok) QFile::remove(exe);
            done(ok, ok ? QString() : tr("The download could not be unpacked."));
        });
        QObject::connect(tar, &QProcess::errorOccurred, tar,
                         [tar, zip, done](QProcess::ProcessError error) {
            if (error != QProcess::FailedToStart) return;
            tar->deleteLater();
            QFile::remove(zip);
            done(false, tr("The download could not be unpacked."));
        });
        tar->start(qEnvironmentVariable("SystemRoot") + QStringLiteral("/System32/tar.exe"),
                   { QStringLiteral("-xf"), zip, QStringLiteral("-C"), dir,
                     QStringLiteral("--strip-components=2"),
                     kFolder + QStringLiteral("/bin/ffmpeg.exe") });
    };
    target.installed = []() {
        return QFileInfo::exists(MediaConvert::bundledPath())
            || QFileInfo::exists(MediaConvert::downloadedPath());
    };
    target.describe = []() {
        return QFileInfo::exists(MediaConvert::bundledPath()) ? tr("Included with the program")
                                                              : tr("Downloaded");
    };
    target.remove = [](const DownloadControl::Done &done) {
        QFile::remove(MediaConvert::downloadedPath());
        const QString bundled = MediaConvert::bundledPath();
        if (!QFileInfo::exists(bundled) || QFile::remove(bundled)) {
            done(true, {});
            return;
        }
        // Installed under Program Files it takes administrator rights.
        Elevation::runSelf({ QStringLiteral("--remove-app-file"), QStringLiteral("ffmpeg.exe") },
                           [done](Elevation::Result result) {
            done(result == Elevation::Result::Done,
                 result == Elevation::Result::Declined ? tr("No permission was given.")
                                                       : tr("The file could not be deleted."));
        });
    };
    return target;
}

void ConvertCard::build(QVBoxLayout *cl)
{
    QWidget *card = cl->parentWidget();

    auto *title = new QLabel(tr("Video Conversion"), card);
    title->setObjectName(QStringLiteral("SettingsCardTitle"));
    cl->addWidget(title);
    cl->addSpacing(4);

    auto *row = new QHBoxLayout;
    row->setSpacing(12);
    auto *text = new QVBoxLayout;
    text->setSpacing(2);
    auto *name = new QLabel(QStringLiteral("FFmpeg"), card);
    name->setObjectName(QStringLiteral("SettingsRowLabel"));
    QFont bold = name->font();
    bold.setBold(true);
    name->setFont(bold);
    text->addWidget(name);
    auto *desc = new QLabel(tr("Converts videos before they are embedded in a PDF."), card);
    desc->setObjectName(QStringLiteral("SettingsRowDesc"));
    desc->setWordWrap(true);
    text->addWidget(desc);

#ifdef Q_OS_WIN
    auto *control = new DownloadControl(card);
#else
    auto *control = new PackageControl(card);
#endif
    text->addWidget(control->status());
    row->addLayout(text, 1);
    row->addWidget(control->button(), 0, Qt::AlignVCenter);
    cl->addLayout(row);

#ifdef Q_OS_WIN
    control->setTarget(download());
#else
    control->setTarget({ { QStringLiteral("ffmpeg-free") },
                         { QStringLiteral("ffmpeg") },
                         { QStringLiteral("ffmpeg") } },
                       []() { return !MediaConvert::toolPath().isEmpty(); });
#endif
}
