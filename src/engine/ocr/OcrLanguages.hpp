#pragma once

#include <QByteArray>
#include <QString>
#include <QStringList>
#include <QUrl>

/// The text recognition languages the program offers and which of them are installed.
namespace OcrLanguages {

const QStringList &offered();

QString dataDir();

QStringList installed();

QString userDir();

bool seedUserDir();

QUrl source(const QString &code);

QByteArray sha256(const QString &code);

bool remove(const QString &code);

}
