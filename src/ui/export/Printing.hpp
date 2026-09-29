#pragma once

#include <QCoreApplication>

class DocumentView;

QT_BEGIN_NAMESPACE
class QWidget;
QT_END_NAMESPACE

/// Asks for printer and page range, then prints the document.
class Printing
{
    Q_DECLARE_TR_FUNCTIONS(Printing)

public:
    static void run(QWidget *parent, DocumentView *view);
};
