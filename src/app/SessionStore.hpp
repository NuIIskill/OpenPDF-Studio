#pragma once

#include <QString>

/// Working copies of documents that have uncommitted changes.
namespace SessionStore {

QString directory();

QString newWorkingFile(const QString &sourcePath);

bool isWorkingFile(const QString &path);

void discard(const QString &path);

QString snapshotDirectory();

QString newSnapshotFile(const QString &sourcePath);

bool isSnapshotFile(const QString &path);

void discardSnapshot(const QString &path);

void pruneSnapshots(int maxAgeDays = 7);

}
