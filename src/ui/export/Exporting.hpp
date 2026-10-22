#pragma once

#include "ui/export/ExportDialog.hpp"

#include <QCoreApplication>

class DocumentView;
class SaveIndicator;

QT_BEGIN_NAMESPACE
class QWidget;
QT_END_NAMESPACE

/// Writes the document in the format chosen in the export dialog and reports the result.
class Exporting
{
    Q_DECLARE_TR_FUNCTIONS(Exporting)

public:
    static void run(QWidget *parent, DocumentView *view, const ExportRequest &request,
                    SaveIndicator *indicator);
};
