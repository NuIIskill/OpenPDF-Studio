#pragma once

#include <QString>

// Writing a PDF into the very file it is being generated from destroys it:
// both qpdf and the Qt PDF reader resolve objects lazily, while the writer
// truncates the target the moment it opens it — every object still to be read
// is then gone. The symptom is a file that still has the right number of pages
// but no content at all.
//
// So no save writes to its target directly. It writes to a staging file next to
// it and swaps that in once the write completed, which also means an aborted or
// crashed save leaves the user's document untouched.
namespace SafeWrite {

/// Path to write to instead of `target` — same directory, so the swap is a
/// rename within one filesystem. Returns an empty string if no free name exists.
QString stagingPath(const QString &target);

/// Moves a finished staging file onto `target`. The previous target is only
/// removed after the new file is in place; on failure it is restored and the
/// staging file is dropped.
bool commit(const QString &stagingPath, const QString &target);

/// Removes a staging file after a failed write. Safe to call with any path.
void discard(const QString &stagingPath);

} // namespace SafeWrite
