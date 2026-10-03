#include "engine/document/PdfiumWriter.hpp"

#if defined(HAVE_PDF_RENDERING) && defined(HAVE_PDFIUM)

#include "app/PdfPwStore.hpp"
#include "engine/document/PdfiumEdits.hpp"
#include "engine/document/PdfiumFonts.hpp"
#include "engine/document/PdfiumLock.hpp"
#include "engine/document/Type3Text.hpp"
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
#include <QSet>

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

QList<FPDF_PAGEOBJECT> type3TextObjects(FPDF_PAGE page)
{
    QList<FPDF_PAGEOBJECT> out;
    for (int i = 0, n = FPDFPage_CountObjects(page); i < n; ++i) {
        FPDF_PAGEOBJECT obj = FPDFPage_GetObject(page, i);
        if (obj && FPDFPageObj_GetType(obj) == FPDF_PAGEOBJ_TEXT
                && PdfiumFonts::isType3(FPDFTextObj_GetFont(obj)))
            out.append(obj);
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

Type3Text::Kept takeType3Text(FPDF_PAGE page, const QList<FPDF_PAGEOBJECT> &type3,
                              const QSet<FPDF_PAGEOBJECT> &before, bool modified)
{
    Type3Text::Kept kept;
    const QSet<FPDF_PAGEOBJECT> now = pageObjects(page);
    bool rewritten = modified;
    for (FPDF_PAGEOBJECT obj : before)
        if (!now.contains(obj)) { rewritten = true; break; }
    if (!rewritten) return kept;

    kept.total = type3.size();
    for (int i = 0; i < type3.size(); ++i) {
        if (!now.contains(type3.at(i))) continue;
        kept.indices.insert(i);
        if (FPDFPage_RemoveObject(page, type3.at(i))) FPDFPageObj_Destroy(type3.at(i));
    }
    return kept;
}

bool writeEdits(const QString &sourcePath, const QString &outputPath,
                const EditSession &session, const std::function<bool()> &cancelled,
                bool &wasEncrypted, QHash<int, Type3Text::Kept> &type3Pages)
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

    QHash<int, QList<EditSession::Edit>> fieldsByPage;
    QList<int> touched;
    const auto touch = [&touched](int page) {
        if (!touched.contains(page)) touched.append(page);
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
    for (const EditSession::DrawStroke &stroke : session.drawStrokes())
        touch(stroke.page);
    for (const EditSession::LinkEdit &e : session.linkEdits())
        touch(e.page);
    for (const EditSession::NoteEdit &e : session.noteEdits())
        touch(e.page);

    for (int pageIndex : touched) {
        FPDF_PAGE page = FPDF_LoadPage(doc, pageIndex);
        if (!page) continue;

        const QList<FPDF_PAGEOBJECT> type3 = type3TextObjects(page);
        const QSet<FPDF_PAGEOBJECT> before = type3.isEmpty() ? QSet<FPDF_PAGEOBJECT>()
                                                             : pageObjects(page);
        PdfiumEdits::applyToPage(doc, page, pageIndex, session);
        PdfiumEdits::applyNoteEdits(page, pageIndex, session);
        for (const EditSession::Edit &edit : fieldsByPage.value(pageIndex))
            setFieldValue(page, edit.formField, edit.newText);
        if (!type3.isEmpty()) {
            const Type3Text::Kept kept = takeType3Text(
                page, type3, before, session.hasLinkEditsOnPage(pageIndex));
            if (!kept.indices.isEmpty()) type3Pages.insert(pageIndex, kept);
        }

        FPDFPage_GenerateContent(page);
        FPDF_ClosePage(page);
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

}

bool PdfiumWriter::save(const QString &sourcePath, const QString &outputPath,
                        const EditSession &session, const std::function<bool()> &cancelled)
{
    if (sourcePath.isEmpty() || outputPath.isEmpty()) return false;

    bool wasEncrypted = false;
    QHash<int, Type3Text::Kept> type3Pages;
    if (!writeEdits(sourcePath, outputPath, session, cancelled, wasEncrypted, type3Pages))
        return false;

    if (!Type3Text::restore(sourcePath, outputPath, type3Pages))
        qWarning() << "[PdfiumWriter] Type3-Text auf" << type3Pages.size()
                   << "Seite(n) konnte nicht erhalten werden";

    const QString password = PdfPwStore::get(sourcePath);
    if (wasEncrypted && !isEncrypted(outputPath, password)) {
        if (!reapplyEncryption(outputPath, password)) {

            QFile::remove(outputPath);
            return false;
        }
    }
    return true;
}

#endif
