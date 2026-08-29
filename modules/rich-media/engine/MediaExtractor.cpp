// SPDX-License-Identifier: LicenseRef-OpenPDF-Business
#include "rich-media/engine/MediaExtractor.hpp"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QStandardPaths>

#ifdef HAVE_QPDF
#  include "app/PdfPwStore.hpp"

#  include <qpdf/Pl_StdioFile.hh>
#  include <qpdf/QPDF.hh>
#  include <qpdf/QPDFObjectHandle.hh>

#  include <cstdio>
#  include <string>
#endif

namespace {

QString cacheRoot()
{
    const QString base = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
    return QDir(base).filePath(
        QStringLiteral("openpdf-media-%1").arg(QCoreApplication::applicationPid()));
}

QString cacheDir(const QString &pdfPath)
{
    const QByteArray key = QCryptographicHash::hash(
        QFileInfo(pdfPath).absoluteFilePath().toUtf8(), QCryptographicHash::Sha1);
    return QDir(cacheRoot()).filePath(QString::fromLatin1(key.toHex().left(16)));
}

QString cacheName(const MediaAsset &asset)
{
    QString safe = QFileInfo(asset.name).fileName();
    safe.remove(QRegularExpression(QStringLiteral("[^A-Za-z0-9._-]")));
    if (safe.isEmpty() || safe.startsWith(QLatin1Char('.')))
        safe.prepend(QStringLiteral("media"));
    return QStringLiteral("%1_%2").arg(asset.streamObject).arg(safe);
}

}

#ifdef HAVE_QPDF

QString MediaExtractor::extract(const QString &pdfPath, const MediaAsset &asset)
{
    if (!asset.isEmbedded() || pdfPath.isEmpty()) return {};

    QDir dir(cacheDir(pdfPath));
    if (!dir.exists() && !QDir().mkpath(dir.absolutePath())) {
        qWarning() << "[rich-media] cannot create cache dir:" << dir.absolutePath();
        return {};
    }
    const QString target = dir.filePath(cacheName(asset));
    if (QFileInfo::exists(target)) return target;

    const QString partial = target + QStringLiteral(".part");
    FILE *out = std::fopen(partial.toLocal8Bit().constData(), "wb");
    if (!out) {
        qWarning() << "[rich-media] cannot write:" << partial;
        return {};
    }

    bool ok = false;
    try {
        QPDF pdf;
        const std::string pw = PdfPwStore::forQpdf(pdfPath);
        pdf.processFile(pdfPath.toLocal8Bit().constData(),
                        pw.empty() ? nullptr : pw.c_str());

        QPDFObjectHandle stream =
            pdf.getObject(asset.streamObject, asset.streamGeneration);
        if (stream.isStream()) {
            Pl_StdioFile pipe("rich-media asset", out);
            bool filtered = false;

            ok = stream.pipeStreamData(&pipe, &filtered, 0, qpdf_dl_all);
        }
    } catch (const std::exception &e) {
        qWarning() << "[rich-media] extraction failed:" << e.what();
        ok = false;
    }

    std::fclose(out);
    if (!ok) {
        QFile::remove(partial);
        return {};
    }

    QFile::remove(target);
    if (!QFile::rename(partial, target)) {
        QFile::remove(partial);
        return {};
    }
    return target;
}

#else

QString MediaExtractor::extract(const QString &, const MediaAsset &)
{
    return {};
}

#endif

void MediaExtractor::clearCache(const QString &pdfPath)
{
    QDir dir(pdfPath.isEmpty() ? cacheRoot() : cacheDir(pdfPath));
    if (dir.exists()) dir.removeRecursively();
}
