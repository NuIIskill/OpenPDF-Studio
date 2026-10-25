#include "ui/settings/SettingsPanel.hpp"
#include "ui/settings/LangRow.hpp"

#include <QFrame>
#include <QLabel>
#include <QScrollArea>
#include <QVBoxLayout>

QWidget *SettingsPanel::buildLanguagePage()
{
    auto *page = new QWidget(this);
    page->setObjectName(QStringLiteral("SettingsScrollContent"));
    auto *vbox = new QVBoxLayout(page);
    vbox->setContentsMargins(0, 28, 0, 0);
    vbox->setSpacing(0);

    auto *titleWrap = new QWidget(page);
    titleWrap->setObjectName(QStringLiteral("SettingsScrollContent"));
    auto *twl = new QVBoxLayout(titleWrap);
    twl->setContentsMargins(32, 0, 32, 0);
    twl->setSpacing(6);

    auto *title = new QLabel(tr("Language"), titleWrap);
    title->setObjectName(QStringLiteral("SettingsSectionTitle"));
    twl->addWidget(title);

    auto *desc = new QLabel(tr("Choose your preferred interface language."), titleWrap);
    desc->setObjectName(QStringLiteral("SettingsSectionDesc"));
    twl->addWidget(desc);

    vbox->addWidget(titleWrap);
    vbox->addSpacing(16);

    auto *scroll = new QScrollArea(page);
    scroll->setObjectName(QStringLiteral("SettingsScroll"));
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scroll->setWidgetResizable(true);

    auto *listWidget = new QWidget;
    listWidget->setObjectName(QStringLiteral("SettingsScrollContent"));
    auto *listLayout = new QVBoxLayout(listWidget);
    listLayout->setContentsMargins(0, 0, 0, 0);
    listLayout->setSpacing(0);

    /// Defines one selectable interface language.
    struct L { const char *code; const char *display; };
    const L langs[] = {
        { "en", "English"    },
        { nullptr, nullptr   },
        { "de", "Deutsch"    },
        { "fr", "Français"   },
        { "es", "Español"    },
        { "it", "Italiano"   },
        { "pt", "Português"  },
        { "nl", "Nederlands" },
        { "pl", "Polski"     },
        { "ru", "Русский"    },
        { "zh", "中文"        },
        { "ja", "日本語"      },
        { "ko", "한국어"      },
    };

    for (const auto &l : langs) {
        if (!l.code) {

            auto *sep = new QFrame(listWidget);
            sep->setObjectName(QStringLiteral("SettingsSeparator"));
            sep->setFrameShape(QFrame::HLine);
            sep->setFixedHeight(1);
            listLayout->addWidget(sep);
        } else {
            addLangRow(listWidget, listLayout, QLatin1String(l.code),
                       QString::fromUtf8(l.display));
        }
    }

    listLayout->addStretch(1);
    scroll->setWidget(listWidget);
    vbox->addWidget(scroll, 1);
    return page;
}

void SettingsPanel::addLangRow(QWidget *parent, QVBoxLayout *layout,
                                const QString &code, const QString &display)
{
    auto *row = new LangRow(code, display, parent);
    row->setSelected(code == m_pendingLang);
    m_langRows.append(row);
    m_langCodes.append(code);

    connect(row, &LangRow::clicked, this, [this, code]() {
        selectLangCode(code);
        m_pendingLang = code;
    });
    layout->addWidget(row);
}

void SettingsPanel::selectLangCode(const QString &code)
{
    for (int i = 0; i < m_langRows.size(); ++i)
        qobject_cast<LangRow *>(m_langRows[i])->setSelected(m_langCodes[i] == code);
}
