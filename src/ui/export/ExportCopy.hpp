#pragma once

#include <QString>
#include <QTemporaryDir>

#include "engine/document/DocumentSource.hpp"
#include "engine/export/DocumentExporter.hpp"

class DocumentView;

/// The document as saving would write it now, unsaved changes included, for exporting and printing to read.
class ExportCopy
{
public:
    explicit ExportCopy(DocumentView *view);
    ~ExportCopy();

    ExportCopy(const ExportCopy &)            = delete;
    ExportCopy &operator=(const ExportCopy &) = delete;

    QString path() const { return m_path; }

#ifdef HAVE_PDF_RENDERING
    bool open();
    DocumentExporter::Sources sources() const;
#endif

private:
    QTemporaryDir  m_dir;
    QString        m_path;
#ifdef HAVE_PDF_RENDERING
    DocumentSource m_source;
#endif
};
