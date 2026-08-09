#pragma once

#include <QString>
#include <string>

// ── PdfPwStore ────────────────────────────────────────────────────────────────
// Holds the passwords the user typed for encrypted documents, keyed by absolute
// file path, for the lifetime of the process only.
//
// An open document is read by several independent subsystems — the renderer,
// the qpdf content scanner, the edit session, the image layer, the exporter —
// each opening the file on its own. Threading a pw argument through all of them
// means every new reader is one more chance to forget it and hand the user a
// silent failure. They ask here instead.
//
// Nothing is ever written to disk: the passwords live in memory and vanish with
// the process. forget() drops one document, clear() drops all.
namespace PdfPwStore {

void    set(const QString &filePath, const QString &pw);
QString get(const QString &filePath);          // empty when none is known
bool    has(const QString &filePath);
void    forget(const QString &filePath);
void    clear();

// qpdf takes `char const*` and treats nullptr as "no password". Keeping the
// std::string alive is the caller's job, hence the by-value return.
std::string forQpdf(const QString &filePath);

} // namespace PdfPwStore
