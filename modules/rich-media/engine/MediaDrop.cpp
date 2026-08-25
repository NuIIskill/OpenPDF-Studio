// SPDX-License-Identifier: LicenseRef-OpenPDF-Business
#include "rich-media/engine/MediaDrop.hpp"

#include "rich-media/engine/MediaFormat.hpp"
#include "rich-media/engine/MediaSession.hpp"
#include "rich-media/engine/MediaSpec.hpp"
#include "rich-media/engine/PosterFrame.hpp"
#include "rich-media/engine/RichMediaWriter.hpp"

#include "app/PdfPwStore.hpp"
#include "app/SessionStore.hpp"

#include <QDebug>
#include <QFile>
#include <QFileInfo>
#include <QStringList>

namespace {

// The band a video page may land in. The upper bound is measured: the page
// Acrobat wrote in Binder1.pdf is 418 pt wide, well under A4. Without it a
// 1920-wide video would open a page of 922 pt.
constexpr double kMinLongestSidePt = 288.0;   // 4 inch
constexpr double kMaxLongestSidePt = 420.0;   // just under 6 inch

} // namespace

bool MediaDrop::isVideoFile(const QString &path)
{
    static const QStringList kSuffixes {
        QStringLiteral("mp4"),  QStringLiteral("m4v"),  QStringLiteral("mov"),
        QStringLiteral("webm"), QStringLiteral("mkv"),  QStringLiteral("avi"),
        QStringLiteral("mpg"),  QStringLiteral("mpeg"), QStringLiteral("wmv"),
        QStringLiteral("flv"),  QStringLiteral("m2ts"), QStringLiteral("ts"),
    };
    return kSuffixes.contains(QFileInfo(path).suffix().toLower());
}

QSizeF MediaDrop::pageSizeFor(int videoWidth, int videoHeight)
{
    if (videoWidth <= 0 || videoHeight <= 0) return QSizeF(640.0, 360.0);

    // 150 dpi, the same arithmetic Acrobat does.
    QSizeF size(videoWidth * 72.0 / 150.0, videoHeight * 72.0 / 150.0);
    const double longest = qMax(size.width(), size.height());
    if (longest < kMinLongestSidePt)      size *= kMinLongestSidePt / longest;
    else if (longest > kMaxLongestSidePt) size *= kMaxLongestSidePt / longest;
    return size;
}

QString MediaDrop::displayNameFor(const QString &original, const QString &actual)
{
    const QFileInfo source(original.isEmpty() ? actual : original);
    if (original.isEmpty() || original == actual) return source.fileName();
    // Converted: keep the original stem, take the extension from what is
    // really inside.
    return source.completeBaseName() + QLatin1Char('.')
         + QFileInfo(actual).suffix().toLower();
}

QString MediaDrop::addAsOwnPage(const QString &pdfPath, const QString &source,
                                int afterPage, const QString &displayName)
{
    if (pdfPath.isEmpty() || source.isEmpty()) return {};
    if (!RichMediaWriter::available()) return {};
    if (!QFileInfo::exists(pdfPath) || !QFileInfo::exists(source)) return {};

    const MediaFormat::Info info = MediaFormat::inspect(source);

    // The still is needed anyway, and for containers this reads only by
    // signature it is also the only place the real dimensions come from.
    const QImage poster = PosterFrame::grab(source, 1280);
    const QSize  pixels = info.size.isValid() ? info.size : poster.size();
    const QSizeF pageSize = pageSizeFor(pixels.width(), pixels.height());

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

    const QString work = SessionStore::newWorkingFile(pdfPath);
    if (work.isEmpty()) return {};
    QFile::remove(work);
    if (!QFile::copy(pdfPath, work)) {
        qWarning() << "[rich-media] cannot create working copy:" << work;
        return {};
    }
    // The copy inherits the original's encryption, hence its password.
    PdfPwStore::set(work, PdfPwStore::get(pdfPath));

    MediaSession once;
    once.addInsert(spec);
    if (!RichMediaWriter::apply(work, once, PdfPwStore::get(pdfPath))) {
        QFile::remove(work);
        return {};
    }
    return work;
}
