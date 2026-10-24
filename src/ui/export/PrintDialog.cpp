#include "ui/export/PrintDialog.hpp"

#if defined(HAVE_QT_PRINT) && defined(HAVE_PDF_RENDERING)

#include "ui/DocumentView.hpp"
#include "ui/export/PageRange.hpp"
#include "ui/export/PrintPreview.hpp"
#include "ui/theme/Theme.hpp"
#include "ui/widgets/CheckBoxStyle.hpp"

#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QFileInfo>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPageRanges>
#include <QPrintDialog>
#include <QPrinter>
#include <QPrinterInfo>
#include <QPushButton>
#include <QRadioButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStyle>
#include <QVBoxLayout>

namespace {

constexpr int kPosterRole = Qt::UserRole + 1;

QString pagesText(int n)
{
    return n == 1 ? PrintDialog::tr("1 page") : PrintDialog::tr("%n pages", nullptr, n);
}

QString sheetsText(int n)
{
    return n == 1 ? PrintDialog::tr("1 sheet") : PrintDialog::tr("%n sheets", nullptr, n);
}

QString copiesText(int n)
{
    return n == 1 ? PrintDialog::tr("1 copy") : PrintDialog::tr("%n copies", nullptr, n);
}

QLabel *label(const QString &text, const char *objectName)
{
    auto *l = new QLabel(text);
    l->setObjectName(QLatin1String(objectName));
    return l;
}

QComboBox *combo(qreal dpr)
{
    auto *box = new QComboBox;
    box->setObjectName(QStringLiteral("XInput"));
    box->setCursor(Qt::PointingHandCursor);
    auto *chevron = new QLabel;
    chevron->setPixmap(Theme::renderSvg(QStringLiteral("chevron-down"), Theme::IconNormal, 16, dpr));
    chevron->setAttribute(Qt::WA_TransparentForMouseEvents);
    auto *row = new QHBoxLayout(box);
    row->setContentsMargins(0, 0, 12, 0);
    row->addStretch(1);
    row->addWidget(chevron);
    return box;
}

QFrame *section(const QString &icon, const QString &title, QVBoxLayout **body)
{
    auto *part = new QFrame;
    auto *vl = new QVBoxLayout(part);
    vl->setContentsMargins(24, 14, 24, 14);
    vl->setSpacing(10);
    auto *head = new QHBoxLayout;
    head->setSpacing(14);
    auto *glyph = new QLabel;
    glyph->setPixmap(Theme::renderSvg(icon, Theme::IconNormal, 24, part->devicePixelRatioF()));
    head->addWidget(glyph);
    head->addWidget(label(title, "XSectionTitle"));
    head->addStretch(1);
    vl->addLayout(head);
    *body = vl;
    return part;
}

QWidget *divider()
{
    auto *line = new QFrame;
    line->setObjectName(QStringLiteral("XDivider"));
    line->setFixedHeight(1);
    auto *wrap = new QWidget;
    auto *hl = new QHBoxLayout(wrap);
    hl->setContentsMargins(20, 0, 20, 0);
    hl->addWidget(line);
    return wrap;
}

QVBoxLayout *field(const QString &caption, QWidget *input)
{
    auto *vl = new QVBoxLayout;
    vl->setSpacing(6);
    vl->addWidget(label(caption, "XCaption"));
    vl->addWidget(input);
    return vl;
}

}

PrintDialog::PrintDialog(QPrinter *printer, DocumentView *view,
                         const DocumentExporter::Sources &pages, QWidget *parent)
    : QDialog(parent)
    , m_printer(printer)
    , m_view(view)
    , m_pages(pages)
    , m_pageCount(qMax(1, view->pageCount()))
    , m_currentPage(qBound(0, view->currentPage(), m_pageCount - 1))
{
    setWindowTitle(tr("Print - OpenPDF Studio"));
    setModal(true);
    setMinimumSize(900, 640);
    resize(1080, 860);

    m_refreshTimer.setSingleShot(true);
    connect(&m_refreshTimer, &QTimer::timeout, this, &PrintDialog::refresh);

    applyDialogStyle();
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);
    root->addWidget(buildHeader());

    auto *body = new QHBoxLayout;
    body->setContentsMargins(20, 0, 20, 16);
    body->setSpacing(16);
    m_preview = new PrintPreview;
    body->addWidget(m_preview, 1);
    body->addWidget(buildSettings(), 1);
    root->addLayout(body, 1);
    root->addWidget(buildFooter());

    auto *checkStyle = new CheckBoxStyle;
    checkStyle->setParent(this);
    for (QAbstractButton *box : findChildren<QAbstractButton *>()) {
        if (!qobject_cast<QCheckBox *>(box) && !qobject_cast<QRadioButton *>(box)) continue;
        box->setStyle(checkStyle);
        box->setCursor(Qt::PointingHandCursor);
    }

    onPrinterChanged();
    refresh();
}

PrintJob PrintDialog::job() const
{
    if (m_systemJob) return *m_systemJob;
    PrintJob job;
    job.pages         = selectedPages();
    job.pagesPerSheet = m_perSheetCombo->currentData().toInt();
    job.posterSheets  = posterSheets();
    job.cutMarks      = job.posterSheets > 1 && m_cutMarksChk->isChecked();
    job.scaling       = PrintJob::Scaling(m_scalingCombo->currentData().toInt());
    job.grayscale     = !colorPrinter() || m_grayChk->isChecked();
    return job;
}

QList<int> PrintDialog::selectedPages(bool *ok) const
{
    if (ok) *ok = true;
    if (m_currentRadio->isChecked()) return { m_currentPage };
    if (m_rangeRadio->isChecked()) return PageRange::parse(m_rangeEdit->text(), m_pageCount, ok);
    QList<int> all;
    for (int p = 0; p < m_pageCount; ++p) all.append(p);
    return all;
}

int PrintDialog::posterSheets() const
{
    return qMax(1, m_perSheetCombo->currentData(kPosterRole).toInt());
}

bool PrintDialog::colorPrinter() const
{
    const QString name = m_printerCombo->currentData().toString();
    if (name.isEmpty()) return true;
    const QList<QPrinter::ColorMode> modes = QPrinterInfo::printerInfo(name).supportedColorModes();
    return modes.isEmpty() || modes.contains(QPrinter::Color);
}

void PrintDialog::applyDialogStyle()
{
    setStyleSheet(QStringLiteral(R"css(
        QDialog { background: palette(window); }
        QLabel#XTitle { font-size: 24px; font-weight: 600; }
        QLabel#XSubtitle { font-size: 14px; color: palette(placeholder-text); }
        QLabel#XMuted { color: palette(placeholder-text); }
        QLabel#XCardTitle { font-size: 15px; font-weight: 600; }
        QLabel#XSectionTitle { font-size: 16px; font-weight: 600; }
        QLabel#XCaption { color: palette(placeholder-text); }
        QLabel#XField { font-weight: 500; }
        QLabel#XSummary { font-size: 15px; font-weight: 600; }
        QFrame#XCard {
            border: 1px solid palette(mid);
            border-radius: 10px;
            background: palette(base);
        }
        PrintPreview#XCard, QWidget#XSheetCanvas { background: palette(window); }
        QWidget#XPage, QScrollArea#XScroll { background: palette(base); border: none; }
        QFrame#XDivider { background: palette(mid); border: none; }
        QScrollArea#XScroll QScrollBar:vertical { background: transparent; width: 6px; margin: 2px; }
        QScrollArea#XScroll QScrollBar::handle:vertical {
            background: palette(mid);
            border-radius: 3px;
            min-height: 32px;
        }
        QScrollArea#XScroll QScrollBar::add-line:vertical,
        QScrollArea#XScroll QScrollBar::sub-line:vertical { height: 0px; }
        QLineEdit#XInput, QComboBox#XInput, QSpinBox#XInput {
            border: 1px solid palette(mid);
            border-radius: 6px;
            padding: 0 10px;
            min-height: 34px;
            background: palette(base);
        }
        QLineEdit#XInput:focus, QComboBox#XInput:focus, QSpinBox#XInput:focus {
            border-color: #3B82F6;
        }
        QLineEdit#XInput[invalid="true"] { border-color: #EF4444; }
        QLineEdit#XInput:disabled, QComboBox#XInput:disabled {
            color: palette(placeholder-text);
        }
        QComboBox#XInput::drop-down { border: none; width: 30px; }
        QSpinBox#XInput::up-button, QSpinBox#XInput::down-button { width: 22px; border: none; }
        QPushButton#XCancel {
            border: 1px solid palette(mid);
            border-radius: 6px;
            background: palette(base);
            color: palette(text);
            padding: 0 14px;
            font-weight: 600;
        }
        QPushButton#XCancel:hover { background: palette(alternate-base); }
        QPushButton#XMore, QPushButton#XMore:checked {
            border: none;
            background: transparent;
            color: palette(text);
            text-align: left;
            font-size: 15px;
            font-weight: 500;
            padding: 4px 0;
        }
        QWidget#XFooter { border-top: 1px solid palette(mid); }
        QPushButton#XNav, QPushButton#XPrint {
            border: none;
            border-radius: 6px;
            background: #2563EB;
            color: white;
            font-weight: 700;
            font-size: 15px;
        }
        QPushButton#XNav:hover, QPushButton#XPrint:hover { background: #1D4ED8; }
        QPushButton#XNav:pressed, QPushButton#XPrint:pressed { background: #1E40AF; }
        QPushButton#XNav:disabled, QPushButton#XPrint:disabled {
            background: rgba(37, 99, 235, 0.35);
            color: rgba(255, 255, 255, 0.75);
        }
    )css"));
}

QWidget *PrintDialog::buildHeader()
{
    auto *head = new QWidget;
    auto *hl = new QHBoxLayout(head);
    hl->setContentsMargins(28, 14, 28, 12);
    hl->setSpacing(18);
    auto *glyph = new QLabel;
    glyph->setPixmap(Theme::renderSvg(QStringLiteral("printer"), Theme::IconNormal, 44,
                                      devicePixelRatioF()));
    hl->addWidget(glyph);
    auto *titles = new QVBoxLayout;
    titles->setSpacing(2);
    titles->addWidget(label(tr("Print"), "XTitle"));
    const QString name = QFileInfo(m_view->currentFile()).fileName();
    titles->addWidget(label(name.isEmpty() ? pagesText(m_pageCount)
                                           : QStringLiteral("%1  ·  %2").arg(name, pagesText(m_pageCount)),
                            "XSubtitle"));
    hl->addLayout(titles, 1);
    return head;
}

QWidget *PrintDialog::buildSettings()
{
    auto *page = new QWidget;
    page->setObjectName(QStringLiteral("XPage"));
    auto *vl = new QVBoxLayout(page);
    vl->setContentsMargins(0, 0, 0, 0);
    vl->setSpacing(0);
    vl->addWidget(buildPrinterSection());
    vl->addWidget(divider());
    vl->addWidget(buildPagesSection());
    vl->addWidget(divider());
    vl->addWidget(buildPaperSection());
    vl->addWidget(divider());
    vl->addWidget(buildOutputSection());
    vl->addWidget(divider());
    vl->addWidget(buildMoreSection());
    vl->addStretch(1);

    auto *scroll = new QScrollArea;
    scroll->setObjectName(QStringLiteral("XScroll"));
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setWidget(page);

    auto *card = new QFrame;
    card->setObjectName(QStringLiteral("XCard"));
    card->setMinimumWidth(500);
    auto *cl = new QVBoxLayout(card);
    cl->setContentsMargins(1, 2, 1, 2);
    cl->addWidget(scroll);
    return card;
}

QFrame *PrintDialog::buildPrinterSection()
{
    QVBoxLayout *body = nullptr;
    QFrame *part = section(QStringLiteral("printer"), tr("Printer"), &body);
    m_printerCombo = combo(devicePixelRatioF());
    const QString preferred = m_printer->printerName().isEmpty()
        ? QPrinterInfo::defaultPrinterName() : m_printer->printerName();
    for (const QPrinterInfo &info : QPrinterInfo::availablePrinters()) {
        const QString shown = info.description().isEmpty()
            ? QString(info.printerName()).replace(QLatin1Char('_'), QLatin1Char(' '))
            : info.description();
        m_printerCombo->addItem(shown, info.printerName());
        if (info.printerName() == preferred)
            m_printerCombo->setCurrentIndex(m_printerCombo->count() - 1);
    }
    if (m_printerCombo->count() == 0) {
        m_printerCombo->addItem(tr("No printer found"));
        m_printerCombo->setEnabled(false);
    }
    body->addWidget(m_printerCombo);

    auto *status = new QHBoxLayout;
    status->setSpacing(8);
    m_statusIcon = new QLabel;
    m_statusLabel = new QLabel;
    m_kindLabel = label(QString{}, "XMuted");
    status->addWidget(m_statusIcon);
    status->addWidget(m_statusLabel);
    status->addStretch(1);
    status->addWidget(m_kindLabel);
    body->addLayout(status);

    connect(m_printerCombo, &QComboBox::currentIndexChanged, this, &PrintDialog::onPrinterChanged);
    return part;
}

QFrame *PrintDialog::buildPagesSection()
{
    QVBoxLayout *body = nullptr;
    QFrame *part = section(QStringLiteral("layers"), tr("Pages & copies"), &body);
    auto *group = new QButtonGroup(this);
    m_allRadio     = new QRadioButton(tr("All %1").arg(m_pageCount));
    m_currentRadio = new QRadioButton(tr("Current page"));
    m_rangeRadio   = new QRadioButton(tr("Range"));
    m_allRadio->setChecked(true);
    m_rangeEdit = new QLineEdit(QStringLiteral("1-%1").arg(m_pageCount));
    m_rangeEdit->setObjectName(QStringLiteral("XInput"));
    m_rangeEdit->setToolTip(tr("For example: 1-3, 5, 8-10"));
    m_rangeEdit->setFixedWidth(110);
    m_rangeEdit->setEnabled(false);
    auto *radios = new QHBoxLayout;
    radios->setSpacing(28);
    for (QRadioButton *radio : { m_allRadio, m_currentRadio, m_rangeRadio }) {
        group->addButton(radio);
        connect(radio, &QRadioButton::toggled, this, [this] { m_refreshTimer.start(); });
    }
    radios->addWidget(m_allRadio);
    radios->addWidget(m_currentRadio);
    auto *range = new QHBoxLayout;
    range->setSpacing(10);
    range->addWidget(m_rangeRadio);
    range->addWidget(m_rangeEdit);
    radios->addLayout(range);
    radios->addStretch(1);
    body->addLayout(radios);
    connect(m_rangeRadio, &QRadioButton::toggled, m_rangeEdit, &QLineEdit::setEnabled);
    connect(m_rangeEdit, &QLineEdit::textChanged, this, [this] { m_refreshTimer.start(); });

    auto *copies = new QHBoxLayout;
    copies->setSpacing(14);
    copies->addWidget(label(tr("Copies"), "XField"));
    m_copiesSpin = new QSpinBox;
    m_copiesSpin->setObjectName(QStringLiteral("XInput"));
    m_copiesSpin->setRange(1, 999);
    m_copiesSpin->setFixedWidth(110);
    copies->addWidget(m_copiesSpin);
    copies->addSpacing(20);
    m_collateChk = new QCheckBox(tr("Collate"));
    m_collateChk->setChecked(true);
    m_collateChk->setEnabled(false);
    copies->addWidget(m_collateChk);
    copies->addStretch(1);
    body->addLayout(copies);
    connect(m_copiesSpin, &QSpinBox::valueChanged, this, [this](int n) {
        m_collateChk->setEnabled(n > 1);
        m_refreshTimer.start();
    });
    connect(m_collateChk, &QCheckBox::toggled, this, [this] { m_refreshTimer.start(); });
    return part;
}

QFrame *PrintDialog::buildPaperSection()
{
    QVBoxLayout *body = nullptr;
    QFrame *part = section(QStringLiteral("file"), tr("Paper & layout"), &body);
    const qreal dpr = devicePixelRatioF();
    m_paperCombo = combo(dpr);
    m_orientationCombo = combo(dpr);
    m_orientationCombo->addItem(tr("Portrait"));
    m_orientationCombo->addItem(tr("Landscape"));
    m_scalingCombo = combo(dpr);
    m_scalingCombo->addItem(tr("Fit to printable area"), int(PrintJob::Scaling::Fit));
    m_scalingCombo->addItem(tr("Shrink oversized pages"), int(PrintJob::Scaling::ShrinkOversize));
    m_scalingCombo->addItem(tr("Actual size"), int(PrintJob::Scaling::ActualSize));

    auto *grid = new QGridLayout;
    grid->setHorizontalSpacing(24);
    grid->setVerticalSpacing(14);
    grid->addLayout(field(tr("Paper size"), m_paperCombo), 0, 0);
    grid->addLayout(field(tr("Orientation"), m_orientationCombo), 0, 1);
    grid->addLayout(field(tr("Scaling"), m_scalingCombo), 1, 0, 1, 2);
    grid->setColumnStretch(0, 1);
    grid->setColumnStretch(1, 1);
    body->addLayout(grid);

    connect(m_paperCombo, &QComboBox::currentIndexChanged, this, [this] { m_refreshTimer.start(); });
    connect(m_orientationCombo, &QComboBox::currentIndexChanged, this, [this] {
        m_orientationChosen = true;
        m_refreshTimer.start();
    });
    connect(m_scalingCombo, &QComboBox::currentIndexChanged, this, [this] { m_refreshTimer.start(); });
    return part;
}

QFrame *PrintDialog::buildOutputSection()
{
    QVBoxLayout *body = nullptr;
    QFrame *part = section(QStringLiteral("copy"), tr("Output"), &body);
    const qreal dpr = devicePixelRatioF();
    m_perSheetCombo = combo(dpr);
    for (int n : { 1, 2, 4, 6, 9, 16 })
        m_perSheetCombo->addItem(pagesText(n), n);
    m_perSheetCombo->insertSeparator(m_perSheetCombo->count());
    for (int n : { 2, 4, 9 }) {
        m_perSheetCombo->addItem(tr("1 page on %1 sheets").arg(n), 1);
        m_perSheetCombo->setItemData(m_perSheetCombo->count() - 1, n, kPosterRole);
    }
    m_duplexCombo = combo(dpr);

    auto *grid = new QGridLayout;
    grid->setHorizontalSpacing(24);
    grid->addLayout(field(tr("Pages per sheet"), m_perSheetCombo), 0, 0);
    grid->addLayout(field(tr("Two-sided"), m_duplexCombo), 0, 1);
    grid->setColumnStretch(0, 1);
    grid->setColumnStretch(1, 1);
    body->addLayout(grid);

    m_grayChk = new QCheckBox(tr("Print in black and white"));
    body->addWidget(m_grayChk);
    m_grayNote = new QWidget;
    auto *note = new QHBoxLayout(m_grayNote);
    note->setContentsMargins(0, 0, 0, 0);
    note->setSpacing(10);
    auto *info = new QLabel;
    info->setPixmap(Theme::renderSvg(QStringLiteral("info"), Theme::IconNormal, 18, dpr));
    note->addWidget(info);
    note->addWidget(label(tr("Output in black and white"), "XMuted"));
    note->addStretch(1);
    body->addWidget(m_grayNote);

    connect(m_perSheetCombo, &QComboBox::currentIndexChanged, this, [this] {
        m_orientationChosen = false;
        m_refreshTimer.start();
    });
    connect(m_duplexCombo, &QComboBox::currentIndexChanged, this, [this] { m_refreshTimer.start(); });
    connect(m_grayChk, &QCheckBox::toggled, this, [this] { m_refreshTimer.start(); });
    return part;
}

QFrame *PrintDialog::buildMoreSection()
{
    auto *part = new QFrame;
    auto *vl = new QVBoxLayout(part);
    vl->setContentsMargins(24, 14, 24, 14);
    vl->setSpacing(10);
    const qreal dpr = devicePixelRatioF();
    m_moreToggle = new QPushButton(tr("More settings"));
    m_moreToggle->setObjectName(QStringLiteral("XMore"));
    m_moreToggle->setCheckable(true);
    m_moreToggle->setCursor(Qt::PointingHandCursor);
    m_moreToggle->setIcon(Theme::renderSvg(QStringLiteral("chevron-right"), Theme::IconNormal, 18, dpr));
    m_moreToggle->setIconSize(QSize(18, 18));
    vl->addWidget(m_moreToggle, 0, Qt::AlignLeft);

    m_moreBody = new QWidget;
    auto *body = new QVBoxLayout(m_moreBody);
    body->setContentsMargins(0, 0, 0, 0);
    body->setSpacing(6);
    m_cutMarksChk = new QCheckBox(tr("Cut marks on posters"));
    body->addWidget(m_cutMarksChk);
    body->addSpacing(6);
    auto *system = new QPushButton(tr("Print with the system dialog…"));
    system->setObjectName(QStringLiteral("XCancel"));
    system->setFixedHeight(36);
    system->setCursor(Qt::PointingHandCursor);
    body->addWidget(system, 0, Qt::AlignLeft);
    QLabel *hint = label(tr("For paper tray, print quality and other settings of the printer driver."),
                         "XMuted");
    hint->setWordWrap(true);
    body->addWidget(hint);
    m_moreBody->setVisible(false);
    vl->addWidget(m_moreBody);

    connect(m_moreToggle, &QPushButton::toggled, this, [this, dpr](bool open) {
        m_moreBody->setVisible(open);
        m_moreToggle->setIcon(Theme::renderSvg(open ? QStringLiteral("chevron-down")
                                                    : QStringLiteral("chevron-right"),
                                               Theme::IconNormal, 18, dpr));
    });
    connect(m_cutMarksChk, &QCheckBox::toggled, this, [this] { m_refreshTimer.start(); });
    connect(system, &QPushButton::clicked, this, &PrintDialog::printWithSystemDialog);
    return part;
}

QWidget *PrintDialog::buildFooter()
{
    auto *footer = new QWidget;
    footer->setObjectName(QStringLiteral("XFooter"));
    footer->setAttribute(Qt::WA_StyledBackground);
    footer->setFixedHeight(76);
    auto *hl = new QHBoxLayout(footer);
    hl->setContentsMargins(24, 0, 24, 0);
    hl->setSpacing(12);
    auto *summary = new QVBoxLayout;
    summary->setSpacing(2);
    m_summaryMain = label(QString{}, "XSummary");
    m_summarySub  = label(QString{}, "XMuted");
    summary->addStretch(1);
    summary->addWidget(m_summaryMain);
    summary->addWidget(m_summarySub);
    summary->addStretch(1);
    hl->addLayout(summary, 1);

    auto *cancel = new QPushButton(tr("Cancel"));
    cancel->setObjectName(QStringLiteral("XCancel"));
    cancel->setFixedSize(130, 44);
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    hl->addWidget(cancel);
    m_printButton = new QPushButton(tr("Print"));
    m_printButton->setObjectName(QStringLiteral("XPrint"));
    m_printButton->setIcon(Theme::renderSvg(QStringLiteral("printer"), Qt::white, 20,
                                            devicePixelRatioF()));
    m_printButton->setIconSize(QSize(20, 20));
    m_printButton->setFixedSize(150, 44);
    m_printButton->setDefault(true);
    connect(m_printButton, &QPushButton::clicked, this, &QDialog::accept);
    hl->addWidget(m_printButton);
    return footer;
}

void PrintDialog::onPrinterChanged()
{
    const QString name = m_printerCombo->currentData().toString();
    const QPrinterInfo info = QPrinterInfo::printerInfo(name);
    const qreal dpr = devicePixelRatioF();

    if (name.isEmpty()) {
        m_statusIcon->setPixmap(Theme::renderSvg(QStringLiteral("info"), QColor(0xEF4444), 16, dpr));
        m_statusLabel->setText(tr("Set up a printer in the system settings."));
        m_statusLabel->setStyleSheet(QStringLiteral("color: #EF4444;"));
        m_kindLabel->clear();
    } else {
        if (m_printer->printerName() != name) m_printer->setPrinterName(name);
        QString state;
        QColor color = Theme::Success;
        QString icon = QStringLiteral("check");
        switch (info.state()) {
        case QPrinter::Idle:   state = tr("Ready"); break;
        case QPrinter::Active: state = tr("Printing"); break;
        case QPrinter::Aborted:
            state = tr("Paused");
            color = QColor(0xF59E0B);
            icon = QStringLiteral("info");
            break;
        case QPrinter::Error:
            state = tr("Not ready");
            color = QColor(0xEF4444);
            icon = QStringLiteral("info");
            break;
        }
        m_statusIcon->setPixmap(Theme::renderSvg(icon, color, 16, dpr));
        m_statusLabel->setText(state);
        m_statusLabel->setStyleSheet(QStringLiteral("color: %1;").arg(color.name()));
        m_kindLabel->setText(colorPrinter() ? tr("Color printer") : tr("Black-and-white printer"));
    }

    const int previous = m_paperCombo->currentIndex();
    const QPageSize keep = previous >= 0 && previous < m_pageSizes.size()
        ? m_pageSizes.at(previous) : info.defaultPageSize();
    m_pageSizes = info.supportedPageSizes();
    if (m_pageSizes.isEmpty())
        m_pageSizes = { QPageSize(QPageSize::A4), QPageSize(QPageSize::Letter),
                        QPageSize(QPageSize::Legal), QPageSize(QPageSize::A3) };
    {
        const QSignalBlocker block(m_paperCombo);
        m_paperCombo->clear();
        int chosen = 0;
        for (int i = 0; i < m_pageSizes.size(); ++i) {
            m_paperCombo->addItem(m_pageSizes.at(i).name());
            if (keep.isValid() && m_pageSizes.at(i).isEquivalentTo(keep)) chosen = i;
        }
        m_paperCombo->setCurrentIndex(chosen);
    }

    const QPrinter::DuplexMode previousDuplex = m_duplexCombo->count() > 0
        ? QPrinter::DuplexMode(m_duplexCombo->currentData().toInt())
        : info.defaultDuplexMode();
    const QList<QPrinter::DuplexMode> duplex = info.supportedDuplexModes();
    {
        const QSignalBlocker block(m_duplexCombo);
        m_duplexCombo->clear();
        m_duplexCombo->addItem(tr("Off"), int(QPrinter::DuplexNone));
        if (duplex.contains(QPrinter::DuplexLongSide))
            m_duplexCombo->addItem(tr("Long edge"), int(QPrinter::DuplexLongSide));
        if (duplex.contains(QPrinter::DuplexShortSide))
            m_duplexCombo->addItem(tr("Short edge"), int(QPrinter::DuplexShortSide));
        const QPrinter::DuplexMode want = previousDuplex == QPrinter::DuplexAuto
            ? QPrinter::DuplexLongSide : previousDuplex;
        m_duplexCombo->setCurrentIndex(qMax(0, m_duplexCombo->findData(int(want))));
        m_duplexCombo->setEnabled(m_duplexCombo->count() > 1);
    }

    const bool color = colorPrinter();
    m_grayChk->setVisible(color);
    m_grayNote->setVisible(!color);
    m_refreshTimer.start();
}

void PrintDialog::applyToPrinter()
{
    const int paper = m_paperCombo->currentIndex();
    if (paper >= 0 && paper < m_pageSizes.size())
        m_printer->setPageSize(m_pageSizes.at(paper));
    m_printer->setPageOrientation(m_orientationCombo->currentIndex() == 1
                                      ? QPageLayout::Landscape : QPageLayout::Portrait);
    m_printer->setDuplex(QPrinter::DuplexMode(m_duplexCombo->currentData().toInt()));
    m_printer->setColorMode(job().grayscale ? QPrinter::GrayScale : QPrinter::Color);
    m_printer->setCopyCount(m_copiesSpin->value());
    m_printer->setCollateCopies(m_collateChk->isChecked());
}

void PrintDialog::chooseOrientation()
{
    if (m_orientationChosen) return;
    const int paper = m_paperCombo->currentIndex();
    const QList<int> pages = selectedPages();
    if (paper < 0 || paper >= m_pageSizes.size() || pages.isEmpty()) return;

    const QSizeF page = PrintSheets(m_pages, {}).pageSizePts(pages.first());
    const QSizeF sheet = m_pageSizes.at(paper).size(QPageSize::Point);
    if (page.isEmpty() || sheet.isEmpty()) return;
    const int perSheet = m_perSheetCombo->currentData().toInt();
    const int poster = posterSheets();
    const auto fit = [&](bool landscape) {
        QSizeF area = landscape ? sheet.transposed() : sheet;
        if (poster > 1) {
            const QSize tiles = PrintSheets::posterGrid(poster, landscape);
            area = QSizeF(area.width() * tiles.width(), area.height() * tiles.height());
        } else {
            const QSize cells = PrintSheets::grid(perSheet, landscape);
            area = QSizeF(area.width() / cells.width(), area.height() / cells.height());
        }
        return qMin(area.width() / page.width(), area.height() / page.height());
    };
    const QSignalBlocker block(m_orientationCombo);
    m_orientationCombo->setCurrentIndex(fit(true) > fit(false) * 1.05 ? 1 : 0);
}

void PrintDialog::refresh()
{
    const bool havePrinter = !m_printerCombo->currentData().toString().isEmpty();
    bool ok = true;
    selectedPages(&ok);
    m_rangeEdit->setProperty("invalid", !ok);
    m_rangeEdit->style()->unpolish(m_rangeEdit);
    m_rangeEdit->style()->polish(m_rangeEdit);
    m_printButton->setEnabled(ok && havePrinter);

    const bool poster = posterSheets() > 1;
    m_scalingCombo->setEnabled(!poster);
    m_cutMarksChk->setEnabled(poster);
    if (poster && m_duplexCombo->currentIndex() != 0) {
        const QSignalBlocker block(m_duplexCombo);
        m_duplexCombo->setCurrentIndex(0);
    }
    m_duplexCombo->setEnabled(!poster && m_duplexCombo->count() > 1);

    chooseOrientation();
    applyToPrinter();
    if (ok) {
        const PrintJob current = job();
        m_preview->setSheets(PrintSheets(m_pages, current), m_printer->pageLayout(),
                             current.pagesPerSheet > 1 || current.posterSheets > 1);
    }
    updateSummary();
}

void PrintDialog::updateSummary()
{
    bool ok = true;
    const int pages = int(selectedPages(&ok).size());
    if (!ok) {
        m_summaryMain->setText(tr("Check the page range"));
        m_summarySub->setText(tr("Use page numbers like 1-3, 5, 8-10."));
        return;
    }
    const int perSheet = m_perSheetCombo->currentData().toInt();
    const int poster = posterSheets();
    const int sides = poster > 1 ? pages * poster : (pages + perSheet - 1) / perSheet;
    const bool duplex = m_duplexCombo->currentData().toInt() != int(QPrinter::DuplexNone);
    const int copies = m_copiesSpin->value();
    const int sheets = (duplex ? (sides + 1) / 2 : sides) * copies;

    m_summaryMain->setText(QStringLiteral("%1  ·  %2").arg(pagesText(pages), sheetsText(sheets)));
    QStringList details { copiesText(copies), duplex ? tr("Two-sided") : tr("One-sided") };
    if (poster > 1) details.append(tr("Sheets overlap by 10 mm"));
    if (job().grayscale) details.append(tr("Black and white"));
    m_summarySub->setText(details.join(QStringLiteral("  ·  ")));
}

void PrintDialog::printWithSystemDialog()
{
    applyToPrinter();
    m_printer->setFromTo(1, m_pageCount);
    bool ok = true;
    const QList<int> pages = selectedPages(&ok);
    if (m_currentRadio->isChecked()) {
        m_printer->setPrintRange(QPrinter::CurrentPage);
    } else if (m_rangeRadio->isChecked() && ok) {
        QPageRanges ranges;
        for (int p : pages) ranges.addPage(p + 1);
        m_printer->setPageRanges(ranges);
        m_printer->setPrintRange(QPrinter::PageRange);
    } else {
        m_printer->setPrintRange(QPrinter::AllPages);
    }

    QPrintDialog dlg(m_printer, this);
    dlg.setOption(QAbstractPrintDialog::PrintPageRange);
    dlg.setOption(QAbstractPrintDialog::PrintCurrentPage);
    const bool accepted = dlg.exec() == QDialog::Accepted;

    const int chosen = m_printerCombo->findData(m_printer->printerName());
    if (chosen >= 0 && chosen != m_printerCombo->currentIndex())
        m_printerCombo->setCurrentIndex(chosen);
    if (!accepted) return;

    PrintJob job = this->job();
    job.pages.clear();
    if (m_printer->printRange() == QPrinter::CurrentPage) {
        job.pages.append(m_currentPage);
    } else if (m_printer->printRange() == QPrinter::PageRange) {
        const QPageRanges ranges = m_printer->pageRanges();
        if (!ranges.isEmpty()) {
            for (int p = ranges.firstPage(); p <= ranges.lastPage(); ++p)
                if (ranges.contains(p)) job.pages.append(p - 1);
        } else {
            for (int p = m_printer->fromPage(); p <= m_printer->toPage(); ++p)
                job.pages.append(p - 1);
        }
    }
    job.grayscale = m_printer->colorMode() == QPrinter::GrayScale;
    m_systemJob = job;
    m_refreshTimer.stop();
    accept();
}

#endif
