#include "cli/Commands.hpp"
#include "app/AppSettings.hpp"
#include "drm/LicenseNotice.hpp"
#include "engine/historymanager/DocumentHistory.hpp"
#include "ui/PresentationWindow.hpp"
#include "ui/export/ExportDialog.hpp"
#include "ui/history/HistoryDialog.hpp"
#include "ui/organizer/PdfOrganizerDialog.hpp"
#include "ui/settings/SettingsPanel.hpp"
#include "ui/theme/Theme.hpp"

#include <QApplication>
#include <QMessageBox>

namespace Cli {

int shotExportDialog(const QStringList &args)
{
    ExportDialog dlg(QStringLiteral("/tmp/demo.pdf"), 4, 0);
    if (args.size() >= 4)
        dlg.selectFormatForTest(args.at(3));
    dlg.show();
    QApplication::processEvents();
    return dlg.grab().save(args.at(2)) ? 0 : 3;
}

int shotHistoryDialog(const QStringList &args)
{
    using Kind = DocumentHistory::Kind;
    if (args.size() >= 4 && args.at(3) == QLatin1String("dark"))
        Theme::apply(QStringLiteral("dark"));
    DocumentHistory history;
    history.record({ Kind::Opened, -1, 4, 0, QStringLiteral("Vertrag.pdf") }, 0);
    history.record({ Kind::PageDeleted, 3 }, 0);
    history.record({ Kind::PageRotated, 0, 1, 90 }, 0);
    history.record({ Kind::ImageInserted, 1 }, 0);
    history.record({ Kind::TextEdited, 0 }, 1);
    HistoryDialog dlg(&history, QStringLiteral("Vertrag.pdf"));
    dlg.setUndoRedoAvailable(true, false);
    dlg.show();
    QApplication::processEvents();
    return dlg.grab().save(args.at(2)) ? 0 : 3;
}

int shotLicenseNotice(const QStringList &args)
{
    if (args.size() >= 4)
        Theme::apply(args.at(3));
    QWidget host;
    LicenseNotice::showExpiryReminderIfDue(&host, {});
    QApplication::processEvents();
    for (QWidget *w : QApplication::topLevelWidgets())
        if (auto *box = qobject_cast<QMessageBox *>(w))
            return box->grab().save(args.at(2)) ? 0 : 3;
    return 3;
}

int shotSettings(const QStringList &args)
{
    if (args.size() >= 5)
        Theme::apply(args.at(4));
    AppSettings settings;
    SettingsPanel dlg(&settings);
    if (args.size() >= 4)
        dlg.selectPageForTest(args.at(3));
    dlg.show();
    QApplication::processEvents();
    return dlg.grab().save(args.at(2)) ? 0 : 3;
}

int shotOrganizer(const QStringList &args)
{
    if (args.size() >= 5)
        Theme::apply(args.at(4));
    PdfOrganizerDialog dlg(args.at(3));
    dlg.resize(1200, 800);
    dlg.show();
    settle(1500);
    return dlg.grab().save(args.at(2)) ? 0 : 3;
}

int shotPresentation(const QStringList &args)
{
    int page = 1;
    for (int a = 4; a < args.size(); ++a)
        if (args.at(a).startsWith(QLatin1String("page=")))
            page = args.at(a).mid(5).toInt();

    auto *pw = new PresentationWindow(args.at(3), page - 1);
    pw->resize(1280, 900);
    settle(1500);
    const bool ok = pw->grab().save(args.at(2), "PNG");
    pw->close();
    return ok ? 0 : 3;
}

}
