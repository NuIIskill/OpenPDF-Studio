#include "engine/import/DocumentImport.hpp"

#include "engine/import/DocumentBuilder.hpp"
#include "engine/import/DocxReader.hpp"
#include "engine/import/ImageImport.hpp"
#include "engine/import/OdtReader.hpp"
#include "engine/import/TextDocumentPdf.hpp"
#include "engine/import/ZipArchive.hpp"

#include <QCoreApplication>
#include <QDebug>
#include <QFile>
#include <QFileInfo>
#include <QTextDocument>

namespace {

QString tr(const char *text) { return QCoreApplication::translate("DocumentImport", text); }

bool isOfficeSuffix(const QString &suffix)
{
    return suffix == QLatin1String("docx") || suffix == QLatin1String("odt");
}

/// An old .doc renamed to .docx is the most common way this goes wrong, and the
/// signature is the only honest way to tell the user what they actually have.
bool isLegacyOleFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return false;
    return file.read(8) == QByteArray("\xD0\xCF\x11\xE0\xA1\xB1\x1A\xE1", 8);
}

}

bool DocumentImport::isPdf(const QString &path)
{
    if (path.endsWith(QLatin1String(".pdf"), Qt::CaseInsensitive)) return true;

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return false;
    // The header may sit behind a little junk, which the format allows.
    return file.read(1024).contains("%PDF-");
}

bool DocumentImport::isSupported(const QString &path)
{
    if (ImageImport::isSupported(path)) return true;
    return ZipArchive::available()
        && isOfficeSuffix(QFileInfo(path).suffix().toLower());
}

QString DocumentImport::formatName(const QString &path)
{
    const QString suffix = QFileInfo(path).suffix().toLower();
    if (suffix == QLatin1String("docx")) return tr("Word document");
    if (suffix == QLatin1String("odt"))  return tr("OpenDocument text");
    if (ImageImport::isSupported(path))  return tr("Image");
    return suffix.toUpper();
}

QString DocumentImport::openFilter()
{
    QStringList all = { QStringLiteral("*.pdf") };
    QStringList parts;
    parts += tr("PDF files") + QStringLiteral(" (*.pdf)");

    if (ZipArchive::available()) {
        all   += { QStringLiteral("*.docx"), QStringLiteral("*.odt") };
        parts += tr("Word documents") + QStringLiteral(" (*.docx)");
        parts += tr("OpenDocument text") + QStringLiteral(" (*.odt)");
    }
    for (const QString &suffix : ImageImport::suffixes())
        all += QStringLiteral("*.") + suffix;
    parts += ImageImport::nameFilter();

    parts.prepend(tr("All supported files") + QStringLiteral(" (")
                  + all.join(QLatin1Char(' ')) + QLatin1Char(')'));
    parts += tr("All files") + QStringLiteral(" (*)");
    return parts.join(QStringLiteral(";;"));
}

bool DocumentImport::convertToPdf(const QString &path, const QString &outPdf,
                                  QString *error, const QString &textDumpPath)
{

    if (!QFileInfo::exists(path)) {
        if (error) *error = tr("The file does not exist.");
        return false;
    }

    if (ImageImport::isSupported(path))
        return ImageImport::toPdf(path, outPdf, error);

    const QString suffix = QFileInfo(path).suffix().toLower();
    if (isOfficeSuffix(suffix)) {
        if (!ZipArchive::available()) {
            if (error) *error = tr("This build was made without zlib and cannot "
                                   "read Word or OpenDocument files.");
            return false;
        }
        if (isLegacyOleFile(path)) {
            if (error) *error = tr("This is an older Word file that only carries "
                                   "a .docx name. Save it as a real .docx first.");
            return false;
        }

        ZipArchive zip;
        QString reason;
        if (!zip.open(path, &reason)) {
            qWarning().noquote() << "[import]" << path << reason;
            if (error) *error = tr("The file could not be read. It may be damaged.");
            return false;
        }

        DocumentBuilder builder;
        const bool read = suffix == QLatin1String("docx")
            ? DocxReader::read(zip, builder, error)
            : OdtReader::read(zip, builder, error);
        if (!read) return false;

        if (!textDumpPath.isEmpty()) {
            QFile dump(textDumpPath);
            if (dump.open(QIODevice::WriteOnly | QIODevice::Truncate))
                dump.write(builder.document()->toPlainText().toUtf8());
        }
        return TextDocumentPdf::write(builder.document(), builder.pageLayout(),
                                      outPdf, error, builder.decorations());
    }

    if (error) *error = tr("%1 files cannot be opened.").arg(suffix.toUpper());
    return false;
}
