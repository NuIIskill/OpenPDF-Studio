#include "ui/export/ExportDialog.hpp"
#include "ui/export/ExportFormatList.hpp"
#include "ui/theme/Theme.hpp"
#include "ui/widgets/CheckBoxStyle.hpp"
#include "engine/export/PdfExporter.hpp"

#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPainter>
#include <QPushButton>
#include <QRadioButton>
#include <QScrollArea>
#include <QVBoxLayout>

namespace {

QLabel *label(const QString &text, const char *objectName)
{
    auto *l = new QLabel(text);
    l->setObjectName(QLatin1String(objectName));
    return l;
}

QFrame *section(const QString &icon, const QString &title, const QString &subtitle,
                QVBoxLayout **body)
{
    auto *card = new QFrame;
    card->setObjectName(QStringLiteral("XSection"));
    auto *row = new QHBoxLayout(card);
    row->setContentsMargins(20, 12, 20, 12);
    row->setSpacing(16);
    auto *glyph = new QLabel;
    glyph->setPixmap(Theme::renderSvg(icon, Theme::IconNormal, 26, card->devicePixelRatioF()));
    row->addWidget(glyph, 0, Qt::AlignTop);

    auto *column = new QVBoxLayout;
    column->setSpacing(2);
    column->addWidget(label(title, "XSectionTitle"));
    QLabel *note = label(subtitle, "XMuted");
    note->setWordWrap(true);
    column->addWidget(note);
    column->addSpacing(8);
    *body = new QVBoxLayout;
    (*body)->setSpacing(10);
    column->addLayout(*body);
    row->addLayout(column, 1);
    return card;
}

QWidget *option(QCheckBox *box, const QString &description)
{
    auto *w = new QWidget;
    auto *vl = new QVBoxLayout(w);
    vl->setContentsMargins(0, 0, 0, 0);
    vl->setSpacing(1);
    vl->addWidget(box);
    auto *note = label(description, "XMuted");
    note->setWordWrap(true);
    note->setContentsMargins(26, 0, 0, 0);
    vl->addWidget(note);
    return w;
}

QPixmap wordIcon(qreal dpr)
{
    const int side = 32;
    QPixmap pm(QSize(side, side) * dpr);
    pm.setDevicePixelRatio(dpr);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0x2B, 0x57, 0x9A));
    p.drawRoundedRect(QRectF(2, 2, side - 4, side - 4), 5, 5);
    QFont f;
    f.setPixelSize(18);
    f.setBold(true);
    p.setFont(f);
    p.setPen(Qt::white);
    p.drawText(QRect(0, 0, side, side), Qt::AlignCenter, QStringLiteral("W"));
    return pm;
}

QWidget *scrolling(QWidget *content)
{
    content->setObjectName(QStringLiteral("XPage"));
    auto *scroll = new QScrollArea;
    scroll->setObjectName(QStringLiteral("XScroll"));
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setWidget(content);
    return scroll;
}

}

ExportDialog::ExportDialog(const QString &currentFile, int pageCount,
                           int currentPage, QWidget *parent)
    : QDialog(parent)
    , m_currentFile(currentFile)
    , m_pageCount(qMax(1, pageCount))
    , m_currentPage(qBound(0, currentPage, qMax(0, pageCount - 1)))
    , m_sourceBytes(currentFile.isEmpty() ? 0 : QFileInfo(currentFile).size())
{
    setWindowTitle(tr("Export - OpenPDF Studio"));
    setModal(true);
    setFixedSize(980, 760);
    buildUi();
    updateOptionAvailability();
    updateEstimate();
}

QList<int> ExportDialog::parseRange(bool *ok) const
{
    if (ok) *ok = false;
    QList<int> pages;
    const QString text = m_rangeEdit ? m_rangeEdit->text().trimmed() : QString{};
    if (text.isEmpty()) return pages;

    const QStringList parts = text.split(QLatin1Char(','), Qt::SkipEmptyParts);
    for (const QString &rawPart : parts) {
        const QString part = rawPart.trimmed();
        if (part.isEmpty()) continue;
        bool okFrom = false, okTo = false;
        int from = 0, to = 0;
        const int dash = part.indexOf(QLatin1Char('-'));
        if (dash < 0) {
            from = to = part.toInt(&okFrom);
            okTo = okFrom;
        } else {
            from = part.left(dash).trimmed().toInt(&okFrom);
            to   = part.mid(dash + 1).trimmed().toInt(&okTo);
        }
        if (!okFrom || !okTo) return {};
        if (from > to) std::swap(from, to);
        if (from < 1 || to > m_pageCount) return {};
        for (int p = from; p <= to; ++p)
            if (!pages.contains(p - 1)) pages.append(p - 1);
    }
    if (pages.isEmpty()) return pages;
    if (ok) *ok = true;
    return pages;
}

QList<int> ExportDialog::selectedPages() const
{
    if (m_currentRadio && m_currentRadio->isChecked())
        return { m_currentPage };
    if (m_rangeRadio && m_rangeRadio->isChecked()) {
        bool ok = false;
        const QList<int> pages = parseRange(&ok);
        if (ok) return pages;
    }
    QList<int> all;
    for (int i = 0; i < m_pageCount; ++i) all.append(i);
    return all;
}

ExportRequest ExportDialog::request() const
{
    ExportRequest r;
    r.path            = selectedPath();
    r.format          = m_selectedFormat;
    r.pages           = selectedPages();
    r.imageQuality    = selectedImageQuality();
    r.compressImages  = m_compressChk  && m_compressChk->isChecked();
    r.includeComments = m_commentsChk  && m_commentsChk->isChecked();
    r.keepForms       = m_formsChk     && m_formsChk->isChecked();
    r.embedFonts      = m_fontsChk     && m_fontsChk->isChecked();
    r.openAfterExport = m_openAfterChk && m_openAfterChk->isChecked();
    return r;
}

void ExportDialog::updateOptionAvailability()
{
    const bool pdf   = m_selectedFormat == QLatin1String("pdf");
    const bool image = m_selectedFormat == QLatin1String("image");

    m_contentColumn->setVisible(pdf);
    m_compressRow->setVisible(!image);
    m_qualityCombo->setEnabled(!pdf || m_compressChk->isChecked());

    const bool qpdf = pdfExportAvailable();
    const QString missing = qpdf ? QString{}
        : tr("Not available in this build: rewriting a PDF's annotations, "
             "forms, fonts or encryption needs qpdf.");
    for (QCheckBox *box : { m_commentsChk, m_formsChk, m_fontsChk }) {
        box->setEnabled(qpdf);
        box->setToolTip(missing);
    }
    for (QCheckBox *box : { m_compressChk, m_commentsChk, m_formsChk, m_fontsChk })
        box->setCursor(box->isEnabled() ? Qt::PointingHandCursor : Qt::ArrowCursor);
}

void ExportDialog::updateEstimate()
{
    if (!m_sizeLabel) return;
    if (m_currentFile.isEmpty() || m_sourceBytes <= 0) {
        m_sizeLabel->setText(tr("Estimated file size: unknown"));
        return;
    }

    bool rangeOk = true;
    if (m_rangeRadio && m_rangeRadio->isChecked()) parseRange(&rangeOk);
    if (!rangeOk) {
        m_sizeLabel->setText(tr("Estimated file size: unknown (check the page range)"));
        return;
    }

    const int pages = qMax(1, selectedPages().size());
    const int quality = selectedImageQuality();
    const double perPage = double(m_sourceBytes) / qMax(1, m_pageCount);

    const double imageShare = qBound(0.0, (perPage - 20000.0) / 200000.0, 0.9);
    double bytes = 0.0;

    if (m_selectedFormat == QLatin1String("image")) {
        const double scale = quality >= 95 ? 3.0 : quality >= 80 ? 2.0
                           : quality >= 55 ? 1.5 : 1.0;
        const double pixels = 595.0 * 842.0 * scale * scale;

        bytes = pixels * (0.10 + qMin(0.15, perPage / 4.0e6)) * pages;
    } else if (m_selectedFormat == QLatin1String("word")) {

        const double base = 20000.0 + perPage * 0.35;
        double factor = 1.0;
        if (m_compressChk && m_compressChk->isChecked())
            factor = quality >= 95 ? 3.7 : quality >= 80 ? 1.0
                   : quality >= 55 ? 0.47 : 0.28;
        else
            factor = 1.2 + 1.9 * imageShare;
        bytes = pages * base * factor;
    } else {

        double factor = 0.85;
        if (m_compressChk && m_compressChk->isChecked()) {

            const double jpeg = quality >= 100 ? 1.0
                              : 0.45 + 0.30 * (quality - 40.0) / 60.0;
            factor *= (1.0 - imageShare) + imageShare * jpeg;
        }
        if (m_fontsChk && !m_fontsChk->isChecked())
            factor *= 0.75 - 0.35 * (1.0 - imageShare);
        if (m_commentsChk && !m_commentsChk->isChecked()) factor *= 0.97;
        bytes = perPage * pages * factor;
    }

    const auto human = [](double v) {
        if (v >= 1024.0 * 1024.0)
            return QStringLiteral("%1 MB").arg(v / (1024.0 * 1024.0), 0, 'f', 1);
        if (v >= 1024.0)
            return QStringLiteral("%1 KB").arg(v / 1024.0, 0, 'f', 0);
        return QStringLiteral("%1 B").arg(qRound(v));
    };

    m_sizeLabel->setText(tr("Estimated file size: ~%1  (%2 of %3 pages)")
                             .arg(human(qMax(1024.0, bytes)))
                             .arg(pages).arg(m_pageCount));
}

void ExportDialog::selectFormatForTest(const QString &id)
{
    m_formats->select(id);
}

QString ExportDialog::selectedPath() const
{
    const QString dir  = m_locationEdit->text().trimmed();
    QString name = m_filenameEdit->text().trimmed();
    if (dir.isEmpty() || name.isEmpty()) return {};

    if (m_selectedFormat == QLatin1String("word") && !name.endsWith(QLatin1String(".docx"), Qt::CaseInsensitive))
        name += QStringLiteral(".docx");
    else if (m_selectedFormat == QLatin1String("image")
             && !name.endsWith(QLatin1String(".png"), Qt::CaseInsensitive))
        name += QStringLiteral(".png");
    return QDir(dir).filePath(name);
}

QString ExportDialog::selectedFormat() const { return m_selectedFormat; }
int ExportDialog::selectedImageQuality() const
{
    return m_qualityCombo ? m_qualityCombo->currentData().toInt() : 85;
}

void ExportDialog::onFormatSelected(const QString &id)
{
    m_selectedFormat = id;
    if (!m_filenameEdit) return;

    QString name = m_filenameEdit->text();

    for (const QString &ext : {".pdf", ".docx", ".png"}) {
        if (name.endsWith(ext, Qt::CaseInsensitive)) { name.chop(ext.length()); break; }
    }
    if (!name.isEmpty())
        name += id == QLatin1String("word")  ? QStringLiteral(".docx")
              : id == QLatin1String("image") ? QStringLiteral(".png")
                                             : QStringLiteral(".pdf");
    m_filenameEdit->setText(name);
    updateOptionAvailability();
    updateEstimate();
}

void ExportDialog::onBrowse()
{
    const QString dir = QFileDialog::getExistingDirectory(
        this, tr("Select Save Location"), m_locationEdit->text());
    if (!dir.isEmpty())
        m_locationEdit->setText(dir);
}

void ExportDialog::onExport()
{
    const QString name = m_filenameEdit->text().trimmed();
    const QString dir  = m_locationEdit->text().trimmed();

    if (name.isEmpty()) {
        QMessageBox::warning(this, tr("Missing filename"), tr("Please enter a filename."));
        return;
    }
    if (dir.isEmpty()) {
        QMessageBox::warning(this, tr("Missing location"), tr("Please select a save location."));
        return;
    }

    if (m_currentFile.isEmpty()) {
        QMessageBox::warning(this, tr("No document"), tr("Please open a PDF document first."));
        return;
    }

    if (m_rangeRadio && m_rangeRadio->isChecked()) {
        bool ok = false;
        parseRange(&ok);
        if (!ok) {
            QMessageBox::warning(this, tr("Invalid page range"),
                tr("\"%1\" is not a valid range for a document with %2 pages.\n"
                   "Use page numbers like 1-3, 5, 8-10.")
                    .arg(m_rangeEdit->text().trimmed()).arg(m_pageCount));
            return;
        }
    }

    const QString path = selectedPath();
    QStringList existing;
    const QList<int> pages = selectedPages();
    if (m_selectedFormat == QLatin1String("image") && pages.size() > 1) {
        const QFileInfo out(path);
        for (int page : pages) {
            const QString candidate = out.dir().filePath(
                out.completeBaseName() + QStringLiteral("_page_%1.png").arg(page + 1));
            if (QFileInfo::exists(candidate)) existing.append(candidate);
        }
    } else if (QFileInfo::exists(path)) {
        existing.append(path);
    }
    if (!existing.isEmpty()) {
        const QString shown = existing.size() == 1
            ? QFileInfo(existing.first()).fileName()
            : tr("%1 image files").arg(existing.size());
        if (QMessageBox::question(this, tr("Overwrite existing file?"),
                tr("%1 already exists. Do you want to overwrite it?").arg(shown),
                QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes)
            return;
    }

    accept();
}


void ExportDialog::applyDialogStyle()
{
    setStyleSheet(QStringLiteral(R"css(
        QDialog { background: palette(window); }
        QLabel#XSideTitle { font-size: 17px; font-weight: 600; }
        QPushButton#XFormat {
            border: 1px solid transparent;
            border-radius: 8px;
            background: transparent;
            text-align: left;
        }
        QPushButton#XFormat:hover:!checked:enabled { background: palette(alternate-base); }
        QLabel#XFormatTitle { font-size: 15px; font-weight: 600; }
        QLabel#XFormatNote, QLabel#XMuted { color: palette(placeholder-text); }
        QLabel#XFormatTitle:disabled, QLabel#XFormatNote:disabled,
        QLabel#XMuted:disabled { color: palette(mid); }
        QFrame#XCard {
            border: 1px solid palette(mid);
            border-radius: 10px;
            background: palette(base);
        }
        QWidget#XPage, QScrollArea#XScroll { background: palette(base); }
        QFrame#XSection {
            border: 1px solid palette(mid);
            border-radius: 8px;
            background: palette(base);
        }
        QLabel#XSectionTitle { font-size: 16px; font-weight: 600; }
        QLabel#XField { font-weight: 500; }
        QFrame#XDivider { background: palette(mid); border: none; }
        QLineEdit#XInput, QComboBox#XInput {
            border: 1px solid palette(mid);
            border-radius: 6px;
            padding: 0 10px;
            min-height: 34px;
            background: palette(base);
        }
        QLineEdit#XInput:focus, QComboBox#XInput:focus { border-color: #3B82F6; }
        QLineEdit#XInput:disabled, QComboBox#XInput:disabled { color: palette(placeholder-text); }
        QComboBox#XInput::drop-down { border: none; width: 30px; }
        QWidget#XFooter { border-top: 1px solid palette(mid); }
        QLabel#XSizeLabel { font-weight: 500; color: palette(window-text); }
        QPushButton#XCancel {
            border: 1px solid palette(mid);
            border-radius: 6px;
            padding: 0 14px;
            background: palette(base);
            color: palette(text);
            font-weight: 600;
        }
        QPushButton#XCancel:hover { background: palette(alternate-base); }
        QPushButton#XExport {
            border: none;
            border-radius: 6px;
            background: #2563EB;
            color: white;
            font-weight: 700;
        }
        QPushButton#XExport:hover   { background: #1D4ED8; }
        QPushButton#XExport:pressed { background: #1E40AF; }
        QPushButton#XExport:disabled {
            background: rgba(37, 99, 235, 0.35);
            color: rgba(255, 255, 255, 0.75);
        }
    )css") + (Theme::DarkMode ? QStringLiteral(R"css(
        QPushButton#XFormat:checked {
            background: rgba(59, 130, 246, 0.22);
            border-color: rgba(96, 165, 250, 0.60);
        }
        QLabel#XFormatTitle[current="true"] { color: #93C5FD; }
    )css") : QStringLiteral(R"css(
        QPushButton#XFormat:checked {
            background: rgba(37, 99, 235, 0.12);
            border-color: rgba(37, 99, 235, 0.50);
        }
        QLabel#XFormatTitle[current="true"] { color: #1D4ED8; }
    )css")));
}

QWidget *ExportDialog::buildFormatList()
{
    const qreal dpr = devicePixelRatioF();
    m_formats = new ExportFormatList;
    m_formats->addFormat(QStringLiteral("pdf"),
                         Theme::renderSvg(QStringLiteral("export-pdf"), Qt::transparent, 32, dpr),
                         QStringLiteral("PDF"), tr("Best for sharing and printing"), true);
    m_formats->addFormat(QStringLiteral("pdfa"),
                         Theme::renderSvg(QStringLiteral("export-pdfa"), Qt::transparent, 32, dpr),
                         QStringLiteral("PDF/A"), tr("Long-term archiving"), false);
    m_formats->addFormat(QStringLiteral("word"), wordIcon(dpr), QStringLiteral("Word"),
                         tr("Edit in Microsoft Word"), true);
    m_formats->addFormat(QStringLiteral("image"),
                         Theme::renderSvg(QStringLiteral("image"), Theme::Primary, 32, dpr),
                         tr("Image"), tr("PNG images"), true);
    connect(m_formats, &ExportFormatList::formatChanged, this, &ExportDialog::onFormatSelected);

    auto *side = new QWidget;
    side->setFixedWidth(260);
    auto *vl = new QVBoxLayout(side);
    vl->setContentsMargins(16, 22, 0, 12);
    vl->addWidget(m_formats);
    return side;
}

QWidget *ExportDialog::buildPage()
{
    auto *page = new QWidget;
    auto *vl = new QVBoxLayout(page);
    vl->setContentsMargins(16, 16, 16, 16);
    vl->setSpacing(12);
    vl->addWidget(buildFileSection());
    vl->addWidget(buildPagesSection());
    vl->addWidget(buildSettingsSection());
    vl->addStretch(1);
    return scrolling(page);
}

QFrame *ExportDialog::buildFileSection()
{
    QVBoxLayout *body = nullptr;
    QFrame *card = section(QStringLiteral("folder-open"), tr("File"),
                           tr("Choose where to save your file."), &body);
    auto *grid = new QGridLayout;
    grid->setHorizontalSpacing(12);
    grid->setVerticalSpacing(10);
    m_filenameEdit = new QLineEdit;
    m_filenameEdit->setObjectName(QStringLiteral("XInput"));
    if (!m_currentFile.isEmpty())
        m_filenameEdit->setText(QFileInfo(m_currentFile).completeBaseName()
                                + QStringLiteral("_export.pdf"));
    m_locationEdit = new QLineEdit;
    m_locationEdit->setObjectName(QStringLiteral("XInput"));
    if (!m_currentFile.isEmpty())
        m_locationEdit->setText(QFileInfo(m_currentFile).absolutePath());
    auto *browse = new QPushButton(tr("Browse…"));
    browse->setObjectName(QStringLiteral("XCancel"));
    browse->setFixedHeight(36);
    browse->setCursor(Qt::PointingHandCursor);
    connect(browse, &QPushButton::clicked, this, &ExportDialog::onBrowse);
    grid->addWidget(label(tr("Filename"), "XField"), 0, 0);
    grid->addWidget(m_filenameEdit, 0, 1, 1, 2);
    grid->addWidget(label(tr("Save location"), "XField"), 1, 0);
    grid->addWidget(m_locationEdit, 1, 1);
    grid->addWidget(browse, 1, 2);
    grid->setColumnStretch(1, 1);
    body->addLayout(grid);
    m_openAfterChk = new QCheckBox(tr("Open file after export"));
    body->addWidget(option(m_openAfterChk,
        tr("Automatically open the exported file when the process completes.")));
    return card;
}

QFrame *ExportDialog::buildPagesSection()
{
    QVBoxLayout *body = nullptr;
    QFrame *card = section(QStringLiteral("layers"), tr("Pages"),
                           tr("Select which pages to export."), &body);
    auto *pg = new QButtonGroup(this);
    const bool hasDocument = !m_currentFile.isEmpty();
    m_allRadio     = new QRadioButton(hasDocument ? tr("All pages (%1)").arg(m_pageCount)
                                                  : tr("All pages"));
    m_currentRadio = new QRadioButton(hasDocument ? tr("Current page (%1)").arg(m_currentPage + 1)
                                                  : tr("Current page"));
    m_rangeRadio   = new QRadioButton(tr("Range"));
    m_allRadio->setChecked(true);
    m_rangeEdit = new QLineEdit(hasDocument ? QStringLiteral("1-%1").arg(m_pageCount) : QString{});
    m_rangeEdit->setObjectName(QStringLiteral("XInput"));
    m_rangeEdit->setToolTip(tr("For example: 1-3, 5, 8-10"));
    m_rangeEdit->setFixedWidth(100);
    m_rangeEdit->setEnabled(false);
    auto *radios = new QHBoxLayout;
    radios->setSpacing(24);
    for (QRadioButton *radio : { m_allRadio, m_currentRadio, m_rangeRadio }) {
        pg->addButton(radio);
        connect(radio, &QRadioButton::toggled, this, &ExportDialog::updateEstimate);
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
    connect(m_rangeEdit, &QLineEdit::textChanged, this, &ExportDialog::updateEstimate);
    return card;
}

QFrame *ExportDialog::buildSettingsSection()
{
    QVBoxLayout *body = nullptr;
    QFrame *card = section(QStringLiteral("sliders"), tr("Settings"),
                           tr("Adjust quality and include additional content."), &body);
    auto *columns = new QHBoxLayout;
    columns->setSpacing(20);
    auto *left = new QVBoxLayout;
    left->setSpacing(12);
    m_compressChk = new QCheckBox(tr("Compress images"));
    m_compressChk->setChecked(true);
    auto *qRow = new QHBoxLayout;
    qRow->setSpacing(12);
    qRow->addWidget(label(tr("Quality"), "XField"));
    m_qualityCombo = new QComboBox;
    m_qualityCombo->setObjectName(QStringLiteral("XInput"));
    auto *chevron = new QLabel;
    chevron->setPixmap(Theme::renderSvg(QStringLiteral("chevron-down"), Theme::IconNormal, 16,
                                        devicePixelRatioF()));
    chevron->setAttribute(Qt::WA_TransparentForMouseEvents);
    auto *arrowRow = new QHBoxLayout(m_qualityCombo);
    arrowRow->setContentsMargins(0, 0, 10, 0);
    arrowRow->addStretch(1);
    arrowRow->addWidget(chevron);
    m_qualityCombo->addItem(tr("Maximum"), 100);
    m_qualityCombo->addItem(tr("High"),     85);
    m_qualityCombo->addItem(tr("Medium"),   60);
    m_qualityCombo->addItem(tr("Low"),      40);
    m_qualityCombo->setCurrentIndex(1);
    m_qualityCombo->setFixedWidth(200);
    qRow->addWidget(m_qualityCombo);
    qRow->addStretch(1);
    left->addLayout(qRow);
    m_compressRow = option(m_compressChk, tr("Reduces file size without noticeable quality loss."));
    left->addWidget(m_compressRow);
    left->addStretch(1);
    columns->addLayout(left, 4);

    m_contentColumn = new QWidget;
    auto *right = new QHBoxLayout(m_contentColumn);
    right->setContentsMargins(0, 0, 0, 0);
    right->setSpacing(20);
    auto *divider = new QFrame;
    divider->setObjectName(QStringLiteral("XDivider"));
    divider->setFixedWidth(1);
    right->addWidget(divider);
    auto *boxes = new QVBoxLayout;
    boxes->setSpacing(12);
    m_formsChk    = new QCheckBox(tr("Keep forms"));
    m_commentsChk = new QCheckBox(tr("Include comments"));
    m_formsChk->setChecked(true);
    m_commentsChk->setChecked(true);
    boxes->addWidget(option(m_formsChk, tr("Preserve form fields and data.")));
    boxes->addWidget(option(m_commentsChk, tr("Export comments and annotations.")));
    m_fontsChk = new QCheckBox(tr("Embed fonts"));
    m_fontsChk->setChecked(true);
    boxes->addWidget(option(m_fontsChk, tr("Include all used fonts in the PDF file.")));
    boxes->addStretch(1);
    right->addLayout(boxes, 1);
    columns->addWidget(m_contentColumn, 5);
    body->addLayout(columns);

    connect(m_qualityCombo, &QComboBox::currentIndexChanged, this, &ExportDialog::updateEstimate);
    connect(m_compressChk, &QCheckBox::toggled, this, &ExportDialog::updateOptionAvailability);
    connect(m_compressChk, &QCheckBox::toggled, this, &ExportDialog::updateEstimate);
    connect(m_commentsChk, &QCheckBox::toggled, this, &ExportDialog::updateEstimate);
    connect(m_fontsChk, &QCheckBox::toggled, this, &ExportDialog::updateEstimate);
    return card;
}

QWidget *ExportDialog::buildFooter()
{
    auto *footer = new QWidget;
    footer->setObjectName(QStringLiteral("XFooter"));
    footer->setFixedHeight(72);
    auto *hl = new QHBoxLayout(footer);
    hl->setContentsMargins(24, 0, 24, 0);
    hl->setSpacing(12);
    auto *info = new QLabel;
    info->setPixmap(Theme::renderSvg(QStringLiteral("info"), Theme::Primary, 22,
                                     devicePixelRatioF()));
    hl->addWidget(info);
    m_sizeLabel = label(tr("Estimated file size: unknown"), "XSizeLabel");
    hl->addWidget(m_sizeLabel, 1);
    auto *cancel = new QPushButton(tr("Cancel"));
    cancel->setObjectName(QStringLiteral("XCancel"));
    cancel->setFixedSize(130, 42);
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    hl->addWidget(cancel);
    auto *exportBtn = new QPushButton(tr("Export"));
    exportBtn->setObjectName(QStringLiteral("XExport"));
    exportBtn->setFixedSize(150, 42);
    exportBtn->setDefault(true);
    exportBtn->setEnabled(!m_currentFile.isEmpty());
    connect(exportBtn, &QPushButton::clicked, this, &ExportDialog::onExport);
    hl->addWidget(exportBtn);
    return footer;
}

void ExportDialog::buildUi()
{
    applyDialogStyle();

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    auto *main = new QHBoxLayout;
    main->setContentsMargins(0, 0, 20, 16);
    main->setSpacing(16);
    main->addWidget(buildFormatList());

    auto *card = new QFrame;
    card->setObjectName(QStringLiteral("XCard"));
    auto *cardLayout = new QVBoxLayout(card);
    cardLayout->setContentsMargins(1, 1, 1, 1);
    cardLayout->setSpacing(0);
    cardLayout->addWidget(buildPage(), 1);

    auto *cardColumn = new QVBoxLayout;
    cardColumn->setContentsMargins(0, 20, 0, 0);
    cardColumn->addWidget(card);
    main->addLayout(cardColumn, 1);
    root->addLayout(main, 1);
    root->addWidget(buildFooter());

    auto *checkStyle = new CheckBoxStyle;
    checkStyle->setParent(this);
    for (QAbstractButton *box : findChildren<QAbstractButton *>()) {
        if (!qobject_cast<QCheckBox *>(box) && !qobject_cast<QRadioButton *>(box)) continue;
        box->setStyle(checkStyle);
        box->setCursor(Qt::PointingHandCursor);
    }
    m_formats->select(QStringLiteral("pdf"));
}
