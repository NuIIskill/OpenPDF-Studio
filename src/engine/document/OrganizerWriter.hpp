#pragma once

#include "engine/document/OrganizerDoc.hpp"

#include <QList>
#include <QMap>
#include <QString>

/// Describes one source page and its organizer transformations.
struct OrganizerPage {
    QString pdfPath;
    int     pageIndex { 0 };
    bool    isBlank   { false };
    int     rotation  { 0 };
};

/// Writes an organized page list to a PDF.
class OrganizerWriter
{
public:
    enum class Error { None, NoBackend, RenderFailures, WriteFailed };
    struct Result {
        bool  ok { false };
        Error error { Error::None };
        int   lostPages { 0 };
        int   totalPages { 0 };
    };

    OrganizerWriter(const QList<OrganizerPage> &pages,
                    const QMap<QString, OrganizerDoc *> &docs)
        : m_pages(pages), m_docs(docs) {}

    Result write(const QString &outPath);

private:
    bool writeVector(const QString &outPath);
    bool verifyWritten(const QString &path) const;

    const QList<OrganizerPage>          &m_pages;
    const QMap<QString, OrganizerDoc *> &m_docs;
};
