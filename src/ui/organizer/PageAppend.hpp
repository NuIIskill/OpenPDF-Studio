#pragma once

#include <QString>

class DocumentView;

/// Puts the pages of another PDF behind the document open in a view.
namespace PageAppend {

bool append(DocumentView *view, const QString &otherPdf);

}
