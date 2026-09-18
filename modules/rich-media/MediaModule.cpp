// SPDX-License-Identifier: LicenseRef-OpenPDF-Business

#include "rich-media/ui/MediaLayer.hpp"
#include "rich-media/ui/RichMediaPanel.hpp"

#include "ui/panels/LeftSidebar.hpp"
#include "ui/panels/ToolPanels.hpp"
#include "ui/view/PageOverlay.hpp"

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
    }
};

const Registration g_registration;

}
