#include "engine/document/PdfiumWriter.hpp"

#if defined(HAVE_PDF_RENDERING) && defined(HAVE_PDFIUM)

#include "app/PdfPwStore.hpp"
#include "engine/document/PdfiumEdits.hpp"
#include "engine/document/PdfiumFonts.hpp"
#include "engine/document/KeptText.hpp"
#include "engine/document/OriginalContent.hpp"
#include "engine/document/PdfiumLock.hpp"
#include "engine/edit/EditSession.hpp"

#include "fpdf_annot.h"
#include "fpdf_edit.h"
#include "fpdf_save.h"
#include "fpdfview.h"

#ifdef HAVE_QPDF
#  include <qpdf/QPDF.hh>
#  include <qpdf/QPDFWriter.hh>
#  include <qpdf/Constants.h>
#endif

#include <QDebug>
#include <QFile>
#include <QHash>
#include <QMap>
#include <QSet>
#include <QTransform>

#include <vector>

namespace {

struct FileSink {
    FPDF_FILEWRITE writer {};
    QFile          file;
    bool           failed { false };
    const std::function<bool()> *cancelled { nullptr };
};

int writeBlock(FPDF_FILEWRITE *self, const void *data, unsigned long size)
{
    auto *sink = reinterpret_cast<FileSink *>(self);
    if (*sink->cancelled && (*sink->cancelled)()) sink->failed = true;
    if (sink->failed) return 0;
    PdfiumLock::yieldToOthers();
    const qint64 written = sink->file.write(static_cast<const char *>(data),
                                            static_cast<qint64>(size));
    if (written != static_cast<qint64>(size)) {
        sink->failed = true;
        return 0;
    }
    return 1;
}

void pinColoursToRgb(FPDF_PAGE page)
{
    // A regenerated page keeps only DeviceRGB colours; ICC, CMYK and
    // separation colours would come out black.
    for (int i = 0, n = FPDFPage_CountObjects(page); i < n; ++i) {
        FPDF_PAGEOBJECT obj = FPDFPage_GetObject(page, i);
        const int type = FPDFPageObj_GetType(obj);
        if (type != FPDF_PAGEOBJ_TEXT && type != FPDF_PAGEOBJ_PATH) continue;
        unsigned int r = 0, g = 0, b = 0, a = 0;
        if (FPDFPageObj_GetFillColor(obj, &r, &g, &b, &a))
            FPDFPageObj_SetFillColor(obj, r, g, b, a);
        if (FPDFPageObj_GetStrokeColor(obj, &r, &g, &b, &a))
            FPDFPageObj_SetStrokeColor(obj, r, g, b, a);
    }
}

QByteArray escapePdfString(const QString &text)
{
    QByteArray out;
    for (const QChar &c : text) {
        const char ch = c.toLatin1();
        if (ch == '(' || ch == ')' || ch == '\\') out += '\\';
        out += ch ? ch : '?';
    }
    return out;
}

bool setFieldValue(FPDF_PAGE page, const QString &fieldName, const QString &value)
{
    const int count = FPDFPage_GetAnnotCount(page);
    for (int i = 0; i < count; ++i) {
        FPDF_ANNOTATION annot = FPDFPage_GetAnnot(page, i);
        if (!annot) continue;
        if (FPDFAnnot_GetSubtype(annot) != FPDF_ANNOT_WIDGET) {
            FPDFPage_CloseAnnot(annot);
            continue;
        }

        unsigned long bytes = FPDFAnnot_GetStringValue(annot, "T", nullptr, 0);
        QString name;
        if (bytes > 2) {
            std::vector<unsigned short> buffer(bytes / 2 + 1, 0);
            FPDFAnnot_GetStringValue(annot, "T", buffer.data(), bytes);
            name = QString::fromUtf16(reinterpret_cast<const char16_t *>(buffer.data()));
        }
        if (name != fieldName) { FPDFPage_CloseAnnot(annot); continue; }

        const std::u16string utf16 = value.toStdU16String();
        FPDFAnnot_SetStringValue(annot, "V",
                                 reinterpret_cast<FPDF_WIDESTRING>(utf16.c_str()));

        FS_RECTF rect {};
        FPDFAnnot_GetRect(annot, &rect);
        const double height = rect.top - rect.bottom;
        const double size   = qBound(6.0, height * 0.65, 24.0);

        const QByteArray ap = "/Tx BMC q BT /Helv " + QByteArray::number(size, 'f', 1)
                            + " Tf 0 g 2 " + QByteArray::number(height * 0.28, 'f', 1)
                            + " Td (" + escapePdfString(value) + ") Tj ET Q EMC";
        const std::u16string apUtf16 = QString::fromLatin1(ap).toStdU16String();
        FPDFAnnot_SetAP(annot, FPDF_ANNOT_APPEARANCEMODE_NORMAL,
                        reinterpret_cast<FPDF_WIDESTRING>(apUtf16.c_str()));

        FPDFPage_CloseAnnot(annot);
        return true;
    }
    return false;
}

bool isEncrypted(const QString &file, const QString &password)
{
    PdfiumLock lock;
    const QByteArray path = file.toUtf8();
    const QByteArray pw   = password.toUtf8();
    FPDF_DOCUMENT doc = FPDF_LoadDocument(path.constData(),
                                          pw.isEmpty() ? nullptr : pw.constData());
    if (!doc) return false;
    const bool encrypted = FPDF_GetSecurityHandlerRevision(doc) >= 0;
    FPDF_CloseDocument(doc);
    return encrypted;
}

bool reapplyEncryption(const QString &file, const QString &password)
{
#ifdef HAVE_QPDF
    if (password.isEmpty()) return true;
    const QString temp = file + QStringLiteral(".enc");
    try {
        QPDF pdf;
        pdf.processFile(file.toLocal8Bit().constData());
        {
            QPDFWriter writer(pdf, temp.toLocal8Bit().constData());
            writer.setCompressStreams(true);
            const std::string pass = password.toStdString();

            writer.setR6EncryptionParameters(
                pass.c_str(), pass.c_str(),
                  true,   true,   true,
                  true,   true,
                  true, qpdf_r3p_full,   true);
            writer.write();
        }
    } catch (const std::exception &ex) {
        qWarning() << "[PdfiumWriter] Verschlüsselung konnte nicht wieder angelegt"
                   << "werden:" << ex.what();
        QFile::remove(temp);
        return false;
    }
    if (!QFile::remove(file)) {
        qWarning() << "[PdfiumWriter] konnte" << file << "nicht ersetzen";
        QFile::remove(temp);
        return false;
    }
    if (!QFile::rename(temp, file)) {
        qWarning() << "[PdfiumWriter] konnte" << temp << "nicht nach" << file
                   << "umbenennen";
        return false;
    }
    return true;
#else

    Q_UNUSED(file)
    qWarning() << "[PdfiumWriter] ohne qpdf kann die Verschlüsselung nicht"
               << "wiederhergestellt werden, Speichern abgebrochen";
    return password.isEmpty();
#endif
}

QList<FPDF_PAGEOBJECT> textObjects(FPDF_PAGE page)
{
    QList<FPDF_PAGEOBJECT> out;
    for (int i = 0, n = FPDFPage_CountObjects(page); i < n; ++i) {
        FPDF_PAGEOBJECT obj = FPDFPage_GetObject(page, i);
        if (obj && FPDFPageObj_GetType(obj) == FPDF_PAGEOBJ_TEXT) out.append(obj);
    }
    return out;
}

QSet<FPDF_PAGEOBJECT> pageObjects(FPDF_PAGE page)
{
    QSet<FPDF_PAGEOBJECT> out;
    for (int i = 0, n = FPDFPage_CountObjects(page); i < n; ++i)
        out.insert(FPDFPage_GetObject(page, i));
    return out;
}

static_assert(FPDF_PAGEOBJ_TEXT == OriginalContent::Text && FPDF_PAGEOBJ_PATH == OriginalContent::Path
              && FPDF_PAGEOBJ_IMAGE == OriginalContent::Image
              && FPDF_PAGEOBJ_SHADING == OriginalContent::Shading
              && FPDF_PAGEOBJ_FORM == OriginalContent::Form);

QList<FPDF_PAGEOBJECT> allObjects(FPDF_PAGE page)
{
    QList<FPDF_PAGEOBJECT> out;
    for (int i = 0, n = FPDFPage_CountObjects(page); i < n; ++i)
        out.append(FPDFPage_GetObject(page, i));
    return out;
}

QList<int> objectTypes(const QList<FPDF_PAGEOBJECT> &objects)
{
    QList<int> out;
    out.reserve(objects.size());
    for (FPDF_PAGEOBJECT obj : objects) out.append(FPDFPageObj_GetType(obj));
    return out;
}

QHash<int, QTransform> textMatrices(const QList<FPDF_PAGEOBJECT> &objects)
{
    QHash<int, QTransform> out;
    for (int i = 0; i < objects.size(); ++i) {
        FS_MATRIX m {};
        if (FPDFPageObj_GetType(objects.at(i)) == FPDF_PAGEOBJ_TEXT
                && FPDFPageObj_GetMatrix(objects.at(i), &m))
            out.insert(i, QTransform(m.a, m.b, m.c, m.d, m.e, m.f));
    }
    return out;
}

bool keepsOriginal(const QList<FPDF_PAGEOBJECT> &original, const QList<int> &types,
                   const QHash<int, QTransform> &matrices, FPDF_PAGE page,
                   OriginalContent::Page &out)
{
    // The original content can be written back when the edits only took text
    // out and added objects of their own.
    const QSet<FPDF_PAGEOBJECT> now = pageObjects(page);
    for (int i = 0; i < original.size(); ++i) {
        if (now.contains(original.at(i))) continue;
        if (types.at(i) != FPDF_PAGEOBJ_TEXT) return false;
        out.removed.insert(i);
    }
    const QSet<FPDF_PAGEOBJECT> before(original.cbegin(), original.cend());
    for (FPDF_PAGEOBJECT obj : now)
        if (!before.contains(obj)) { out.appended = true; break; }
    out.textMatrices = matrices;
    return true;
}

QList<FPDF_PAGEOBJECT> keptCandidates(FPDF_PAGE page, const QList<bool> &spaced,
                                      KeptText::Kept &kept)
{
    const QList<FPDF_PAGEOBJECT> text = textObjects(page);
    kept.type3Only = text.size() != spaced.size();
    QList<FPDF_PAGEOBJECT> counted;
    for (int i = 0; i < text.size(); ++i) {
        const bool type3 = PdfiumFonts::isType3(FPDFTextObj_GetFont(text.at(i)));
        if (kept.type3Only && !type3) continue;
        const bool lost = type3 || (!kept.type3Only && spaced.at(i));
        counted.append(lost ? text.at(i) : nullptr);
    }
    kept.total = counted.size();
    return counted;
}

bool takeKeptText(FPDF_PAGE page, const QList<FPDF_PAGEOBJECT> &counted,
                  const QSet<FPDF_PAGEOBJECT> &before, bool modified, KeptText::Kept &kept)
{
    const QSet<FPDF_PAGEOBJECT> now = pageObjects(page);
    bool rewritten = modified;
    for (FPDF_PAGEOBJECT obj : before)
        if (!now.contains(obj)) { rewritten = true; break; }
    if (!rewritten) return false;

    for (int i = 0; i < counted.size(); ++i) {
        FPDF_PAGEOBJECT obj = counted.at(i);
        if (!obj || !now.contains(obj)) continue;
        kept.indices.insert(i);
        if (FPDFPage_RemoveObject(page, obj)) FPDFPageObj_Destroy(obj);
    }
    return true;
}

bool writePages(const QString &sourcePath, const QString &outputPath,
                const PdfiumWriter::PagePass &pass, const std::function<bool()> &cancelled,
                bool &wasEncrypted, QHash<int, KeptText::Kept> &keptPages,
                QHash<int, OriginalContent::Page> &originalPages,
                QHash<QByteArray, QMap<quint32, uint>> &toUnicode)
{
    PdfiumLock lock;
    const QByteArray path = sourcePath.toUtf8();
    const QByteArray pw   = PdfPwStore::get(sourcePath).toUtf8();

    FPDF_DOCUMENT doc = FPDF_LoadDocument(path.constData(),
                                          pw.isEmpty() ? nullptr : pw.constData());
    if (!doc) {
        qWarning() << "[PdfiumWriter] konnte" << sourcePath << "nicht öffnen,"
                   << "Fehler" << FPDF_GetLastError();
        return false;
    }

    wasEncrypted = FPDF_GetSecurityHandlerRevision(doc) >= 0;

    const QHash<int, OriginalContent::Scan> scans = OriginalContent::scan(sourcePath, pass.pages);
    if (pass.begin) pass.begin(doc);
    bool changed = true;
    for (int pageIndex : pass.pages) {
        FPDF_PAGE page = FPDF_LoadPage(doc, pageIndex);
        if (!page) continue;

        const OriginalContent::Scan scan = scans.value(pageIndex);
        const QList<FPDF_PAGEOBJECT> original = allObjects(page);
        const QList<int> types = objectTypes(original);
        const bool mapped = !scan.objects.isEmpty() && scan.objects == types;
        const QHash<int, QTransform> matrices = mapped ? textMatrices(original)
                                                       : QHash<int, QTransform>();
        KeptText::Kept kept;
        const QList<FPDF_PAGEOBJECT> counted =
            keptCandidates(page, mapped ? scan.spacedText : QList<bool>(), kept);
        const QSet<FPDF_PAGEOBJECT> before(original.cbegin(), original.cend());
        bool inPlace = false;
        if (!pass.change(doc, page, pageIndex, inPlace)) {
            FPDF_ClosePage(page);
            changed = false;
            break;
        }
        OriginalContent::Page content;
        if (mapped && !inPlace && keepsOriginal(original, types, matrices, page, content)) {
            // Text only added still goes through the original: its state must not reach the new text.
            if (!content.removed.isEmpty() || content.appended) originalPages.insert(pageIndex, content);
        } else if (takeKeptText(page, counted, before, inPlace, kept)) {
            if (!kept.indices.isEmpty()) keptPages.insert(pageIndex, kept);
            pinColoursToRgb(page);
        }

        FPDFPage_GenerateContent(page);
        FPDF_ClosePage(page);
    }
    if (pass.end) pass.end();
    if (!changed) {
        PdfiumFonts::releaseFonts(doc);
        FPDF_CloseDocument(doc);
        return false;
    }

    FileSink sink;
    sink.writer.version    = 1;
    sink.writer.WriteBlock = &writeBlock;
    sink.cancelled         = &cancelled;
    sink.file.setFileName(outputPath);
    if (!sink.file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        PdfiumFonts::releaseFonts(doc);
        FPDF_CloseDocument(doc);
        return false;
    }

    const bool saved = FPDF_SaveWithVersion(doc, &sink.writer, FPDF_NO_INCREMENTAL, 17);
    toUnicode = PdfiumFonts::writtenGlyphs(doc);
    const bool ok = saved && !sink.failed;
    sink.file.close();
    PdfiumFonts::releaseFonts(doc);
    FPDF_CloseDocument(doc);

    if (!ok) {
        QFile::remove(outputPath);
        qWarning() << "[PdfiumWriter] Schreiben nach" << outputPath << "fehlgeschlagen";
        return false;
    }
    return true;
}

bool writeAndRestore(const QString &sourcePath, const WriteGrant &output,
                     const PdfiumWriter::PagePass &pass, const std::function<bool()> &cancelled,
                     bool &wasEncrypted)
{
    const QString outputPath = output.path();
    QHash<int, KeptText::Kept> keptPages;
    QHash<int, OriginalContent::Page> originalPages;
    QHash<QByteArray, QMap<quint32, uint>> toUnicode;
    if (!writePages(sourcePath, outputPath, pass, cancelled, wasEncrypted, keptPages,
                    originalPages, toUnicode))
        return false;

    if (!OriginalContent::restore(sourcePath, output, originalPages, toUnicode))
        qWarning() << "[PdfiumWriter] Originalinhalt auf" << originalPages.size()
                   << "Seite(n) konnte nicht erhalten werden";

    if (!KeptText::restore(sourcePath, output, keptPages))
        qWarning() << "[PdfiumWriter] Text auf" << keptPages.size()
                   << "Seite(n) konnte nicht erhalten werden";
    return true;
}

}

bool PdfiumWriter::save(const QString &sourcePath, const WriteGrant &output,
                        const EditSession &session, const std::function<bool()> &cancelled)
{
    const QString outputPath = output.path();
    if (sourcePath.isEmpty() || outputPath.isEmpty()) return false;

    QHash<int, QList<EditSession::Edit>> fieldsByPage;
    PagePass pass;
    const auto touch = [&pass](int page) {
        if (!pass.pages.contains(page)) pass.pages.append(page);
    };
    for (const EditSession::Edit &e : session.snapshotEdits()) {
        if (!e.formField.isEmpty()) {
            if (!e.newText.isNull()) { fieldsByPage[e.page].append(e); touch(e.page); }
        } else {
            touch(e.page);
        }
    }
    for (const EditSession::ImageEdit &e : session.imageEdits())
        touch(e.page);
    for (const EditSession::ImageChange &c : session.imageChanges())
        touch(c.page);
    for (const EditSession::DrawStroke &stroke : session.drawStrokes())
        touch(stroke.page);
    for (const EditSession::LinkEdit &e : session.linkEdits())
        touch(e.page);
    for (const EditSession::NoteEdit &e : session.noteEdits())
        touch(e.page);

    // Pasted text takes its style from other pages, read before the loop changes them.
    QHash<int, FPDF_PAGE> sourcePages;
    pass.begin = [&](FPDF_DOCUMENT doc) {
        for (const EditSession::Edit &e : session.edits())
            for (const TextLayout::Source &s : TextLayout::distinctSources(e.sources))
                if (s.page != e.page && !sourcePages.contains(s.page))
                    if (FPDF_PAGE p = FPDF_LoadPage(doc, s.page)) sourcePages.insert(s.page, p);
    };
    pass.change = [&](FPDF_DOCUMENT doc, FPDF_PAGE page, int pageIndex, bool &inPlace) {
        PdfiumEdits::applyToPage(doc, page, pageIndex, session, &sourcePages);
        PdfiumEdits::applyNoteEdits(page, pageIndex, session);
        for (const EditSession::Edit &edit : fieldsByPage.value(pageIndex))
            setFieldValue(page, edit.formField, edit.newText);
        // Link styling and image changes alter objects in place, so those pages are regenerated.
        inPlace = session.hasLinkEditsOnPage(pageIndex);
        for (const EditSession::ImageChange &c : session.imageChanges())
            inPlace |= c.page == pageIndex;
        return true;
    };
    pass.end = [&]() {
        for (FPDF_PAGE p : std::as_const(sourcePages)) FPDF_ClosePage(p);
    };

    bool wasEncrypted = false;
    if (!writeAndRestore(sourcePath, output, pass, cancelled, wasEncrypted)) return false;

    const QString password = PdfPwStore::get(sourcePath);
    if (wasEncrypted && !isEncrypted(outputPath, password)) {
        if (!reapplyEncryption(outputPath, password)) {

            QFile::remove(outputPath);
            return false;
        }
    }
    return true;
}

bool PdfiumWriter::rewrite(const QString &sourcePath, const WriteGrant &output,
                           const PagePass &pass, const std::function<bool()> &cancelled)
{
    if (sourcePath.isEmpty() || output.path().isEmpty() || !pass.change) return false;
    bool wasEncrypted = false;
    return writeAndRestore(sourcePath, output, pass, cancelled, wasEncrypted);
}

#endif
