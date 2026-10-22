#pragma once

#include <QByteArray>
#include <QList>
#include <QPair>

/// Wraps a bare CFF font program, as PDFs embed it, in the OpenType tables PDFium needs to write with it.
namespace CffFont {

bool isBare(const QByteArray &data);

QList<QPair<QByteArray, QByteArray>> tables(const QByteArray &cff, bool bold);

}
