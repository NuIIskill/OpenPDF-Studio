#pragma once

#include <QList>
#include <QPoint>
#include <QStringList>

class QWidget;

/// The individual command-line modes and the helpers they share.
namespace Cli {

int exportPdfCommand(const QStringList &args);
int exportDocxCommand(const QStringList &args);
int exportImagesCommand(const QStringList &args);
int importPdfCommand(const QStringList &args);

int selectTextCommand(const QStringList &args);
int applyEditCommand(const QStringList &args);
int organizeSaveCommand(const QStringList &args);

int shotExportDialog(const QStringList &args);
int shotHistoryDialog(const QStringList &args);
int shotLicenseNotice(const QStringList &args);
int shotSettings(const QStringList &args);
int shotOrganizer(const QStringList &args);
int shotPresentation(const QStringList &args);
int shotWindow(const QStringList &args);

void appendPages(const QString &spec, QList<int> &pages);
void settle(int ms);
void click(QWidget *target, const QPoint &at);

}
