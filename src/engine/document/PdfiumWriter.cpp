#include "engine/document/PdfiumWriter.hpp"

#if defined(HAVE_PDF_RENDERING) && defined(HAVE_PDFIUM)

#include "app/PdfPwStore.hpp"
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
#include <QImage>
#include <QFont>
#include <QFontMetricsF>
#include <QtMath>

#include <vector>

namespace {

// ── Ausgabe ──────────────────────────────────────────────────────────────────

/// PDFium schreibt über einen Rückruf. Die Struktur MUSS mit dem
/// FPDF_FILEWRITE-Feld beginnen: PDFium reicht ihre Adresse zurück und
/// erwartet dort genau dieses Feld.
struct FileSink {
    FPDF_FILEWRITE writer {};
    QFile          file;
    bool           failed { false };
};

int writeBlock(FPDF_FILEWRITE *self, const void *data, unsigned long size)
{
    auto *sink = reinterpret_cast<FileSink *>(self);
    if (sink->failed) return 0;
    const qint64 written = sink->file.write(static_cast<const char *>(data),
                                            static_cast<qint64>(size));
    if (written != static_cast<qint64>(size)) {
        sink->failed = true;
        return 0;
    }
    return 1;
}

// ── Koordinaten ──────────────────────────────────────────────────────────────
// Die Sitzung rechnet in Qt-Konvention (Ursprung oben links, Y nach unten),
// PDF-Seitenobjekte in PDF-Konvention (unten links, Y nach oben).

double toPdfY(double qtY, double pageHeight) { return pageHeight - qtY; }

QRectF objectBoundsQt(FPDF_PAGEOBJECT obj, double pageHeight)
{
    float left = 0, bottom = 0, right = 0, top = 0;
    if (!FPDFPageObj_GetBounds(obj, &left, &bottom, &right, &top)) return {};
    return QRectF(left, pageHeight - top, right - left, top - bottom);
}

/// Die Flächen, in denen der ursprüngliche Text verschwinden soll. Die engen
/// Glyphenkästen sind das Genauere; ohne sie bleibt der ganze Rahmen.
QList<QRectF> eraseAreas(const EditSession::Edit &edit)
{
    if (!edit.eraseRects.isEmpty()) return edit.eraseRects;
    return { edit.pdfBounds };
}

// ── Text ─────────────────────────────────────────────────────────────────────

/// Entfernt die Textobjekte, die in einer der Flächen liegen, und gibt die
/// Schrift des ersten entfernten zurück.
///
/// Sie zurückzugeben ist der Trick, der den Austausch unauffällig macht: die
/// Ersetzung bekommt damit dieselbe eingebettete Schrift wie das Original,
/// statt auf eine Standardschrift auszuweichen.
FPDF_FONT removeTextIn(FPDF_PAGE page, const QList<QRectF> &areas, double pageHeight)
{
    FPDF_FONT reusable = nullptr;
    std::vector<FPDF_PAGEOBJECT> doomed;

    const int count = FPDFPage_CountObjects(page);
    for (int i = 0; i < count; ++i) {
        FPDF_PAGEOBJECT obj = FPDFPage_GetObject(page, i);
        if (!obj || FPDFPageObj_GetType(obj) != FPDF_PAGEOBJ_TEXT) continue;

        const QRectF bounds = objectBoundsQt(obj, pageHeight);
        if (bounds.isEmpty()) continue;

        bool covered = false;
        for (const QRectF &area : areas) {
            // Die Mitte entscheidet: ein Textobjekt, das nur mit dem Rand in
            // die Fläche ragt, gehört noch zur Nachbarzeile.
            if (area.contains(bounds.center())) { covered = true; break; }
        }
        if (!covered) continue;

        if (!reusable) reusable = FPDFTextObj_GetFont(obj);
        doomed.push_back(obj);
    }

    for (FPDF_PAGEOBJECT obj : doomed) {
        if (FPDFPage_RemoveObject(page, obj))
            FPDFPageObj_Destroy(obj);   // nach dem Entfernen gehört es uns
    }
    return reusable;
}

/// Wo die erste Zeile der Ersetzung ansetzt, in PDF-Koordinaten.
///
/// `textOriginOffset` ist das Genaue: die Stiftposition der ersetzten Stelle,
/// y auf der Grundlinie. Fehlt sie, wird die Grundlinie aus dem Rahmen
/// geschätzt — der Rahmen sagt nur, wo die Tinte lag, und das ist eine
/// Oberlänge daneben.
QPointF firstBaseline(const EditSession::Edit &edit, double fontSize, double pageHeight)
{
    const bool customInset = edit.box.paddingPt > 0.0
                          || edit.box.verticalAlign != TextBoxProperties::VerticalAlign::Top;
    if (edit.hasTextOrigin && !customInset) {
        const QPointF qt = edit.pdfBounds.topLeft() + edit.textOriginOffset;
        return QPointF(qt.x(), toPdfY(qt.y(), pageHeight));
    }
    const int lines = qMax(1, edit.newText.count(u'\n') + 1);
    const double step = edit.lineSpacingPt > 0.0 ? edit.lineSpacingPt : fontSize * 1.2;
    const double contentHeight = fontSize + (lines - 1)
                               * (step + edit.box.paragraphSpacingPt);
    const double innerHeight = qMax(0.0, edit.pdfBounds.height() - 2 * edit.box.paddingPt);
    double offset = 0.0;
    if (edit.box.verticalAlign == TextBoxProperties::VerticalAlign::Center)
        offset = qMax(0.0, (innerHeight - contentHeight) / 2.0);
    else if (edit.box.verticalAlign == TextBoxProperties::VerticalAlign::Bottom)
        offset = qMax(0.0, innerHeight - contentHeight);
    const double baselineQt = edit.pdfBounds.top() + edit.box.paddingPt
                            + offset + fontSize * 0.8;
    return QPointF(edit.pdfBounds.left() + edit.box.paddingPt
                       + edit.box.indentLevel * 18.0,
                   toPdfY(baselineQt, pageHeight));
}

void rotateObject(FPDF_PAGEOBJECT object, const EditSession::Edit &edit,
                  double pageHeight)
{
    if (!object || qFuzzyIsNull(edit.box.rotationDeg)) return;
    const double a = qDegreesToRadians(-edit.box.rotationDeg);
    const double cs = std::cos(a), sn = std::sin(a);
    const QPointF center(edit.pdfBounds.center().x(),
                         toPdfY(edit.pdfBounds.center().y(), pageHeight));
    FPDFPageObj_Transform(object, cs, sn, -sn, cs,
                          center.x() - cs * center.x() + sn * center.y(),
                          center.y() - sn * center.x() - cs * center.y());
}

FPDF_PAGEOBJECT roundedRectPath(const QRectF &qtRect, double radius,
                                double pageHeight)
{
    const double l=qtRect.left(), r=qtRect.right();
    const double b=toPdfY(qtRect.bottom(),pageHeight), t=toPdfY(qtRect.top(),pageHeight);
    const double rad=qBound(0.0,radius,qMin(qtRect.width(),qtRect.height())/2.0);
    if (rad <= 0.0) return FPDFPageObj_CreateNewRect(l,b,qtRect.width(),qtRect.height());
    constexpr double k=.5522847498;
    FPDF_PAGEOBJECT p=FPDFPageObj_CreateNewPath(l+rad,b);
    FPDFPath_LineTo(p,r-rad,b); FPDFPath_BezierTo(p,r-rad+k*rad,b,r,b+rad-k*rad,r,b+rad);
    FPDFPath_LineTo(p,r,t-rad); FPDFPath_BezierTo(p,r,t-rad+k*rad,r-rad+k*rad,t,r-rad,t);
    FPDFPath_LineTo(p,l+rad,t); FPDFPath_BezierTo(p,l+rad-k*rad,t,l,t-rad+k*rad,l,t-rad);
    FPDFPath_LineTo(p,l,b+rad); FPDFPath_BezierTo(p,l,b+rad-k*rad,l+rad-k*rad,b,l+rad,b);
    FPDFPath_Close(p); return p;
}

void insertBoxDecoration(FPDF_PAGE page, const EditSession::Edit &edit,
                         double pageHeight)
{
    if (!edit.box.backgroundEnabled && !edit.box.borderEnabled) return;
    FPDF_PAGEOBJECT path=roundedRectPath(edit.pdfBounds,edit.box.cornerRadiusPt,pageHeight);
    if (!path) return;
    const int alpha=qRound(255*qBound(0.0,edit.box.opacity,1.0));
    if (edit.box.backgroundEnabled) {
        const QColor c=edit.box.backgroundColor;
        FPDFPageObj_SetFillColor(path,c.red(),c.green(),c.blue(),qRound(alpha*c.alphaF()));
    }
    const bool solidBorder = edit.box.borderEnabled
                          && edit.box.borderStyle == TextBoxProperties::BorderStyle::Solid;
    if (solidBorder) {
        const QColor c=edit.box.borderColor;
        FPDFPageObj_SetStrokeColor(path,c.red(),c.green(),c.blue(),qRound(alpha*c.alphaF()));
        FPDFPageObj_SetStrokeWidth(path,qMax(.1,edit.box.borderWidthPt));
    }
    FPDFPath_SetDrawMode(path,edit.box.backgroundEnabled?FPDF_FILLMODE_WINDING:FPDF_FILLMODE_NONE,
                         solidBorder);
    rotateObject(path,edit,pageHeight);
    FPDFPage_InsertObject(page,path);

    if (edit.box.borderEnabled && !solidBorder) {
        const QRectF r=edit.pdfBounds;
        const double left=r.left(),right=r.right(),bottom=toPdfY(r.bottom(),pageHeight),top=toPdfY(r.top(),pageHeight);
        FPDF_PAGEOBJECT dashed=FPDFPageObj_CreateNewPath(left,bottom);
        const double dash=edit.box.borderStyle==TextBoxProperties::BorderStyle::Dotted
                            ? qMax(.2,edit.box.borderWidthPt*.25)
                            : qMax(2.,edit.box.borderWidthPt*4.);
        const double gap=qMax(2.,edit.box.borderWidthPt*2.5);
        const auto edge=[&](double x1,double y1,double x2,double y2){
            const double len=std::hypot(x2-x1,y2-y1); if(len<=0)return;
            for(double at=0;at<len;at+=dash+gap){const double e=qMin(len,at+dash);FPDFPath_MoveTo(dashed,x1+(x2-x1)*at/len,y1+(y2-y1)*at/len);FPDFPath_LineTo(dashed,x1+(x2-x1)*e/len,y1+(y2-y1)*e/len);}
        };
        edge(left,bottom,right,bottom);edge(right,bottom,right,top);edge(right,top,left,top);edge(left,top,left,bottom);
        const QColor c=edit.box.borderColor;FPDFPageObj_SetStrokeColor(dashed,c.red(),c.green(),c.blue(),qRound(alpha*c.alphaF()));FPDFPageObj_SetStrokeWidth(dashed,qMax(.1,edit.box.borderWidthPt));FPDFPath_SetDrawMode(dashed,FPDF_FILLMODE_NONE,true);rotateObject(dashed,edit,pageHeight);FPDFPage_InsertObject(page,dashed);
    }
}

/// Setzt den Ersatztext als Seitenobjekte ein — eine Zeile, ein Objekt.
void insertReplacement(FPDF_DOCUMENT doc, FPDF_PAGE page,
                       const EditSession::Edit &edit,
                       FPDF_FONT original, double pageHeight)
{
    if (edit.newText.isEmpty()) return;   // reine Löschung: nichts einsetzen

    const double size = edit.fontSizePt > 0.0 ? edit.fontSizePt
                                              : qMax(6.0, edit.pdfBounds.height() * 0.8);

    // Die Schrift des Originals behalten, solange der Benutzer sie nicht
    // gewechselt hat — sonst eine Standardschrift, die jedes PDF versteht.
    FPDF_FONT font = nullptr;
    bool ownsFont = false;
    if (!edit.fontChanged && original) {
        font = original;
    } else {
        font = FPDFText_LoadStandardFont(doc, "Helvetica");
        ownsFont = true;
    }
    if (!font) return;

    insertBoxDecoration(page, edit, pageHeight);

    const QPointF start = firstBaseline(edit, size, pageHeight);
    const double  step  = (edit.lineSpacingPt > 0.0 ? edit.lineSpacingPt : size * 1.2)
                        + edit.box.paragraphSpacingPt;

    const QStringList lines = edit.newText.split(QLatin1Char('\n'));
    QFont metricFont(edit.fontFamily.isEmpty()?QStringLiteral("Helvetica"):edit.fontFamily);
    metricFont.setPixelSize(qMax(1,qRound(size)));
    metricFont.setBold(edit.bold);
    metricFont.setItalic(edit.italic);
    const QFontMetricsF fm(metricFont);
    const double availableWidth = qMax(0.0, edit.pdfBounds.width()
        - 2 * edit.box.paddingPt - edit.box.indentLevel * 18.0);
    for (int i = 0; i < lines.size(); ++i) {
        QString line = lines.at(i);
        if (edit.box.listStyle == TextBoxProperties::ListStyle::Bullets)
            line.prepend(QStringLiteral("• "));
        else if (edit.box.listStyle == TextBoxProperties::ListStyle::Numbered)
            line.prepend(QString::number(i + 1) + QStringLiteral(". "));
        if (line.isEmpty()) continue;
        const QColor color = edit.textColor.isValid() ? edit.textColor : QColor(Qt::black);
        const auto insertObject = [&](const QString &text, double x) {
            FPDF_PAGEOBJECT obj=FPDFPageObj_CreateTextObj(doc,font,static_cast<float>(size));
            if(!obj)return;
            const std::u16string utf16=text.toStdU16String();
            FPDFText_SetText(obj,reinterpret_cast<FPDF_WIDESTRING>(utf16.c_str()));
            FPDFPageObj_SetFillColor(obj,color.red(),color.green(),color.blue(),
                                     qRound(color.alpha()*qBound(0.0,edit.box.opacity,1.0)));
            FPDFPageObj_Transform(obj,1,0,0,1,x,start.y()-step*i);
            rotateObject(obj,edit,pageHeight); FPDFPage_InsertObject(page,obj);
        };
        double lineWidth = fm.horizontalAdvance(line);
        if (!qFuzzyIsNull(edit.box.characterSpacingPt) && line.size() > 1)
            lineWidth += edit.box.characterSpacingPt * (line.size() - 1);
        double lineX = start.x();
        if (edit.box.horizontalAlign == TextBoxProperties::HorizontalAlign::Center)
            lineX += qMax(0.0, (availableWidth - lineWidth) / 2.0);
        else if (edit.box.horizontalAlign == TextBoxProperties::HorizontalAlign::Right)
            lineX += qMax(0.0, availableWidth - lineWidth);
        if (qFuzzyIsNull(edit.box.characterSpacingPt)) {
            insertObject(line,lineX);
        } else {
            double x=lineX;
            for (const QChar ch : line) {
                insertObject(QString(ch),x);
                x += fm.horizontalAdvance(ch) + edit.box.characterSpacingPt;
            }
        }
    }

    if (ownsFont) FPDFFont_Close(font);
}

// ── Formularfelder ───────────────────────────────────────────────────────────

/// Escapt eine Zeichenkette für einen PDF-Literal-String.
QByteArray escapePdfString(const QString &text)
{
    QByteArray out;
    for (const QChar &c : text) {
        const char ch = c.toLatin1();
        if (ch == '(' || ch == ')' || ch == '\\') out += '\\';
        out += ch ? ch : '?';   // außerhalb von Latin-1: Platzhalter
    }
    return out;
}

/// Setzt den Wert eines AcroForm-Textfeldes und erneuert seine Darstellung.
///
/// Zwei Dinge gehören dazu und nur zusammen ergeben sie ein sichtbares
/// Ergebnis: `/V` ist der Wert, den ein Formularleser ausliest, und der
/// Appearance-Stream ist das, was ein Betrachter zeichnet. Wird nur `/V`
/// gesetzt, zeigt die Seite weiterhin den alten Text.
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

        // Feldname aus dem Wörterbuch — dieselbe Quelle wie im Regionenmodell.
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

        // Ein minimaler, aber vollständiger Darstellungsstrom. /Helv steht in
        // den Standardressourcen praktisch jedes Formulars.
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

// ── Bilder ───────────────────────────────────────────────────────────────────

void insertImage(FPDF_DOCUMENT doc, FPDF_PAGE page,
                 const EditSession::ImageEdit &edit, double pageHeight)
{
    if (edit.image.isNull()) return;

    const QImage src = edit.image.convertToFormat(QImage::Format_ARGB32);
    FPDF_BITMAP bitmap = FPDFBitmap_Create(src.width(), src.height(), 1);
    if (!bitmap) return;

    // FPDFBitmap_Create liefert BGRA — dasselbe Byte-Layout wie ARGB32 auf
    // Little-Endian, weshalb sich die Zeilen direkt kopieren lassen.
    auto *dst = static_cast<unsigned char *>(FPDFBitmap_GetBuffer(bitmap));
    const int stride = FPDFBitmap_GetStride(bitmap);
    for (int y = 0; y < src.height(); ++y)
        memcpy(dst + static_cast<size_t>(y) * stride, src.constScanLine(y),
               static_cast<size_t>(src.bytesPerLine()));

    FPDF_PAGEOBJECT obj = FPDFPageObj_NewImageObj(doc);
    if (!obj) { FPDFBitmap_Destroy(bitmap); return; }
    FPDFImageObj_SetBitmap(&page, 1, obj, bitmap);

    // Ein Bildobjekt füllt das Einheitsquadrat; die Matrix bestimmt, wo es
    // landet und wie groß es wird.
    const QRectF &b = edit.pdfBounds;
    FPDFImageObj_SetMatrix(obj, b.width(), 0, 0, b.height(),
                           b.left(), toPdfY(b.bottom(), pageHeight));
    FPDFPage_InsertObject(page, obj);
    FPDFBitmap_Destroy(bitmap);
}

/// Trägt die Datei ein Verschlüsselungswörterbuch?
bool isEncrypted(const QString &file, const QString &password)
{
    const QByteArray path = file.toUtf8();
    const QByteArray pw   = password.toUtf8();
    FPDF_DOCUMENT doc = FPDF_LoadDocument(path.constData(),
                                          pw.isEmpty() ? nullptr : pw.constData());
    if (!doc) return false;   // nicht lesbar — keine belastbare Aussage
    const bool encrypted = FPDF_GetSecurityHandlerRevision(doc) >= 0;
    FPDF_CloseDocument(doc);
    return encrypted;
}

/// Legt die Verschlüsselung wieder an, falls sie beim Schreiben verloren ging.
///
/// Gebraucht wird das seltener als gedacht: `FPDF_SaveWithVersion` ÜBERNIMMT
/// die Verschlüsselung eines Dokuments, das aus einer geschützten Datei geladen
/// wurde. Nachgemessen an `encrypted.pdf` — die Ausgabe trägt ein
/// /Encrypt-Wörterbuch, ist ohne Passwort nicht lesbar und mit dem
/// ursprünglichen Passwort vollständig lesbar, Bearbeitung inklusive.
///
/// Was PDFium nicht kann, ist eine NEUE Verschlüsselung anlegen — etwa für ein
/// frisch erzeugtes Dokument, wie es der Seiten-Organizer baut. Für diesen Fall
/// steht der Nachlauf hier bereit, und der Aufrufer prüft mit isEncrypted(), ob
/// er ihn braucht, statt es anzunehmen. So heilt sich der Weg auch dann, wenn
/// eine spätere PDFium-Fassung ihr Verhalten ändert.
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
            // AES-256, und das Benutzerpasswort dient zugleich als
            // Besitzerpasswort: das Dokument öffnet mit demselben Passwort wie
            // zuvor, und wer es kennt, darf alles.
            writer.setR6EncryptionParameters(
                pass.c_str(), pass.c_str(),
                /*accessibility*/ true, /*extract*/ true, /*assemble*/ true,
                /*annotate_and_form*/ true, /*form_filling*/ true,
                /*modify_other*/ true, qpdf_r3p_full, /*encrypt_metadata*/ true);
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
    // Ohne qpdf lässt sich die Verschlüsselung nicht wieder anlegen. Dann darf
    // die Datei nicht als Erfolg durchgehen — ungeschützt gespeichert wäre
    // schlimmer als gar nicht gespeichert.
    Q_UNUSED(file)
    qWarning() << "[PdfiumWriter] ohne qpdf kann die Verschlüsselung nicht"
               << "wiederhergestellt werden — Speichern abgebrochen";
    return password.isEmpty();
#endif
}

} // namespace

bool PdfiumWriter::save(const QString &sourcePath, const QString &outputPath,
                        const EditSession &session)
{
    if (sourcePath.isEmpty() || outputPath.isEmpty()) return false;

    const QByteArray path = sourcePath.toUtf8();
    const QByteArray pw   = PdfPwStore::get(sourcePath).toUtf8();
    // Ein ZWEITES, eigenes Dokument: das der Ansicht wird gerade gerendert und
    // darf sich nicht unter ihr verändern.
    FPDF_DOCUMENT doc = FPDF_LoadDocument(path.constData(),
                                          pw.isEmpty() ? nullptr : pw.constData());
    if (!doc) {
        qWarning() << "[PdfiumWriter] konnte" << sourcePath << "nicht öffnen —"
                   << "Fehler" << FPDF_GetLastError();
        return false;
    }

    // Vor dem Schreiben merken: nachher ist das Dokument geschlossen, und die
    // Frage "war die Quelle geschützt?" wäre nicht mehr zu beantworten.
    const bool wasEncrypted = FPDF_GetSecurityHandlerRevision(doc) >= 0;

    QHash<int, QList<EditSession::Edit>> textByPage;
    QHash<int, QList<EditSession::Edit>> fieldsByPage;
    for (const EditSession::Edit &e : session.snapshotEdits()) {
        // Feldbearbeitungen fassen den Content-Stream nicht an: sichtbar wird
        // ihr Wert über die Darstellung des Widgets.
        if (e.formField.isEmpty()) textByPage[e.page].append(e);
        else if (!e.newText.isNull()) fieldsByPage[e.page].append(e);
    }

    QHash<int, QList<EditSession::ImageEdit>> imagesByPage;
    for (const EditSession::ImageEdit &e : session.imageEdits())
        imagesByPage[e.page].append(e);

    QList<int> touched = textByPage.keys();
    for (int page : imagesByPage.keys())
        if (!touched.contains(page)) touched.append(page);
    for (int page : fieldsByPage.keys())
        if (!touched.contains(page)) touched.append(page);

    for (int pageIndex : touched) {
        FPDF_PAGE page = FPDF_LoadPage(doc, pageIndex);
        if (!page) continue;
        const double pageHeight = FPDF_GetPageHeightF(page);

        for (const EditSession::Edit &edit : textByPage.value(pageIndex)) {
            FPDF_FONT original = removeTextIn(page, eraseAreas(edit), pageHeight);
            insertReplacement(doc, page, edit, original, pageHeight);
        }
        for (const EditSession::ImageEdit &edit : imagesByPage.value(pageIndex))
            insertImage(doc, page, edit, pageHeight);
        for (const EditSession::Edit &edit : fieldsByPage.value(pageIndex))
            setFieldValue(page, edit.formField, edit.newText);

        // Ohne diesen Aufruf steht die Änderung nur im Objektbaum und nicht im
        // Content-Stream — die gespeicherte Datei sähe aus wie vorher.
        FPDFPage_GenerateContent(page);
        FPDF_ClosePage(page);
    }

    FileSink sink;
    sink.writer.version    = 1;
    sink.writer.WriteBlock = &writeBlock;
    sink.file.setFileName(outputPath);
    if (!sink.file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        FPDF_CloseDocument(doc);
        return false;
    }

    // Version 17 = PDF 1.7. FPDF_NO_INCREMENTAL schreibt das Dokument neu,
    // statt Änderungen anzuhängen: das Ergebnis ist kleiner und enthält den
    // ersetzten Text nicht mehr als überschriebene Altfassung.
    const bool saved = FPDF_SaveWithVersion(doc, &sink.writer, FPDF_NO_INCREMENTAL, 17);
    const bool ok = saved && !sink.failed;
    sink.file.close();
    FPDF_CloseDocument(doc);

    if (!ok) {
        QFile::remove(outputPath);
        qWarning() << "[PdfiumWriter] Schreiben nach" << outputPath << "fehlgeschlagen";
        return false;
    }

    // Geprüft statt angenommen: normalerweise trägt PDFium die Verschlüsselung
    // mit, und dann ist hier nichts zu tun.
    const QString password = PdfPwStore::get(sourcePath);
    if (wasEncrypted && !isEncrypted(outputPath, password)) {
        if (!reapplyEncryption(outputPath, password)) {
            // Ungeschützt gespeichert wäre schlimmer als nicht gespeichert.
            QFile::remove(outputPath);
            return false;
        }
    }
    return true;
}

#endif // HAVE_PDF_RENDERING && HAVE_PDFIUM
