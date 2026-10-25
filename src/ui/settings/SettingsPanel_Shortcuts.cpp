#include "ui/settings/SettingsPanel.hpp"
#include "app/AppSettings.hpp"
#include "ui/settings/ShortcutRow.hpp"

#include <QCheckBox>
#include <QFrame>
#include <QHBoxLayout>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QScrollArea>
#include <QVBoxLayout>

QWidget *SettingsPanel::buildShortcutsPage()
{
    auto *page = new QWidget(this);
    page->setObjectName(QStringLiteral("SettingsScrollContent"));
    auto *outer = new QVBoxLayout(page);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);

    {
        auto *hdr = new QWidget(page);
        hdr->setObjectName(QStringLiteral("SettingsScrollContent"));
        auto *vl = new QVBoxLayout(hdr);
        vl->setContentsMargins(32, 28, 32, 16);
        vl->setSpacing(6);
        auto *title = new QLabel(tr("Keyboard Shortcuts"), hdr);
        title->setObjectName(QStringLiteral("SettingsSectionTitle"));
        vl->addWidget(title);
        auto *desc = new QLabel(tr("Configure keyboard shortcuts for frequent actions."), hdr);
        desc->setObjectName(QStringLiteral("SettingsSectionDesc"));
        vl->addWidget(desc);
        outer->addWidget(hdr);
    }

    {
        auto *row = new QWidget(page);
        row->setObjectName(QStringLiteral("SettingsScrollContent"));
        auto *hl = new QHBoxLayout(row);
        hl->setContentsMargins(32, 0, 32, 12);
        hl->setSpacing(12);
        auto *searchEdit = new QLineEdit(row);
        searchEdit->setObjectName(QStringLiteral("ShortcutSearch"));
        searchEdit->setPlaceholderText(tr("Search action"));
        searchEdit->setFixedHeight(34);
        hl->addWidget(searchEdit, 1);
        auto *globalChk = new QCheckBox(tr("Show only global default shortcuts"), row);
        globalChk->setObjectName(QStringLiteral("ShortcutGlobalChk"));
        hl->addWidget(globalChk);
        outer->addWidget(row);

        connect(searchEdit, &QLineEdit::textChanged, this, [this](const QString &text) {
            for (ShortcutRow *r : m_shortcutRows)
                r->setVisible(r->matchesFilter(text));
        });
    }

    {
        auto *hdr = new QWidget(page);
        hdr->setObjectName(QStringLiteral("ShortcutTableHeader"));
        hdr->setFixedHeight(30);
        auto *hl = new QHBoxLayout(hdr);
        hl->setContentsMargins(16, 0, 16, 0);
        hl->setSpacing(8);
        auto *colAction = new QLabel(tr("Action"), hdr);
        colAction->setObjectName(QStringLiteral("ShortcutColHeader"));
        colAction->setFixedWidth(260);
        hl->addWidget(colAction);
        auto *colKey = new QLabel(tr("Key Combination"), hdr);
        colKey->setObjectName(QStringLiteral("ShortcutColHeader"));
        hl->addWidget(colKey, 1);
        outer->addWidget(hdr);
    }

    {
        auto *sep = new QFrame(page);
        sep->setObjectName(QStringLiteral("SettingsSeparator"));
        sep->setFrameShape(QFrame::HLine);
        outer->addWidget(sep);
    }

    {
        auto *scroll = new QScrollArea(page);
        scroll->setObjectName(QStringLiteral("SettingsScroll"));
        scroll->setFrameShape(QFrame::NoFrame);
        scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        scroll->setWidgetResizable(true);

        auto *listW = new QWidget;
        listW->setObjectName(QStringLiteral("SettingsScrollContent"));
        auto *vl = new QVBoxLayout(listW);
        vl->setContentsMargins(0, 0, 0, 0);
        vl->setSpacing(0);

        struct Def { const char *label; const char *key; const char *seq; };
        static const Def kDefs[] = {
            { QT_TR_NOOP("Save"),          "save",     "Ctrl+S"       },
            { QT_TR_NOOP("Save As"),       "saveas",   "Ctrl+Shift+S" },
            { QT_TR_NOOP("Print"),         "print",    "Ctrl+P"       },
            { QT_TR_NOOP("Open"),          "open",     "Ctrl+O"       },
            { QT_TR_NOOP("Add to Current Document"), "openhere", "Ctrl+Shift+O" },
            { QT_TR_NOOP("Undo"),          "undo",     "Ctrl+Z"       },
            { QT_TR_NOOP("Redo"),          "redo",     "Ctrl+Y"       },
            { QT_TR_NOOP("Find"),          "find",     "Ctrl+F"       },
            { QT_TR_NOOP("Text Tool"),     "texttool", "T"            },
            { QT_TR_NOOP("Notes"),         "comment",  "N"            },
            { QT_TR_NOOP("Zoom In"),       "zoomin",       "Ctrl++"       },
            { QT_TR_NOOP("Zoom Out"),      "zoomout",      "Ctrl+-"       },
            { QT_TR_NOOP("Presentation"),  "presentation", "F5"           },
        };

        m_shortcutRows.clear();
        m_shortcutKeys.clear();
        bool first = true;
        for (const auto &def : kDefs) {
            if (!first) {
                auto *sep = new QFrame(listW);
                sep->setObjectName(QStringLiteral("SettingsSeparator"));
                sep->setFrameShape(QFrame::HLine);
                vl->addWidget(sep);
            }
            first = false;

            const QKeySequence appDefault = QKeySequence::fromString(
                QLatin1String(def.seq), QKeySequence::PortableText);
            const QKeySequence saved = m_settings->shortcut(
                QLatin1String(def.key), appDefault);

            auto *srow = new ShortcutRow(tr(def.label), appDefault, listW);
            srow->setCurrentSequence(saved);

            m_shortcutRows.append(srow);
            m_shortcutKeys.append(QLatin1String(def.key));

            connect(srow, &ShortcutRow::editStarted, this, [this, srow]() {
                for (ShortcutRow *r : m_shortcutRows)
                    if (r != srow) r->cancelIfEditing();
            });
            vl->addWidget(srow);
        }
        vl->addStretch(1);
        scroll->setWidget(listW);
        outer->addWidget(scroll, 1);
    }

    {
        auto *bar = new QFrame(page);
        bar->setObjectName(QStringLiteral("ShortcutInfoBar"));
        bar->setFrameShape(QFrame::NoFrame);
        bar->setFixedHeight(42);
        auto *hl = new QHBoxLayout(bar);
        hl->setContentsMargins(20, 0, 20, 0);
        hl->setSpacing(8);
        auto *icon = new QLabel(QStringLiteral("ℹ"), bar);
        icon->setObjectName(QStringLiteral("ShortcutInfoIcon"));
        hl->addWidget(icon);
        auto *text = new QLabel(tr("Double-click on a key combination to change it."), bar);
        text->setObjectName(QStringLiteral("ShortcutInfoText"));
        hl->addWidget(text, 1);
        outer->addWidget(bar);
    }

    return page;
}
