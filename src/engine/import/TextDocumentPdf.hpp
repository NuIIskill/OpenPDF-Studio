#pragma once

#include "engine/document/WriteGrant.hpp"
#include "engine/import/DocumentBuilder.hpp"

#include <QList>
#include <QString>

QT_BEGIN_NAMESPACE
class QPageLayout;
class QTextDocument;
QT_END_NAMESPACE

/// Writes a QTextDocument into a PDF, one page at a time.
namespace TextDocumentPdf {

bool write(QTextDocument *document, const QPageLayout &layout,
           const WriteGrant &outPdf, QString *error = nullptr,
           const QList<DocumentBuilder::Decoration> &decorations = {});

}
