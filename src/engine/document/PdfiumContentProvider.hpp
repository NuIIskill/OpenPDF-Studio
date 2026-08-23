#pragma once

#if defined(HAVE_PDF_RENDERING) && defined(HAVE_PDFIUM)

#include "engine/edit/ContentMap.hpp"
#include "engine/edit/ContentModel.hpp"

#include "fpdfview.h"

/// Das Regionenmodell einer Seite, gebaut aus PDFiums eigener Sicht auf das
/// Dokument.
///
/// Die beiden Wege, die es vorher gab, hatten je einen Haken: der qpdf-Scanner
/// las den Content-Stream als Bytes und bekam bei Schriften mit eigener
/// Kodierung Zeichencodes statt Unicode — dafür gab es im Exporter eine eigene
/// Nachbesserung, die den Text von Qt nachholte. Poppler lieferte zwar Unicode,
/// aber nur Wortkästen ohne Schriftgröße und ohne Farbe; beides musste
/// geschätzt werden.
///
/// PDFium liefert beides zugleich: dekodierten Text UND die Schriftgröße, die
/// im Dokument steht, dazu Füllfarbe und Schriftname pro Zeichen. Was hier
/// entsteht, ist deshalb genauer als bei beiden Vorgängern — und braucht keine
/// Nachbesserung von außen.
class PdfiumContentProvider : public ContentProvider
{
public:
    /// `doc` gehört dem Backend und muss den Provider überleben.
    explicit PdfiumContentProvider(FPDF_DOCUMENT doc);
    ~PdfiumContentProvider() override;

    QList<ContentItem> pageItemsForExport(int page) override;

protected:
    QList<ContentItem> buildPage(int page) override;

private:
    QList<ContentItem> buildPageItems(int page, bool mergeVertical);

    /// Wörter der Seite als backendneutrale Cluster — daraus macht
    /// classifyContentClusters() Zeilen, Absätze und Tabellenzellen.
    QList<ContentCluster> collectWords(FPDF_TEXTPAGE tp, double pageHeight) const;

    /// Textfelder und Medien-Annotationen einer Seite.
    void collectAnnotations(FPDF_PAGE pg, double pageHeight,
                            QList<ContentItem> *fields,
                            QList<ContentItem> *media) const;

    /// Formularfelder werden direkt aus dem Wörterbuch der Annotation gelesen
    /// (/FT, /T, /V, /DA) und NICHT über FPDFDOC_InitFormFillEnvironment.
    ///
    /// Grund: mit dieser Umgebung beendet sich der Windows-Build nicht mehr.
    /// Der Export schreibt seine Datei fertig und bleibt danach hängen —
    /// gemessen 45 s Zeitüberschreitung gegen 1,6 s ohne, und ein sauberes
    /// FPDFDOC_ExitFormFillEnvironment im Destruktor ändert daran nichts.
    /// Zum Lesen wird sie ohnehin nicht gebraucht.
    ///
    /// Der Preis: erbt ein Widget /FT oder /T vom übergeordneten Feldknoten,
    /// statt sie selbst zu tragen, wird es nicht erkannt. Zusammengelegte
    /// Feld-und-Widget-Wörterbücher — der Normalfall — tragen beides selbst.
    FPDF_DOCUMENT m_doc { nullptr };
};

#endif
