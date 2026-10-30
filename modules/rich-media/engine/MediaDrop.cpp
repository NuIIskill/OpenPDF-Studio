// SPDX-License-Identifier: LicenseRef-OpenPDF-Business
#include "rich-media/engine/MediaDrop.hpp"

#include "rich-media/engine/MediaFormat.hpp"
#include "rich-media/engine/MediaSession.hpp"
#include "rich-media/engine/MediaSpec.hpp"
#include "rich-media/engine/PosterFrame.hpp"
#include "rich-media/engine/RichMediaWriter.hpp"

#include "app/PdfPwStore.hpp"

#include <QDebug>
#include <QFile>
#include <QFileInfo>
#include <QMimeDatabase>
#include <QStringList>

namespace {

constexpr double kMinLongestSidePt = 288.0;
constexpr double kMaxLongestSidePt = 420.0;

}

const QStringList &MediaDrop::videoSuffixes()
{
    static const QStringList kSuffixes {
        QStringLiteral("mp4"),  QStringLiteral("m4v"),  QStringLiteral("mov"),
        QStringLiteral("webm"), QStringLiteral("mkv"),  QStringLiteral("avi"),
        QStringLiteral("mpg"),  QStringLiteral("mpeg"), QStringLiteral("wmv"),
        QStringLiteral("flv"),  QStringLiteral("m2ts"), QStringLiteral("ts"),
    };
    return kSuffixes;
}

bool MediaDrop::isVideoFile(const QString &path)
{
    return videoSuffixes().contains(QFileInfo(path).suffix().toLower());
}

QSizeF MediaDrop::pageSizeFor(int videoWidth, int videoHeight)
{
    if (videoWidth <= 0 || videoHeight <= 0) return QSizeF(640.0, 360.0);

    QSizeF size(videoWidth * 72.0 / 150.0, videoHeight * 72.0 / 150.0);
    const double longest = qMax(size.width(), size.height());
    if (longest < kMinLongestSidePt)      size *= kMinLongestSidePt / longest;
    else if (longest > kMaxLongestSidePt) size *= kMaxLongestSidePt / longest;
    return size;
}

QRectF MediaDrop::placementBoundsFor(const QSizeF &pageSize,
                                     const QSize &videoPixels,
                                     const QPointF &dropPoint)
{
    if (pageSize.isEmpty()) return {};

    const qreal aspect = videoPixels.width() > 0 && videoPixels.height() > 0
        ? qreal(videoPixels.width()) / qreal(videoPixels.height())
        : 16.0 / 9.0;
    QSizeF size(pageSize.width() * 0.42, pageSize.width() * 0.42 / aspect);
    const qreal maxHeight = pageSize.height() * 0.35;
    if (size.height() > maxHeight)
        size = QSizeF(maxHeight * aspect, maxHeight);
    size.setWidth(qMin(size.width(), pageSize.width()));
    size.setHeight(qMin(size.height(), pageSize.height()));

    QPointF topLeft = dropPoint - QPointF(size.width() / 2.0,
                                          size.height() / 2.0);
    topLeft.setX(qBound(0.0, topLeft.x(), pageSize.width() - size.width()));
    topLeft.setY(qBound(0.0, topLeft.y(), pageSize.height() - size.height()));
    return QRectF(topLeft, size);
}

QString MediaDrop::mimeTypeFor(const QString &path)
{
    static QMimeDatabase database;
    const QString mime = database.mimeTypeForFile(path).name();
    if (!mime.isEmpty() && mime != QLatin1String("application/octet-stream"))
        return mime;

    const QString suffix = QFileInfo(path).suffix().toLower();
    if (suffix == QLatin1String("mp4") || suffix == QLatin1String("m4v"))
        return QStringLiteral("video/mp4");
    if (suffix == QLatin1String("webm")) return QStringLiteral("video/webm");
    if (suffix == QLatin1String("mov")) return QStringLiteral("video/quicktime");
    if (suffix == QLatin1String("mp3")) return QStringLiteral("audio/mpeg");
    return QStringLiteral("application/octet-stream");
}

QString MediaDrop::displayNameFor(const QString &original, const QString &actual)
{
    const QFileInfo source(original.isEmpty() ? actual : original);
    if (original.isEmpty() || original == actual) return source.fileName();

    return source.completeBaseName() + QLatin1Char('.')
         + QFileInfo(actual).suffix().toLower();
}

namespace {

MediaSpec ownPageSpec(const QString &source, int afterPage, const QString &displayName)
{
    const MediaFormat::Info info = MediaFormat::inspect(source);

    const QImage poster = PosterFrame::grab(source, 1280);
    const QSize  pixels = info.size.isValid() ? info.size : poster.size();
    const QSizeF pageSize = MediaDrop::pageSizeFor(pixels.width(), pixels.height());

    MediaSpec spec;
    spec.type        = MediaSpec::Type::Video;
    spec.source      = source;
    spec.displayName = displayName.isEmpty() ? QFileInfo(source).fileName()
                                             : displayName;
    spec.mimeType    = QStringLiteral("video/mp4");
    spec.ownPage     = true;
    spec.pageSizePt  = pageSize;
    spec.page        = qMax(0, afterPage);
    spec.poster      = poster;
    if (spec.poster.isNull())
        spec.poster = PosterFrame::placeholder(pageSize.toSize());
    return spec;
}

}

bool MediaDrop::addAsOwnPage(const QString &pdfPath, const QString &source, int afterPage,
                             const WriteGrant &work, const QString &displayName)
{
    if (pdfPath.isEmpty() || source.isEmpty() || work.path().isEmpty()) return false;
    if (!RichMediaWriter::available()) return false;
    if (!QFileInfo::exists(pdfPath) || !QFileInfo::exists(source)) return false;

    const MediaSpec spec = ownPageSpec(source, afterPage, displayName);

    QFile::remove(work.path());
    if (!QFile::copy(pdfPath, work.path())) {
        qWarning() << "[rich-media] cannot create working copy:" << work.path();
        return false;
    }

    PdfPwStore::set(work.path(), PdfPwStore::get(pdfPath));

    MediaSession once;
    once.addInsert(spec);
    if (!RichMediaWriter::apply(work, once, PdfPwStore::get(pdfPath))) {
        QFile::remove(work.path());
        return false;
    }
    return true;
}

bool MediaDrop::newDocument(const QString &source, const WriteGrant &outPdf,
                            const QString &displayName)
{
    if (source.isEmpty() || outPdf.path().isEmpty() || !QFileInfo::exists(source)) return false;
    if (!RichMediaWriter::available() || !RichMediaWriter::writeEmpty(outPdf)) return false;

    MediaSession once;
    once.addInsert(ownPageSpec(source, 0, displayName));
    return RichMediaWriter::apply(outPdf, once);
}
