#include "ui/settings/SettingsPanel.hpp"
#include "engine/ocr/OcrLanguages.hpp"
#include "ui/settings/AddOns.hpp"
#include "ui/settings/DownloadControl.hpp"
#include "ui/settings/PackageControl.hpp"

#include <QComboBox>
#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QProcess>
#include <QPushButton>
#include <QStandardPaths>
#include <QUrl>
#include <QVBoxLayout>

namespace {

struct OcrLanguage { const char *code; const char *name; };

const OcrLanguage kOcrLanguages[] = {
    { "deu",     QT_TRANSLATE_NOOP("SettingsPanel", "German")     },
    { "eng",     QT_TRANSLATE_NOOP("SettingsPanel", "English")    },
    { "fra",     QT_TRANSLATE_NOOP("SettingsPanel", "French")     },
    { "spa",     QT_TRANSLATE_NOOP("SettingsPanel", "Spanish")    },
    { "ita",     QT_TRANSLATE_NOOP("SettingsPanel", "Italian")    },
    { "nld",     QT_TRANSLATE_NOOP("SettingsPanel", "Dutch")      },
    { "pol",     QT_TRANSLATE_NOOP("SettingsPanel", "Polish")     },
    { "por",     QT_TRANSLATE_NOOP("SettingsPanel", "Portuguese") },
    { "rus",     QT_TRANSLATE_NOOP("SettingsPanel", "Russian")    },
    { "jpn",     QT_TRANSLATE_NOOP("SettingsPanel", "Japanese")   },
    { "kor",     QT_TRANSLATE_NOOP("SettingsPanel", "Korean")     },
    { "chi_sim", QT_TRANSLATE_NOOP("SettingsPanel", "Chinese")    },
};

[[maybe_unused]] SystemPackages::Names ocrPackages(const QString &code)
{
    QString debian = code;
    debian.replace(u'_', u'-');
    return { { QStringLiteral("tesseract-langpack-") + code },
             { QStringLiteral("tesseract-ocr-") + debian },
             { QStringLiteral("tesseract-data-") + code } };
}

[[maybe_unused]] DownloadControl::Target ocrDownload(const QString &code)
{
    DownloadControl::Target target;
    target.url       = OcrLanguages::source(code);
    target.sha256    = OcrLanguages::sha256(code);
    target.file      = OcrLanguages::userDir() + u'/' + code + QStringLiteral(".traineddata");
    target.installed = [code]() { return OcrLanguages::installed().contains(code); };
    target.prepare   = []() { OcrLanguages::seedUserDir(); };
    target.remove    = [code](const DownloadControl::Done &done) {
        const bool ok = OcrLanguages::remove(code);
        done(ok, ok ? QString()
                    : QCoreApplication::translate("SettingsPanel", "The file could not be deleted."));
    };
    return target;
}

// The desktop's own font management, else the folder for the user's own fonts.
void openFontManagement()
{
#ifdef Q_OS_WIN
    QDesktopServices::openUrl(QUrl(QStringLiteral("ms-settings:fonts")));
#else
    struct Tool { const char *program; QStringList arguments; };
    const QList<Tool> kde {
        { "kcmshell6",      { QStringLiteral("kcm_fontinst") } },
        { "systemsettings", { QStringLiteral("kcm_fontinst") } },
        { "kcmshell5",      { QStringLiteral("kcm_fontinst") } },
    };
    const QList<Tool> others {
        { "gnome-font-viewer", {} },
        { "font-manager",      {} },
    };
    const bool onKde = qEnvironmentVariable("XDG_CURRENT_DESKTOP")
                           .contains(QLatin1String("KDE"), Qt::CaseInsensitive);
    for (const QList<Tool> &group : onKde ? QList { kde, others } : QList { others, kde })
        for (const Tool &tool : group) {
            const QString path = QStandardPaths::findExecutable(QLatin1String(tool.program));
            if (!path.isEmpty() && QProcess::startDetached(path, tool.arguments)) return;
        }

    const QString dir = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)
                      + QStringLiteral("/fonts");
    QDir().mkpath(dir);
    QDesktopServices::openUrl(QUrl::fromLocalFile(dir));
#endif
}

}

QWidget *SettingsPanel::buildAddOnsPage()
{
    QWidget *page = nullptr;
#ifdef Q_OS_WIN
    const QString desc = tr("Optional parts that OpenPDF Studio downloads and removes.");
#else
    const QString desc =
        tr("Optional parts, installed and removed by your system's package manager.");
#endif
    QVBoxLayout *vl = buildScrollPage(page, tr("Add-on Packages"), desc);

#ifdef HAVE_TESSERACT
    buildAddOnsOcr(vl);
#endif
    buildAddOnsFonts(vl);

    for (const AddOns::Card &card : AddOns::cards()) {
        auto *frame = new QFrame(vl->parentWidget());
        frame->setObjectName(QStringLiteral("SettingsCard"));
        frame->setAttribute(Qt::WA_StyledBackground, true);
        auto *cl = new QVBoxLayout(frame);
        cl->setContentsMargins(16, 16, 16, 16);
        cl->setSpacing(8);
        card.build(cl);
        vl->addWidget(frame);
    }

    finishScrollPage(vl);
    m_addOnsPage = page;
    return page;
}

void SettingsPanel::buildAddOnsOcr(QVBoxLayout *vl)
{
    QVBoxLayout *cl = addSettingsCard(vl, tr("Text Recognition (OCR)"));
    QWidget *card = cl->parentWidget();

    auto *desc = new QLabel(tr("Language packs for recognising text in scanned documents."), card);
    desc->setObjectName(QStringLiteral("SettingsRowDesc"));
    desc->setWordWrap(true);
    cl->addWidget(desc);
    cl->addSpacing(4);

    auto *grid = new QGridLayout;
    grid->setHorizontalSpacing(12);
    grid->setVerticalSpacing(10);
    grid->setColumnStretch(1, 1);

    auto *installedLabel = new QLabel(tr("Installed languages:"), card);
    installedLabel->setObjectName(QStringLiteral("SettingsRowLabel"));
    grid->addWidget(installedLabel, 0, 0);
    auto *installedList = new QLabel(card);
    installedList->setWordWrap(true);
    installedList->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    grid->addWidget(installedList, 0, 1, 1, 2);

    auto *languageLabel = new QLabel(tr("Language:"), card);
    languageLabel->setObjectName(QStringLiteral("SettingsRowLabel"));
    grid->addWidget(languageLabel, 1, 0);
    auto *combo = new QComboBox(card);
    combo->setObjectName(QStringLiteral("AdvancedCombo"));
    combo->setFixedWidth(220);
    for (const OcrLanguage &language : kOcrLanguages)
        combo->addItem(tr(language.name), QLatin1String(language.code));
    grid->addWidget(combo, 1, 1, Qt::AlignRight);

#ifdef Q_OS_WIN
    auto *control = new DownloadControl(card);
#else
    auto *control = new PackageControl(card);
#endif
    using Control = std::remove_pointer_t<decltype(control)>;
    grid->addWidget(control->button(), 1, 2);
    cl->addLayout(grid);
    cl->addWidget(control->status());

    const auto showInstalled = [this, installedList]() {
        const QStringList codes = OcrLanguages::installed();
        QStringList names;
        for (const OcrLanguage &language : kOcrLanguages)
            if (codes.contains(QLatin1String(language.code)))
                names << tr(language.name);
        installedList->setText(names.isEmpty() ? tr("None") : names.join(QStringLiteral(", ")));
    };
    const auto choose = [control, combo]() {
        const QString code = combo->currentData().toString();
#ifdef Q_OS_WIN
        control->setTarget(ocrDownload(code));
#else
        control->setTarget(ocrPackages(code), [code]() {
            return OcrLanguages::installed().contains(code);
        });
#endif
    };
    showInstalled();
    // A language not installed yet is the likelier wish.
    const QStringList installed = OcrLanguages::installed();
    for (int i = 0; i < combo->count(); ++i)
        if (!installed.contains(combo->itemData(i).toString())) {
            combo->setCurrentIndex(i);
            break;
        }
    choose();
    connect(combo, &QComboBox::currentIndexChanged, control, choose);
    connect(control, &Control::changed, installedList, showInstalled);
}

void SettingsPanel::buildAddOnsFonts(QVBoxLayout *vl)
{
    QVBoxLayout *cl = addSettingsCard(vl, tr("Fonts"));
    QWidget *card = cl->parentWidget();

    auto *row = new QHBoxLayout;
    row->setSpacing(12);
    auto *desc = new QLabel(tr("Fonts are added and removed in your system's font management. "
                               "OpenPDF Studio uses new fonts after a restart."), card);
    desc->setObjectName(QStringLiteral("SettingsRowDesc"));
    desc->setWordWrap(true);
    row->addWidget(desc, 1);

    auto *manage = new QPushButton(tr("Manage Fonts"), card);
    manage->setObjectName(QStringLiteral("SettingsActionBtn"));
    manage->setCursor(Qt::PointingHandCursor);
    manage->setFixedHeight(28);
    connect(manage, &QPushButton::clicked, this, []() { openFontManagement(); });
    row->addWidget(manage, 0, Qt::AlignVCenter);
    cl->addLayout(row);
}
