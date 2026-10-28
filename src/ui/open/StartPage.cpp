#include "ui/open/StartPage.hpp"

#include "app/AppSettings.hpp"
#include "ui/theme/Theme.hpp"

#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QEvent>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QLocale>
#include <QMenu>
#include <QProcess>
#include <QPushButton>
#include <QUrl>
#include <QVBoxLayout>

namespace {

constexpr int kWidth    = 540;
constexpr int kIconSize = 18;
constexpr int kArtSize  = 64;

class ElidedLabel : public QLabel
{
public:
    ElidedLabel(const QString &text, const char *name, QWidget *parent)
        : QLabel(text, parent)
        , m_full(text)
    {
        setObjectName(QLatin1String(name));
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    }

protected:
    void resizeEvent(QResizeEvent *e) override
    {
        QLabel::resizeEvent(e);
        setText(fontMetrics().elidedText(m_full, Qt::ElideMiddle, width()));
    }

private:
    QString m_full;
};

class LayoutButton : public QPushButton
{
public:
    using QPushButton::QPushButton;

    QSize sizeHint() const override
    {
        return layout() ? layout()->sizeHint() : QPushButton::sizeHint();
    }
    QSize minimumSizeHint() const override { return sizeHint(); }
};

QLabel *passiveLabel(const char *name, QWidget *parent)
{
    auto *label = new QLabel(parent);
    label->setObjectName(QLatin1String(name));
    label->setAttribute(Qt::WA_TransparentForMouseEvents);
    return label;
}

void showInFolder(const QString &path)
{
#ifdef Q_OS_WIN
    QProcess::startDetached(QStringLiteral("explorer.exe"),
                            { QStringLiteral("/select,"), QDir::toNativeSeparators(path) });
#else
    QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(path).absolutePath()));
#endif
}

QString folderText(const QFileInfo &info)
{
    const QString dir = info.absolutePath();
    const QString inHome = QDir::home().relativeFilePath(dir);
    if (inHome.isEmpty() || inHome == QLatin1String("."))
        return QDir::home().dirName();
    if (inHome.startsWith(QLatin1String("..")) || QDir::isAbsolutePath(inHome))
        return QDir::toNativeSeparators(dir);
    return inHome.split(u'/').join(QStringLiteral(" / "));
}

}

StartPage::StartPage(AppSettings *settings, QWidget *parent)
    : QWidget(parent)
    , m_settings(settings)
{
    setObjectName(QStringLiteral("StartPage"));
    setFixedWidth(kWidth);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    m_art = new QLabel(this);
    m_art->setAlignment(Qt::AlignCenter);
    m_title = new QLabel(this);
    m_title->setObjectName(QStringLiteral("StartTitle"));
    m_title->setAlignment(Qt::AlignCenter);
    m_hint = new QLabel(this);
    m_hint->setObjectName(QStringLiteral("StartHint"));
    m_hint->setAlignment(Qt::AlignCenter);
    m_emptyNote = new QLabel(this);
    m_emptyNote->setObjectName(QStringLiteral("StartEmptyNote"));
    m_emptyNote->setAlignment(Qt::AlignCenter);
    layout->addWidget(m_art);
    layout->addSpacing(18);
    layout->addWidget(m_title);
    layout->addSpacing(4);
    layout->addWidget(m_hint);
    layout->addSpacing(40);
    layout->addWidget(m_emptyNote);

    m_recent = new QWidget(this);
    auto *recent = new QVBoxLayout(m_recent);
    recent->setContentsMargins(0, 0, 0, 0);
    recent->setSpacing(0);

    auto *header = new QWidget(m_recent);
    header->setObjectName(QStringLiteral("RecentHeader"));
    header->setAttribute(Qt::WA_StyledBackground);
    m_recentHeading = new QLabel(header);
    m_recentHeading->setObjectName(QStringLiteral("RecentHeading"));
    m_clear = new QPushButton(header);
    m_clear->setObjectName(QStringLiteral("RecentClear"));
    m_clear->setCursor(Qt::PointingHandCursor);
    connect(m_clear, &QPushButton::clicked, settings, &AppSettings::clearRecentFiles);
    auto *headerLine = new QHBoxLayout(header);
    headerLine->setContentsMargins(0, 0, 8, 10);
    headerLine->addWidget(m_recentHeading);
    headerLine->addStretch(1);
    headerLine->addWidget(m_clear);
    recent->addWidget(header);

    m_rows = new QVBoxLayout();
    m_rows->setSpacing(0);
    recent->addLayout(m_rows);
    layout->addWidget(m_recent);

    connect(settings, &AppSettings::recentFilesChanged, this, &StartPage::rebuild);
    retranslateUi();
    refreshIcons();
}

void StartPage::showEvent(QShowEvent *e)
{
    rebuild();
    QWidget::showEvent(e);
}

void StartPage::changeEvent(QEvent *e)
{
    if (e->type() == QEvent::LanguageChange) {
        retranslateUi();
        if (isVisible()) rebuild();
    } else if (e->type() == QEvent::PaletteChange) {
        refreshIcons();
    }
    QWidget::changeEvent(e);
}

void StartPage::retranslateUi()
{
    m_title->setText(tr("Drop a document here"));
    m_hint->setText(tr("or click a tab above to open"));
    m_emptyNote->setText(tr("Recently opened files will appear here."));
    m_recentHeading->setText(tr("Recently Opened"));
    m_clear->setText(tr("Clear List"));
}

void StartPage::refreshIcons()
{
    m_art->setPixmap(Theme::renderSvg(QStringLiteral("files"), Theme::IconMuted,
                                      kArtSize, devicePixelRatioF()));
    const QPixmap file = Theme::renderSvg(QStringLiteral("file"), Theme::IconMuted,
                                          kIconSize, devicePixelRatioF());
    for (QLabel *icon : findChildren<QLabel *>(QStringLiteral("RecentIcon")))
        icon->setPixmap(file);
}

QString StartPage::openedText(const QDateTime &opened) const
{
    if (!opened.isValid()) return {};
    const QDateTime now = QDateTime::currentDateTime();
    const qint64 seconds = opened.secsTo(now);
    if (seconds < 60) return tr("Just now");
    if (seconds < 3600) return tr("%n min ago", nullptr, int(seconds / 60));

    const QLocale locale(m_settings->language());
    const qint64 days = opened.date().daysTo(now.date());
    if (days == 0)
        return tr("Today, %1").arg(locale.toString(opened.time(), QLocale::ShortFormat));
    if (days == 1) return tr("Yesterday");
    return locale.toString(opened.date(), tr("d MMM"));
}

void StartPage::rebuild()
{
    // deleteLater: a click on a row reaches this while that row is still emitting.
    while (QLayoutItem *item = m_rows->takeAt(0)) {
        item->widget()->hide();
        item->widget()->deleteLater();
        delete item;
    }

    int shown = 0;
    for (const AppSettings::RecentFile &recent : m_settings->recentFiles()) {
        const QFileInfo info(recent.path);
        if (!info.isFile()) continue;

        auto *row = new LayoutButton(m_recent);
        row->setObjectName(QStringLiteral("RecentItem"));
        row->setCursor(Qt::PointingHandCursor);
        row->setToolTip(QDir::toNativeSeparators(recent.path));

        auto *text = new QVBoxLayout();
        text->setSpacing(2);
        text->addWidget(new ElidedLabel(info.fileName(), "RecentName", row));
        text->addWidget(new ElidedLabel(folderText(info), "RecentFolder", row));

        QLabel *opened = passiveLabel("RecentTime", row);
        opened->setText(openedText(recent.opened));

        auto *line = new QHBoxLayout(row);
        line->setContentsMargins(8, 10, 8, 10);
        line->setSpacing(12);
        line->addWidget(passiveLabel("RecentIcon", row));
        line->addLayout(text, 1);
        line->addWidget(opened);

        connect(row, &QPushButton::clicked, this,
                [this, path = recent.path]() { Q_EMIT openRequested(path); });
        row->setContextMenuPolicy(Qt::CustomContextMenu);
        connect(row, &QWidget::customContextMenuRequested, this,
                [this, row, path = recent.path](const QPoint &at) {
            QMenu menu(this);
            QAction *location = menu.addAction(tr("Open File Location"));
            QAction *remove   = menu.addAction(tr("Remove from List"));
            QAction *chosen   = menu.exec(row->mapToGlobal(at));
            if (chosen == location) showInFolder(path);
            else if (chosen == remove) m_settings->removeRecentFile(path);
        });
        m_rows->addWidget(row);
        ++shown;
    }
    m_recent->setVisible(shown > 0);
    m_emptyNote->setVisible(shown == 0);
    refreshIcons();
}
