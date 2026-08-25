// SPDX-License-Identifier: LicenseRef-OpenPDF-Business
//
// Where the module registers with the program. The Core does not know
// modules/ and must not, so it carries three registers and this is the only
// place that fills them: the tool, its panel and the page overlay.
//
// Registered from a static initializer, before main(). That holds because all
// sources compile into the same binary, so nothing can drop this file.

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

/// The window's one insert panel. Null until it has been built.
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
            /*needsEditMode=*/true,
        });

        ToolPanels::add({
            QStringLiteral("video"),
            RichMediaPanel::kWidth,
            [](QWidget *parent) -> QWidget * {
                auto *panel = new RichMediaPanel(parent);
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

} // namespace
