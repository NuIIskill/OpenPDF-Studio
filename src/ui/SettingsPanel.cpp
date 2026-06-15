#include "SettingsPanel.hpp"
#include "app/AppSettings.hpp"
#include "ui/theme/Theme.hpp"

#include <QVBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QButtonGroup>

SettingsPanel::SettingsPanel(AppSettings *settings, QWidget *parent)
    : QDialog(parent, Qt::Dialog | Qt::FramelessWindowHint)
    , m_settings(settings)
{
    setObjectName(QStringLiteral("SettingsPanel"));
    setAttribute(Qt::WA_StyledBackground, true);
    setModal(true);
    buildUi(settings->theme());
}

void SettingsPanel::buildUi(const QString &currentMode)
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(16, 14, 16, 14);
    root->setSpacing(4);

    auto *title = new QLabel(tr("Erscheinungsbild"), this);
    title->setObjectName(QStringLiteral("SettingsPanelTitle"));
    root->addWidget(title);
    root->addSpacing(6);

    struct Option { const char *id; const char *label; };
    const Option opts[] = {
        { "system", "System (automatisch)" },
        { "light",  "Hell"   },
        { "dark",   "Dunkel" },
    };

    auto *group = new QButtonGroup(this);
    for (const auto &opt : opts) {
        auto *btn = new QPushButton(tr(opt.label), this);
        btn->setObjectName(QStringLiteral("ThemeOptionBtn"));
        btn->setCheckable(true);
        btn->setChecked(QLatin1String(opt.id) == currentMode);
        btn->setFlat(true);
        group->addButton(btn);
        const QString id = QLatin1String(opt.id);
        connect(btn, &QPushButton::clicked, this, [this, id]() {
            m_settings->setTheme(id);
            m_settings->sync();
            Q_EMIT themeChangeRequested(id);
            accept();
        });
        root->addWidget(btn);
    }
    setFixedWidth(220);
    adjustSize();
}

void SettingsPanel::showNear(QWidget *anchor)
{
    adjustSize();
    const QPoint globalAnchor = anchor->mapToGlobal(QPoint(0, 0));
    move(globalAnchor.x() + anchor->width() + 8,
         globalAnchor.y() - height() + anchor->height());
    exec();
}
