#pragma once

#include <QString>

// Working copies of documents that have uncommitted changes.
//
// Some edits (page reordering, rotation, insertion) cannot be held in memory
// as an overlay the way text and image edits can — they only exist once they
// are written to a PDF. Those go into a session working file: the document the
// user sees is the working file, while the file they opened stays untouched
// until they explicitly save.
//
// The directory is deliberately persistent rather than a QTemporaryDir: the
// files are what a later crash-recovery pass will offer to restore.
namespace SessionStore {

/// Directory holding the working copies. Created on first use.
/// Empty string if it could not be created.
QString directory();

/// Allocates an unused working-file path for `sourcePath` (which may be empty
/// for documents that have no file yet). Does not create the file.
QString newWorkingFile(const QString &sourcePath);

/// True when `path` is a file inside directory().
bool isWorkingFile(const QString &path);

/// Deletes `path` if it is a working file. Ignores everything else, so it is
/// safe to call with a user document path.
void discard(const QString &path);

/// Directory holding the document-history snapshots. A sub-directory of
/// directory() on purpose: snapshots are states the user can go back to, not
/// documents to offer for recovery, and discard() must never reach them.
QString snapshotDirectory();

/// Allocates an unused snapshot path for `sourcePath`. Does not create the file.
QString newSnapshotFile(const QString &sourcePath);

/// True when `path` is a file inside snapshotDirectory().
bool isSnapshotFile(const QString &path);

/// Deletes `path` if it is a snapshot. Ignores everything else.
void discardSnapshot(const QString &path);

/// Deletes snapshots older than `maxAgeDays`. A history belongs to the run
/// that built it and is cleaned up when its document closes; what stays behind
/// is the leftovers of runs that were killed. Called once at startup — old
/// enough that a session still running cannot be holding one.
void pruneSnapshots(int maxAgeDays = 7);

} // namespace SessionStore
