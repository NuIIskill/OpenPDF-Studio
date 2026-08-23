#pragma once

#include "engine/document/OrganizerDoc.hpp"

#include <QList>
#include <QMap>
#include <QString>

// One page of the organized document: where it comes from, how it is turned.
struct OrganizerPage {
    QString pdfPath;
    int     pageIndex { 0 };
    bool    isBlank   { false };
    int     rotation  { 0 };   // 0, 90, 180, 270
};

// Writes an organized page list to a PDF. Tries a vector save that copies the
// source pages, falls back to rasterising them.
class OrganizerWriter
{
public:
    enum class Error { None, NoBackend, RenderFailures, WriteFailed };
    struct Result {
        bool  ok { false };
        Error error { Error::None };
        int   lostPages { 0 };   // RenderFailures: how many did not render
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
