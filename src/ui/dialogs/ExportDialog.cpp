#include "ExportDialog.hpp"
#include "ui/theme/Theme.hpp"

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

ExportDialog::ExportDialog(const QString &currentFile, int pageCount, QWidget *parent)
    : QDialog(parent)
    , m_currentFile(currentFile)
    , m_pageCount(qMax(1, pageCount))
{
    setWindowTitle(tr("Export"));
    setModal(true);
    setFixedSize(720, 760);
    buildUi();
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

    const QString path = selectedPath();
    QStringList existing;
    if (m_selectedFormat == QLatin1String("image") && m_pageCount > 1) {
        const QFileInfo out(path);
        for (int page = 1; page <= m_pageCount; ++page) {
            const QString candidate = out.dir().filePath(
                out.completeBaseName() + QStringLiteral("_page_%1.png").arg(page));
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
        QLabel#XSizeLabel {
            font-size: 12px;
            color: palette(mid);
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
        auto *allR = new QRadioButton(tr("All pages"));   allR->setChecked(true);
        auto *curR = new QRadioButton(tr("Current page"));
        m_rangeRadio = new QRadioButton(tr("Range"));
        pg->addButton(allR); pg->addButton(curR); pg->addButton(m_rangeRadio);
        lv->addWidget(allR); lv->addWidget(curR);
        auto *rangeRow = new QHBoxLayout; rangeRow->setSpacing(8);
        rangeRow->addWidget(m_rangeRadio);
        m_rangeEdit = new QLineEdit(QStringLiteral("1-%1").arg(m_pageCount));
        m_rangeEdit->setObjectName(QStringLiteral("XRangeInput"));
        m_rangeEdit->setEnabled(false);
        rangeRow->addWidget(m_rangeEdit); rangeRow->addStretch();
        lv->addLayout(rangeRow);
        connect(m_rangeRadio, &QRadioButton::toggled, m_rangeEdit, &QLineEdit::setEnabled);
        lv->addStretch();
        row->addLayout(lv, 1);

        // Right: quality
        auto *rv = new QVBoxLayout; rv->setSpacing(6);
        rv->addWidget(makeSectionHeader(tr("5."), tr("Quality && Compression")));
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
