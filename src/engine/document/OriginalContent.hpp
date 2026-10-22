#pragma once

#include <QByteArray>
#include <QHash>
#include <QList>
#include <QMap>
#include <QSet>
#include <QString>
#include <QTransform>

/// Corrects a saved file with qpdf: original page content and the ToUnicode maps of the fonts the editor embedded.
namespace OriginalContent {

enum ObjectType { Text = 1, Path = 2, Image = 3, Shading = 4, Form = 5 };

struct Scan {
    QList<int>  objects;
    QList<bool> spacedText;
};

struct Page {
    QSet<int>              removed;
    QHash<int, QTransform> textMatrices;
    bool                   appended { false };
};

QHash<int, Scan> scan(const QString &source, const QList<int> &pages);

bool restore(const QString &source, const QString &output, const QHash<int, Page> &pages,
             const QHash<QByteArray, QMap<quint32, uint>> &toUnicode);

}
