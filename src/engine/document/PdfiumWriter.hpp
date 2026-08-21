#pragma once

#if defined(HAVE_PDF_RENDERING) && defined(HAVE_PDFIUM)

#include <QString>

class EditSession;

/// Schreibt ein Dokument mit den Änderungen einer Sitzung — auf Objektebene.
///
/// Der Unterschied zum Weg, den es vorher gab, ist die Ebene, auf der
/// gearbeitet wird. Der qpdf-Pfad zerlegte den Content-Stream einer Seite in
/// Token, entfernte die Textoperatoren im Bearbeitungsbereich und hängte neue
/// an. Das funktioniert, solange der Erzeuger des PDFs nichts Unerwartetes tut
/// — und weil das nicht verlässlich ist, rendert er anschließend jede geänderte
/// Seite gegen das Original und schaltete für zerschossene Seiten in einen
/// Overlay-Modus zurück.
///
/// Hier wird stattdessen das Textobjekt entfernt und ein neues eingesetzt.
/// Alles andere auf der Seite bleibt unberührt, weil es gar nicht angefasst
/// wird — die Absicherung erübrigt sich damit.
///
/// Ebenfalls erledigt: Bildbearbeitungen. Der qpdf-Pfad fiel auf ein Vollraster
/// des GANZEN Dokuments zurück, sobald eine einzige Bildeinfügung vorkam ("vector PDF manipulation cannot embed raster images into the page
/// stream cleanly"). Für PDFium ist ein Bild ein Seitenobjekt unter vielen.
///
/// Zur Verschlüsselung: sie überlebt das Speichern. `FPDF_SaveWithVersion`
/// übernimmt sie von der Quelle — nachgemessen, nicht angenommen. Nur eine
/// NEUE Verschlüsselung kann PDFium nicht anlegen; für den Fall prüft der
/// Schreibweg das Ergebnis und schiebt einen qpdf-Lauf nach, wenn der Schutz
/// tatsächlich fehlt.
namespace PdfiumWriter {

/// Schreibt `sourcePath` mit allen Änderungen nach `outputPath`.
/// `outputPath` wird vollständig geschrieben oder gar nicht angelegt.
bool save(const QString &sourcePath, const QString &outputPath,
          const EditSession &session);

} // namespace PdfiumWriter

#endif
