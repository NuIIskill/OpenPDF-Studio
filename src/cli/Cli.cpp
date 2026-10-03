#include "cli/Cli.hpp"
#include "cli/Commands.hpp"

#include <QApplication>
#include <QEventLoop>
#include <QMouseEvent>
#include <QTimer>
#include <QWidget>

namespace Cli {

namespace {

struct Command {
    const char *name;
    int         minArgs;
    int       (*run)(const QStringList &args);
};

constexpr Command kCommands[] = {
    { "--shot-export-dialog",  3, shotExportDialog },
    { "--shot-history-dialog", 3, shotHistoryDialog },
    { "--shot-license-notice", 3, shotLicenseNotice },
    { "--shot-settings",       3, shotSettings },
    { "--organize-save",       4, organizeSaveCommand },
    { "--shot-organizer",      4, shotOrganizer },
    { "--export-pdf",          4, exportPdfCommand },
    { "--export-docx",         4, exportDocxCommand },
    { "--export-images",       4, exportImagesCommand },
    { "--import-pdf",          4, importPdfCommand },
    { "--select-text",         3, selectTextCommand },
    { "--apply-edit",          4, applyEditCommand },
    { "--shot-presentation",   4, shotPresentation },
    { "--shot-window",         4, shotWindow },
};

}

std::optional<int> run(const QStringList &args)
{
    if (args.size() < 2) return std::nullopt;
    for (const Command &command : kCommands)
        if (args.at(1) == QLatin1String(command.name))
            return args.size() >= command.minArgs ? std::optional(command.run(args))
                                                  : std::nullopt;
    return std::nullopt;
}

void appendPages(const QString &spec, QList<int> &pages)
{
    for (const QString &part : spec.split(u',', Qt::SkipEmptyParts)) {
        const int dash = part.indexOf(u'-');
        const int from = dash < 0 ? part.toInt() : part.left(dash).toInt();
        const int to   = dash < 0 ? from : part.mid(dash + 1).toInt();
        for (int p = from; p <= to; ++p) pages.append(p - 1);
    }
}

void settle(int ms)
{
    QEventLoop loop;
    QTimer::singleShot(ms, &loop, &QEventLoop::quit);
    loop.exec();
}

void click(QWidget *target, const QPoint &at)
{
    for (const QEvent::Type type : { QEvent::MouseButtonPress,
                                     QEvent::MouseButtonRelease }) {
        QMouseEvent me(type, QPointF(at), target->mapToGlobal(QPointF(at)),
                       Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(target, &me);
    }
}

}
