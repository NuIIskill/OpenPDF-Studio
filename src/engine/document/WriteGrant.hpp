#pragma once

#include <QString>

/// Permission to write one PDF file, handed out only where the history manager keeps track of what is written.
class WriteGrant
{
public:
    const QString &path() const { return m_path; }

    WriteGrant beside(const QString &suffix) const { return WriteGrant(m_path + suffix); }

    static WriteGrant scratch(const QString &name);
    static bool isScratch(const QString &path);

private:
    explicit WriteGrant(const QString &path) : m_path(path) {}

    QString m_path;

    // The issuers. Adding one needs the owner's consent: CLAUDE.md, rule 6.
    friend class DocumentView;
    friend class Exporting;
    friend class Importing;
    friend struct CliWrites;
};
