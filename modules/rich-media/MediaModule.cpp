// SPDX-License-Identifier: LicenseRef-OpenPDF-Business

#include "rich-media/ui/ConvertCard.hpp"
#include "rich-media/ui/MediaLayer.hpp"
#include "rich-media/ui/RichMediaPanel.hpp"

#include "rich-media/engine/MediaDrop.hpp"

#include "ui/import/Importing.hpp"
#include "ui/panels/LeftSidebar.hpp"
#include "ui/panels/ToolPanels.hpp"
#include "ui/settings/AddOns.hpp"
#include "ui/view/PageOverlay.hpp"

#include <QFile>
#include <QPointer>
#include <QWidget>

namespace {
QPointer<RichMediaPanel> g_panel;
}

RichMediaPanel *richMediaPanel()
{
    return g_panel.data();
}

namespace {

struct Registration
{
    Registration()
    {
        LeftSidebar::registerTool({
            QStringLiteral("video"),
            QStringLiteral("film"),
            QStringLiteral(QT_TR_NOOP("Rich Media")),
             true,
        });

        ToolPanels::add({
            QStringLiteral("video"),
            RichMediaPanel::kWidth,
            [](QWidget *parent) -> QWidget * {
                auto *panel = new RichMediaPanel(parent);
                QObject::connect(panel, &RichMediaPanel::closeRequested,
                                 panel, [panel]() {
                    if (auto *sidebar = panel->window()->findChild<LeftSidebar *>())
                        Q_EMIT sidebar->toolSelected(QStringLiteral("select"));
                });
                g_panel = panel;
                return panel;
            },
        });

        PageOverlays::add([](PageCanvas *canvas, QObject *parent) -> PageOverlay * {
            return new MediaLayer(canvas, parent);
        });

        Importing::addFormat({
            QStringLiteral(QT_TRANSLATE_NOOP("Importing", "Videos")),
            MediaDrop::videoSuffixes(),
            [](QWidget *, const QString &path, const QString &working, QString *) {
                QStringList tempFiles;
                const QString playable = MediaLayer::ensurePlayableSource(path, &tempFiles);
                const bool made = !playable.isEmpty()
                    && MediaDrop::newDocument(playable, working,
                                              MediaDrop::displayNameFor(path, playable));
                for (const QString &temp : std::as_const(tempFiles)) QFile::remove(temp);
                if (playable.isEmpty()) return Importing::Result::Cancelled;
                return made ? Importing::Result::Converted : Importing::Result::Failed;
            },
        });

        AddOns::addCard({ &ConvertCard::build });
    }
};

const Registration g_registration;

}
