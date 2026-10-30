#pragma once

#include "engine/document/WriteGrant.hpp"

#include <QCoreApplication>
#include <QString>
#include <QStringList>

#include <functional>

class DocumentView;
class SaveIndicator;

QT_BEGIN_NAMESPACE
class QWidget;
QT_END_NAMESPACE

/// Turns a Word, OpenDocument or image file into a PDF working copy while the window keeps painting.
class Importing
{
    Q_DECLARE_TR_FUNCTIONS(Importing)

public:
    enum class Result { Converted, Failed, Cancelled, WindowClosing };

    struct Format
    {
        QString     name;
        QStringList suffixes;
        std::function<Result(QWidget *window, const QString &path,
                             const WriteGrant &working, QString *error)> toPdf;
    };

    static void addFormat(Format format);
    static const Format *formatFor(const QString &path);
    static QString openFilter();

    static Result convert(QWidget *window, DocumentView *busyView, SaveIndicator *indicator,
                          const QString &path, const QString &working, QString *error);
};
