#include "ExportDialog.hpp"
#include "ui/theme/Theme.hpp"
#include "engine/edit/PdfExporter.hpp"

#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QRadioButton>
#include <QStandardPaths>
#include <QStyle>
#include <QVBoxLayout>

// ── ExportDialog ──────────────────────────────────────────────────────────────

ExportDialog::ExportDialog(const QString &currentFile, int pageCount,
                           int currentPage, QWidget *parent)
    : QDialog(parent)
    , m_currentFile(currentFile)
    , m_pageCount(qMax(1, pageCount))
    , m_currentPage(qBound(0, currentPage, qMax(0, pageCount - 1)))
    , m_sourceBytes(currentFile.isEmpty() ? 0 : QFileInfo(currentFile).size())
{
    setWindowTitle(tr("Export"));
    setModal(true);
    setFixedSize(720, 760);
    buildUi();
    updateOptionAvailability();
    updateEstimate();
}

// ── page selection ────────────────────────────────────────────────────────────

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
    if (m_passwordChk && m_passwordChk->isChecked())
        r.password = m_passEdit ? m_passEdit->text() : QString{};
    return r;
}

// ── availability and estimate ─────────────────────────────────────────────────

// Not every switch means something for every target. Greying the irrelevant
// ones out is honest; leaving them clickable but inert is what made the whole
// panel look broken.
void ExportDialog::updateOptionAvailability()
{
    // Rewriting a PDF's annotations, forms, fonts or encryption is qpdf's job.
    // Builds without it — the Windows package among them — cannot honour these
    // at all, so they are switched off there rather than accepted and ignored.
    const bool pdf   = m_selectedFormat == QLatin1String("pdf")
                       && pdfExportAvailable();
    const bool image = m_selectedFormat == QLatin1String("image");

    if (m_commentsChk) m_commentsChk->setEnabled(pdf);
    if (m_formsChk)    m_formsChk->setEnabled(pdf);
    if (m_fontsChk)    m_fontsChk->setEnabled(pdf);
    if (m_passwordChk) m_passwordChk->setEnabled(pdf);
    if (!pdf && m_passwordChk && m_passwordChk->isChecked())
        m_passwordChk->setChecked(false);
    updatePasswordFields();

    // Quality and compression apply to every target: the render resolution for
    // images, JPEG recompression inside a PDF, and the scale and encoding of
    // the pictures a DOCX embeds.
    Q_UNUSED(image)
    if (m_qualityCombo) m_qualityCombo->setEnabled(true);
    if (m_compressChk)  m_compressChk->setEnabled(true);

    // The options with no counterpart outside PDF are greyed out. The reason is
    // in the tooltip; the labels stay untouched.
    const QString pdfOnly = !pdfExportAvailable()
        ? tr("Not available in this build — rewriting a PDF's annotations, "
             "forms, fonts or encryption needs qpdf.")
        : tr("Only available when exporting as PDF — a %1 file has no "
             "equivalent.").arg(m_selectedFormat == QLatin1String("word")
                                    ? tr("Word") : tr("PNG"));
    for (QCheckBox *box : { m_commentsChk, m_formsChk, m_fontsChk, m_passwordChk })
        if (box) box->setToolTip(pdf ? QString{} : pdfOnly);
}

void ExportDialog::updateEstimate()
{
    if (!m_sizeLabel) return;
    if (m_currentFile.isEmpty() || m_sourceBytes <= 0) {
        m_sizeLabel->setText(tr("Estimated file size: —"));
        return;
    }

    bool rangeOk = true;
    if (m_rangeRadio && m_rangeRadio->isChecked()) parseRange(&rangeOk);
    if (!rangeOk) {
        m_sizeLabel->setText(tr("Estimated file size: — (check the page range)"));
        return;
    }

    const int pages = qMax(1, selectedPages().size());
    const int quality = selectedImageQuality();
    const double perPage = double(m_sourceBytes) / qMax(1, m_pageCount);
    // How much of a page is picture rather than text. A scan runs to hundreds
    // of kilobytes per page and is essentially all image; a generated report is
    // a few kilobytes and almost none. The factors below were fitted against
    // measured exports of both kinds rather than guessed.
    const double imageShare = qBound(0.0, (perPage - 20000.0) / 200000.0, 0.9);
    double bytes = 0.0;

    if (m_selectedFormat == QLatin1String("image")) {
        const double scale = quality >= 95 ? 3.0 : quality >= 80 ? 2.0
                           : quality >= 55 ? 1.5 : 1.0;
        const double pixels = 595.0 * 842.0 * scale * scale;
        // Measured: ~0.10 B/px for a vector page, ~0.19 for a scanned one.
        bytes = pixels * (0.10 + qMin(0.15, perPage / 4.0e6)) * pages;
    } else if (m_selectedFormat == QLatin1String("word")) {
        // Embedded pictures dominate a DOCX. Their weight at High quality was
        // measured at ~21 KB per structured page and ~157 KB per scanned one;
        // the factors below are that curve, again fitted rather than guessed.
        const double base = 20000.0 + perPage * 0.35;
        double factor = 1.0;
        if (m_compressChk && m_compressChk->isChecked())
            factor = quality >= 95 ? 3.7 : quality >= 80 ? 1.0
                   : quality >= 55 ? 0.47 : 0.28;
        else
            factor = 1.2 + 1.9 * imageShare;    // lossless PNG
        bytes = pages * base * factor;
    } else {
        // qpdf rewrites every stream, which alone takes off roughly 15 %.
        double factor = 0.85;
        if (m_compressChk && m_compressChk->isChecked()) {
            // Re-encoding at maximum quality is never smaller, so it is skipped.
            const double jpeg = quality >= 100 ? 1.0
                              : 0.45 + 0.30 * (quality - 40.0) / 60.0;
            factor *= (1.0 - imageShare) + imageShare * jpeg;
        }
        if (m_fontsChk && !m_fontsChk->isChecked())
            factor *= 0.75 - 0.35 * (1.0 - imageShare);   // text-heavy gains most
        if (m_commentsChk && !m_commentsChk->isChecked()) factor *= 0.97;
        if (m_passwordChk && m_passwordChk->isChecked())  factor *= 1.02;
        bytes = perPage * pages * factor;
    }

    const auto human = [](double v) {
        if (v >= 1024.0 * 1024.0)
            return QStringLiteral("%1 MB").arg(v / (1024.0 * 1024.0), 0, 'f', 1);
        if (v >= 1024.0)
            return QStringLiteral("%1 KB").arg(v / 1024.0, 0, 'f', 0);
        return QStringLiteral("%1 B").arg(qRound(v));
    };
    // Deliberately labelled as an approximation — the real size depends on the
    // document's own content, which is not known until it has been written.
    m_sizeLabel->setText(tr("Estimated file size: ~%1  (%2 of %3 pages)")
                             .arg(human(qMax(1024.0, bytes)))
                             .arg(pages).arg(m_pageCount));
}

void ExportDialog::selectFormatForTest(const QString &id)
{
    // Goes through the card itself rather than calling the handler directly, so
    // a captured screenshot shows the same state a real click produces.
    if (!m_formatGroup) return;
    for (QAbstractButton *btn : m_formatGroup->buttons())
        if (btn->property("formatId").toString() == id && btn->isEnabled()) {
            btn->setChecked(true);
            return;
        }
}

QString ExportDialog::selectedPath() const
{
    const QString dir  = m_locationEdit->text().trimmed();
    QString name = m_filenameEdit->text().trimmed();
    if (dir.isEmpty() || name.isEmpty()) return {};
    // ensure correct extension for selected format
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

// ── helpers ───────────────────────────────────────────────────────────────────

QWidget *ExportDialog::makeSectionHeader(const QString &num, const QString &title)
{
    auto *w  = new QWidget;
    auto *hl = new QHBoxLayout(w);
    hl->setContentsMargins(0, 0, 0, 0);
    hl->setSpacing(5);
    auto *nL = new QLabel(num);
    nL->setObjectName(QStringLiteral("XSecNum"));
    hl->addWidget(nL);
    auto *tL = new QLabel(title);
    tL->setObjectName(QStringLiteral("XSecTitle"));
    hl->addWidget(tL);
    hl->addStretch();
    return w;
}

QPushButton *ExportDialog::makeFormatCard(const QString &iconChar, const QString &label,
                                          const QString &id, bool available)
{
    auto *btn = new QPushButton;
    btn->setCheckable(true);
    btn->setEnabled(available);
    btn->setObjectName(QStringLiteral("XCard"));
    btn->setFixedSize(152, 88);
    btn->setCursor(available ? Qt::PointingHandCursor : Qt::ArrowCursor);

    // Build visual: icon on top, label below
    auto *inner = new QVBoxLayout(btn);
    inner->setContentsMargins(8, 10, 8, 8);
    inner->setSpacing(5);
    inner->setAlignment(Qt::AlignCenter);

    auto *iconLbl = new QLabel(iconChar);
    iconLbl->setObjectName(available ? QStringLiteral("XCardIcon")
                                     : QStringLiteral("XCardIconDim"));
    iconLbl->setAlignment(Qt::AlignCenter);
    iconLbl->setAttribute(Qt::WA_TransparentForMouseEvents);
    inner->addWidget(iconLbl, 0, Qt::AlignCenter);

    auto *textLbl = new QLabel(label);
    textLbl->setObjectName(available ? QStringLiteral("XCardText")
                                     : QStringLiteral("XCardTextDim"));
    textLbl->setAlignment(Qt::AlignCenter);
    textLbl->setAttribute(Qt::WA_TransparentForMouseEvents);
    inner->addWidget(textLbl, 0, Qt::AlignCenter);

    btn->setProperty("formatId", id);
    m_formatGroup->addButton(btn);
    m_formatGroup->setId(btn, m_formatGroup->buttons().size() - 1);

    const QString fmtId = id;
    connect(btn, &QPushButton::toggled, this, [this, fmtId](bool on) {
        if (on) onFormatSelected(fmtId);
    });
    return btn;
}

void ExportDialog::onFormatSelected(const QString &id)
{
    m_selectedFormat = id;
    if (!m_filenameEdit) return; // called during buildUi before filename field exists
    // Update filename extension
    QString name = m_filenameEdit->text();
    // strip old ext
    for (const QString &ext : {".pdf", ".docx", ".png"}) {
        if (name.endsWith(ext, Qt::CaseInsensitive)) { name.chop(ext.length()); break; }
    }
    if (id == QLatin1String("word"))
        name += QStringLiteral(".docx");
    else if (id == QLatin1String("image"))
        name += QStringLiteral(".png");
    else
        name += QStringLiteral(".pdf");
    m_filenameEdit->setText(name);
    updateOptionAvailability();
    updateEstimate();
}

void ExportDialog::updatePasswordFields()
{
    const bool on = m_passwordChk->isChecked();
    m_passEdit->setEnabled(on);
    m_passConfirm->setEnabled(on);
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

    if (m_passwordChk && m_passwordChk->isChecked()) {
        if (m_passEdit->text().isEmpty()) {
            QMessageBox::warning(this, tr("Missing password"),
                tr("Please enter a password, or switch password protection off."));
            return;
        }
        if (m_passEdit->text() != m_passConfirm->text()) {
            QMessageBox::warning(this, tr("Passwords do not match"),
                tr("The password and its confirmation are different."));
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

// ── buildUi ───────────────────────────────────────────────────────────────────

void ExportDialog::buildUi()
{
    m_formatGroup = new QButtonGroup(this);
    m_formatGroup->setExclusive(true);

    // ── stylesheet ────────────────────────────────────────────────────────
    setStyleSheet(QStringLiteral(R"css(
        QDialog {
            background: palette(window);
        }
        /* Section headers */
        QLabel#XSecNum {
            font-weight: 700;
            font-size: 13px;
        }
        QLabel#XSecTitle {
            font-weight: 700;
            font-size: 13px;
        }
        /* Format cards */
        QPushButton#XCard {
            border: 2px solid palette(mid);
            border-radius: 8px;
            background: palette(base);
            text-align: center;
        }
        QPushButton#XCard:checked {
            border: 2px solid #3B82F6;
            background: palette(base);
        }
        QPushButton#XCard:disabled {
            background: palette(alternateBase);
            border-color: palette(mid);
        }
        QPushButton#XCard:hover:!checked:enabled {
            border-color: #93C5FD;
        }
        QLabel#XCardIcon {
            font-size: 22px;
            background: transparent;
            color: #3B82F6;
        }
        QLabel#XCardIconDim {
            font-size: 22px;
            background: transparent;
            color: palette(mid);
        }
        QLabel#XCardText {
            font-size: 12px;
            font-weight: 600;
            background: transparent;
            color: palette(text);
        }
        QLabel#XCardTextDim {
            font-size: 12px;
            background: transparent;
            color: palette(mid);
        }
        /* Inputs */
        QLineEdit#XInput {
            border: 1px solid palette(mid);
            border-radius: 6px;
            padding: 6px 10px;
            background: palette(base);
        }
        QLineEdit#XInput:disabled {
            background: palette(alternateBase);
            color: palette(mid);
        }
        QLineEdit#XRangeInput {
            border: 1px solid palette(mid);
            border-radius: 6px;
            padding: 4px 8px;
            background: palette(base);
            min-width: 60px;
            max-width: 70px;
        }
        /* Separator */
        QFrame#XSep {
            color: palette(mid);
            max-height: 1px;
            background: palette(mid);
        }
        /* Footer */
        QWidget#XFooter {
            border-top: 1px solid palette(mid);
        }
        /* palette(mid) is a border shade, not a text shade — against the
           footer it came out barely legible. windowText carries the theme's
           actual contrast in both light and dark. */
        QLabel#XSizeLabel {
            font-size: 12px;
            font-weight: 500;
            color: palette(windowText);
        }
        /* Browse button */
        QPushButton#XBrowse {
            border: 1px solid palette(mid);
            border-radius: 6px;
            padding: 6px 14px;
            background: palette(button);
            color: palette(buttonText);
        }
        QPushButton#XBrowse:hover { background: palette(light); }
        /* Cancel */
        QPushButton#XCancel {
            border: 1px solid palette(mid);
            border-radius: 6px;
            padding: 0 16px;
            background: palette(base);
            color: palette(text);
        }
        QPushButton#XCancel:hover { background: palette(light); }
        /* Export (primary) */
        QPushButton#XExport {
            border: none;
            border-radius: 6px;
            padding: 0 20px;
            background: #3B82F6;
            color: white;
            font-weight: 700;
        }
        QPushButton#XExport:hover   { background: #2563EB; }
        QPushButton#XExport:pressed { background: #1D4ED8; }
    )css"));

    // ── root layout ───────────────────────────────────────────────────────
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    // ── body ──────────────────────────────────────────────────────────────
    auto *body = new QWidget;
    auto *bl   = new QVBoxLayout(body);
    bl->setContentsMargins(28, 22, 28, 16);
    bl->setSpacing(16);

    // ── 1. Format ─────────────────────────────────────────────────────────
    bl->addWidget(makeSectionHeader(tr("1."), tr("Format")));
    {
        auto *row = new QHBoxLayout;
        row->setSpacing(10);
        auto *pdfBtn  = makeFormatCard(QStringLiteral("📄"), QStringLiteral("PDF"),   QStringLiteral("pdf"),   true);
        auto *pdfaBtn = makeFormatCard(QStringLiteral("📋"), QStringLiteral("PDF/A"), QStringLiteral("pdfa"),  true);
        auto *wordBtn = makeFormatCard(QStringLiteral("W"),  QStringLiteral("Word"),  QStringLiteral("word"),  true);
        auto *imgBtn  = makeFormatCard(QStringLiteral("🖼"), QStringLiteral("Image"), QStringLiteral("image"), true);
        // Style the "W" as a blue badge
        wordBtn->findChild<QLabel *>(QStringLiteral("XCardIcon"))->setStyleSheet(
            QStringLiteral("font-size:16px;font-weight:bold;color:white;"
                           "background:#2B579A;border-radius:5px;"
                           "padding:3px 5px;"));
        row->addWidget(pdfBtn);
        row->addWidget(pdfaBtn);
        row->addWidget(wordBtn);
        row->addWidget(imgBtn);
        row->addStretch();
        bl->addLayout(row);
        pdfBtn->setChecked(true);
    }

    auto mkSep = [&]() {
        auto *f = new QFrame; f->setObjectName(QStringLiteral("XSep"));
        f->setFrameShape(QFrame::HLine); f->setFixedHeight(1);
        bl->addWidget(f);
    };
    mkSep();

    // ── 2. Filename  +  3. Location ──────────────────────────────────────
    {
        auto *row = new QHBoxLayout; row->setSpacing(16);

        auto *lv = new QVBoxLayout; lv->setSpacing(6);
        lv->addWidget(makeSectionHeader(tr("2."), tr("Filename")));
        m_filenameEdit = new QLineEdit;
        m_filenameEdit->setObjectName(QStringLiteral("XInput"));
        const QString base = m_currentFile.isEmpty()
            ? QStringLiteral("document")
            : QFileInfo(m_currentFile).completeBaseName();
        m_filenameEdit->setText(base + QStringLiteral("_export.pdf"));
        lv->addWidget(m_filenameEdit);
        row->addLayout(lv, 1);

        auto *rv = new QVBoxLayout; rv->setSpacing(6);
        rv->addWidget(makeSectionHeader(tr("3."), tr("Save Location")));
        auto *locRow = new QHBoxLayout; locRow->setSpacing(8);
        m_locationEdit = new QLineEdit;
        m_locationEdit->setObjectName(QStringLiteral("XInput"));
        m_locationEdit->setText(m_currentFile.isEmpty()
            ? QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)
            : QFileInfo(m_currentFile).absolutePath());
        locRow->addWidget(m_locationEdit, 1);
        auto *browse = new QPushButton(tr("Browse…"));
        browse->setObjectName(QStringLiteral("XBrowse"));
        browse->setFixedHeight(34);
        connect(browse, &QPushButton::clicked, this, &ExportDialog::onBrowse);
        locRow->addWidget(browse);
        rv->addLayout(locRow);
        row->addLayout(rv, 1);

        bl->addLayout(row);
    }
    mkSep();

    // ── 4. Page range  +  5. Quality ─────────────────────────────────────
    {
        auto *row = new QHBoxLayout; row->setSpacing(24);

        // Left: page range
        auto *lv = new QVBoxLayout; lv->setSpacing(6);
        lv->addWidget(makeSectionHeader(tr("4."), tr("Page Range")));
        auto *pg = new QButtonGroup(this); pg->setExclusive(true);
        m_allRadio     = new QRadioButton(tr("All pages"));   m_allRadio->setChecked(true);
        m_currentRadio = new QRadioButton(tr("Current page"));
        m_rangeRadio   = new QRadioButton(tr("Range"));
        pg->addButton(m_allRadio); pg->addButton(m_currentRadio);
        pg->addButton(m_rangeRadio);
        lv->addWidget(m_allRadio); lv->addWidget(m_currentRadio);
        auto *rangeRow = new QHBoxLayout; rangeRow->setSpacing(8);
        rangeRow->addWidget(m_rangeRadio);
        m_rangeEdit = new QLineEdit(QStringLiteral("1-%1").arg(m_pageCount));
        m_rangeEdit->setObjectName(QStringLiteral("XRangeInput"));
        m_rangeEdit->setToolTip(tr("For example: 1-3, 5, 8-10"));
        m_rangeEdit->setEnabled(false);
        rangeRow->addWidget(m_rangeEdit); rangeRow->addStretch();
        lv->addLayout(rangeRow);
        connect(m_rangeRadio, &QRadioButton::toggled, m_rangeEdit, &QLineEdit::setEnabled);
        connect(m_allRadio,     &QRadioButton::toggled, this, &ExportDialog::updateEstimate);
        connect(m_currentRadio, &QRadioButton::toggled, this, &ExportDialog::updateEstimate);
        connect(m_rangeRadio,   &QRadioButton::toggled, this, &ExportDialog::updateEstimate);
        connect(m_rangeEdit,    &QLineEdit::textChanged, this, &ExportDialog::updateEstimate);
        lv->addStretch();
        row->addLayout(lv, 1);

        // Right: quality
        auto *rv = new QVBoxLayout; rv->setSpacing(6);
        rv->addWidget(makeSectionHeader(tr("5."), tr("Quality & Compression")));
        auto *qRow = new QHBoxLayout; qRow->setSpacing(8);
        qRow->addWidget(new QLabel(tr("Quality")));
        m_qualityCombo = new QComboBox;
        m_qualityCombo->addItem(tr("Maximum"), 100);
        m_qualityCombo->addItem(tr("High"),     85);
        m_qualityCombo->addItem(tr("Medium"),   60);
        m_qualityCombo->addItem(tr("Low"),      40);
        m_qualityCombo->setCurrentIndex(1);
        qRow->addWidget(m_qualityCombo, 1);
        rv->addLayout(qRow);
        m_compressChk = new QCheckBox(tr("Compress images"));
        m_compressChk->setChecked(true);
        rv->addWidget(m_compressChk);
        connect(m_qualityCombo, &QComboBox::currentIndexChanged,
                this, &ExportDialog::updateEstimate);
        connect(m_compressChk, &QCheckBox::toggled, this, &ExportDialog::updateEstimate);
        rv->addStretch();
        row->addLayout(rv, 1);

        bl->addLayout(row);
    }
    mkSep();

    // ── 6. Options ────────────────────────────────────────────────────────
    bl->addWidget(makeSectionHeader(tr("6."), tr("Options")));
    {
        auto *row = new QHBoxLayout; row->setSpacing(16);
        auto *lv = new QVBoxLayout;
        m_commentsChk = new QCheckBox(tr("Include comments")); m_commentsChk->setChecked(true);
        m_formsChk    = new QCheckBox(tr("Keep forms"));       m_formsChk->setChecked(true);
        lv->addWidget(m_commentsChk); lv->addWidget(m_formsChk);
        auto *rv = new QVBoxLayout;
        m_fontsChk     = new QCheckBox(tr("Embed fonts"));          m_fontsChk->setChecked(true);
        m_openAfterChk = new QCheckBox(tr("Open file after export")); m_openAfterChk->setChecked(false);
        rv->addWidget(m_fontsChk); rv->addWidget(m_openAfterChk);
        row->addLayout(lv, 1); row->addLayout(rv, 1);
        bl->addLayout(row);
        connect(m_commentsChk, &QCheckBox::toggled, this, &ExportDialog::updateEstimate);
        connect(m_fontsChk,    &QCheckBox::toggled, this, &ExportDialog::updateEstimate);
    }
    mkSep();

    // ── 7. Security ───────────────────────────────────────────────────────
    bl->addWidget(makeSectionHeader(tr("7."), tr("Security (optional)")));
    m_passwordChk = new QCheckBox(tr("Enable password protection"));
    m_passwordChk->setChecked(false);
    bl->addWidget(m_passwordChk);
    {
        auto *row = new QHBoxLayout; row->setSpacing(12);
        m_passEdit = new QLineEdit;
        m_passEdit->setObjectName(QStringLiteral("XInput"));
        m_passEdit->setEchoMode(QLineEdit::Password);
        m_passEdit->setPlaceholderText(tr("Password"));
        m_passEdit->setEnabled(false);
        m_passConfirm = new QLineEdit;
        m_passConfirm->setObjectName(QStringLiteral("XInput"));
        m_passConfirm->setEchoMode(QLineEdit::Password);
        m_passConfirm->setPlaceholderText(tr("Confirm password"));
        m_passConfirm->setEnabled(false);
        row->addWidget(m_passEdit); row->addWidget(m_passConfirm);
        bl->addLayout(row);
    }
    connect(m_passwordChk, &QCheckBox::toggled, this, &ExportDialog::updatePasswordFields);
    connect(m_passwordChk, &QCheckBox::toggled, this, &ExportDialog::updateEstimate);

    root->addWidget(body, 1);

    // ── footer ────────────────────────────────────────────────────────────
    auto *footer = new QWidget;
    footer->setObjectName(QStringLiteral("XFooter"));
    footer->setFixedHeight(64);
    auto *fl = new QHBoxLayout(footer);
    fl->setContentsMargins(28, 0, 28, 0);
    fl->setSpacing(12);
    m_sizeLabel = new QLabel(tr("Estimated file size: —"));
    m_sizeLabel->setObjectName(QStringLiteral("XSizeLabel"));
    fl->addWidget(m_sizeLabel, 1);
    auto *cancelBtn = new QPushButton(tr("Cancel"));
    cancelBtn->setObjectName(QStringLiteral("XCancel"));
    cancelBtn->setFixedSize(100, 36);
    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
    fl->addWidget(cancelBtn);
    auto *exportBtn = new QPushButton(tr("Export"));
    exportBtn->setObjectName(QStringLiteral("XExport"));
    exportBtn->setFixedSize(120, 36);
    connect(exportBtn, &QPushButton::clicked, this, &ExportDialog::onExport);
    fl->addWidget(exportBtn);
    root->addWidget(footer);
}
