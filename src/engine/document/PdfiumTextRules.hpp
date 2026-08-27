#pragma once

#if defined(HAVE_PDF_RENDERING) && defined(HAVE_PDFIUM)

#include <QRectF>
#include <QtGlobal>

/// Regeln, nach denen aus PDFiums Zeichenstrom Wörter und Zeilen werden.
///
/// Sie stehen hier und nicht zweimal daneben, weil Backend und Regionenmodell
/// dieselbe Frage stellen und sonst irgendwann verschiedene Antworten geben.
/// Genau das war schon einmal so: das Backend trennte bei einer halben
/// Schriftgröße, der Provider bei einem Viertel — und derselbe Betrag "112.50"
/// wurde im DOCX-Export zu "1 12.50", während die Textauswahl ihn richtig hatte.
namespace PdfiumTextRules {

/// Trennt die Lücke zwischen zwei Zeichen zwei Wörter?
///
/// Wortabstände stehen meist als echte Leerzeichen im Dokument. Tabellenzellen
/// aber nicht — dort wird jede Zelle einzeln positioniert, und ohne diese
/// Prüfung wird aus einer Kopfzeile "PositionBezeichnungMengePreis".
///
/// Bezugsgröße ist die Schriftgröße, NICHT die Kastenhöhe. Am Korpus gemessen:
/// normale Buchstabenabstände 0,9 bis 1,9 pt, CJK-Zeichenabstände bis 4 pt bei
/// 20 pt Schrift, Zellengrenzen 82 bis 115 pt. Die halbe Schriftgröße liegt mit
/// Abstand dazwischen. Nach Kastenhöhe bemessen ging es zweimal schief: bei
/// Ziffern und CJK-Glyphen ist die Tinte schmaler als der Vorschub, und aus
/// "Standardtext" wurde "Stan dardtext".
///
/// Im Zweifel wird NICHT getrennt: eine fehlende Wortgrenze ist kosmetisch,
/// eine falsche mitten im Wort zerstört Kopieren und Suchen.
inline bool separatesWords(const QRectF &prev, const QRectF &next, double fontSize)
{
    const double gap = next.left() - prev.right();
    return gap > qMax(2.0, fontSize * 0.5);
}

inline bool separatesBlocks(const QRectF &prev, const QRectF &next, double fontSize)
{
    const double gap = next.left() - prev.right();
    return gap > qMax(12.0, fontSize * 2.5);
}

inline bool sameGlyph(const QRectF &a, const QRectF &b)
{
    const QRectF hit = a.intersected(b);
    if (hit.isEmpty()) return false;
    const double areaA = a.width() * a.height();
    const double areaB = b.width() * b.height();
    const double small = qMax(1e-6, qMin(areaA, areaB));
    return hit.width() * hit.height() / small > 0.7;
}

/// Liegen zwei Zeichen auf derselben Zeile?
///
/// Verglichen werden Schriftgrundlinien, nicht Kastenmitten: ein Komma hängt
/// unter die Grundlinie, ein Großbuchstabe ragt darüber, und nach Mitten
/// gruppiert brach eine Zeile genau am Komma.
inline bool sameLine(double baselineA, double baselineB, double charHeight)
{
    return qAbs(baselineA - baselineB) <= qMax(2.0, charHeight * 0.5);
}

} // namespace PdfiumTextRules

#endif
