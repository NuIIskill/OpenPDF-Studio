#pragma once

#include <QCoreApplication>
#include <QString>

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
    enum class Result { Converted, Failed, WindowClosing };

    static Result convert(QWidget *window, DocumentView *busyView, SaveIndicator *indicator,
                          const QString &path, const QString &working, QString *error);
};
