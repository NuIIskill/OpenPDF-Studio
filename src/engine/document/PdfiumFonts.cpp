#include "engine/document/PdfiumFonts.hpp"

#if defined(HAVE_PDF_RENDERING) && defined(HAVE_PDFIUM)

#include "fpdf_edit.h"

#include <QByteArray>
#include <QCryptographicHash>
#include <QFontDatabase>
#include <QHash>

#include <vector>

namespace {

QHash<QByteArray, QString> &registry()
{
    static QHash<QByteArray, QString> cache;
    return cache;
}

} // namespace

QString PdfiumFonts::registerWithQt(FPDF_FONT font)
{
    if (!font || FPDFFont_GetIsEmbedded(font) != 1) return {};

    size_t size = 0;
    if (!FPDFFont_GetFontData(font, nullptr, 0, &size) || size == 0) return {};
    if (size > 32u * 1024 * 1024) return {};

    std::vector<uint8_t> buffer(size);
    size_t written = 0;
    if (!FPDFFont_GetFontData(font, buffer.data(), size, &written) || written == 0)
        return {};

    const QByteArray data(reinterpret_cast<const char *>(buffer.data()),
                          static_cast<qsizetype>(written));
    const QByteArray key = QCryptographicHash::hash(data, QCryptographicHash::Sha1);
    const auto known = registry().constFind(key);
    if (known != registry().constEnd()) return *known;

    const int id = QFontDatabase::addApplicationFontFromData(data);
    const QStringList families =
        id >= 0 ? QFontDatabase::applicationFontFamilies(id) : QStringList();
    const QString family = families.isEmpty() ? QString() : families.first();
    registry().insert(key, family);
    return family;
}

#endif
