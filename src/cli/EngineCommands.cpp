#include "cli/Commands.hpp"
#include "app/PdfPwStore.hpp"
#include "ui/organizer/PdfOrganizerDialog.hpp"
#ifdef HAVE_PDF_RENDERING
#  include "engine/document/DocumentSource.hpp"
#  include "engine/document/PdfBackend.hpp"
#  include "engine/edit/EditSession.hpp"
#endif

#include <QImage>
#include <QPointF>
#include <QTextStream>
#include <optional>

namespace Cli {

int selectTextCommand(const QStringList &args)
{
#ifdef HAVE_PDF_RENDERING
    int page = 1;
    std::optional<QPointF> from, to;
    const auto parsePoint = [](const QString &v) -> std::optional<QPointF> {
        const QStringList xy = v.split(u',');
        if (xy.size() != 2) return std::nullopt;
        return QPointF(xy.at(0).toDouble(), xy.at(1).toDouble());
    };
    for (int a = 3; a < args.size(); ++a) {
        const QString o = args.at(a);
        if      (o.startsWith(QLatin1String("srcpw="))) PdfPwStore::set(args.at(2), o.mid(6));
        else if (o.startsWith(QLatin1String("page=")))  page = o.mid(5).toInt();
        else if (o.startsWith(QLatin1String("from=")))  from = parsePoint(o.mid(5));
        else if (o.startsWith(QLatin1String("to=")))    to   = parsePoint(o.mid(3));
    }

    DocumentSource src;
    if (!src.open(args.at(2), nullptr)) return 2;
    auto *backend = src.backend();
    if (!backend) return 3;
    const PdfBackend::Selection sel = backend->selectPage(page - 1, from, to);
    QTextStream out(stdout);
    out << "backend=" << backend->name() << "\n";
    out << "rects=" << sel.rects.size() << "\n";
    for (const QRectF &r : sel.rects) {
        out << QStringLiteral("rect %1,%2 %3x%4\n")
                   .arg(r.x(), 0, 'f', 1).arg(r.y(), 0, 'f', 1)
                   .arg(r.width(), 0, 'f', 1).arg(r.height(), 0, 'f', 1);
    }
    out << "text<<\n" << sel.text << "\n>>text\n";
    return sel.text.isEmpty() && sel.rects.isEmpty() ? 1 : 0;
#else
    Q_UNUSED(args);
    return 3;
#endif
}

int applyEditCommand(const QStringList &args)
{
#ifdef HAVE_PDF_RENDERING
    int     page = 1;
    QPointF at;
    QString replacement;
    QString fieldName;
    for (int a = 4; a < args.size(); ++a) {
        const QString o = args.at(a);
        if      (o.startsWith(QLatin1String("srcpw="))) PdfPwStore::set(args.at(2), o.mid(6));
        else if (o.startsWith(QLatin1String("page=")))  page = o.mid(5).toInt();
        else if (o.startsWith(QLatin1String("text=")))  replacement = o.mid(5);
        else if (o.startsWith(QLatin1String("field="))) fieldName = o.mid(6);
        else if (o.startsWith(QLatin1String("at="))) {
            const QStringList xy = o.mid(3).split(u',');
            if (xy.size() == 2) at = QPointF(xy.at(0).toDouble(), xy.at(1).toDouble());
        }
    }

    DocumentSource src;
    if (!src.open(args.at(2), nullptr)) return 2;
    auto *backend = src.backend();
    if (!backend) return 3;

    QTextStream out(stdout);
    out << "backend=" << backend->name() << "\n";

    if (!fieldName.isEmpty()) {
        EditSession fieldSession;
        EditSession::Edit fieldEdit;
        fieldEdit.page      = page - 1;
        fieldEdit.formField = fieldName;
        fieldEdit.newText   = replacement;
        fieldSession.addEdit(fieldEdit);
        return backend->saveWithEdits(args.at(3), fieldSession) ? 0 : 3;
    }

    const TextBlock block = backend->textAt(page - 1, at);
    if (!block.isValid()) {
        QTextStream(stdout) << "kein Text an dieser Stelle\n";
        return 4;
    }

    EditSession session;
    EditSession::Edit blank;
    blank.page         = page - 1;
    blank.pdfBounds    = block.pdfBounds;
    blank.sourceRect   = block.pdfBounds;
    blank.originalText = block.text;
    blank.eraseRects   = backend->glyphRects(page - 1, block.pdfBounds);
    session.addEdit(blank);

    EditSession::Edit edit;
    edit.page         = page - 1;
    edit.pdfBounds    = block.pdfBounds;
    edit.sourceRect   = block.pdfBounds;
    edit.originalText = block.text;
    edit.newText      = replacement;
    session.addEdit(edit);

    out << QStringLiteral("block=%1,%2,%3,%4\n")
               .arg(block.pdfBounds.x(), 0, 'f', 1)
               .arg(block.pdfBounds.y(), 0, 'f', 1)
               .arg(block.pdfBounds.width(), 0, 'f', 1)
               .arg(block.pdfBounds.height(), 0, 'f', 1);
    out << "ersetzt<<\n" << block.text << "\n>>ersetzt\n";

    for (int a = 4; a < args.size(); ++a) {
        if (!args.at(a).startsWith(QLatin1String("preview="))) continue;
        const QImage shot = backend->renderPage(page - 1, 2.0, &session);
        if (shot.isNull() || !shot.save(args.at(a).mid(8), "PNG")) return 3;
        out << "vorschau=" << args.at(a).mid(8) << "\n";
        break;
    }
    return backend->saveWithEdits(args.at(3), session) ? 0 : 3;
#else
    Q_UNUSED(args);
    return 3;
#endif
}

int organizeSaveCommand(const QStringList &args)
{
    PdfOrganizerDialog dlg(args.at(2));
    return dlg.writeForTest(args.at(3)) ? 0 : 3;
}

}
