#pragma once

#include <QList>
#include <QString>

/// Working copies of documents that have uncommitted changes.
namespace SessionStore {

/// One document as an earlier run of the program left it.
struct OpenDocument {
    QString target;
    QString content;
    int     page  { 0 };
    bool    dirty { false };
};

QString directory();

QString newWorkingFile(const QString &sourcePath);

bool isWorkingFile(const QString &path);

void discard(const QString &path);

QString snapshotDirectory();

QString newSnapshotFile(const QString &sourcePath);

bool isSnapshotFile(const QString &path);

void discardSnapshot(const QString &path);

void pruneSnapshots(int maxAgeDays = 7);

bool beginSession();

void updateSession(const QList<OpenDocument> &documents);

void endSession();

QList<OpenDocument> takeAbandonedDocuments();

}
