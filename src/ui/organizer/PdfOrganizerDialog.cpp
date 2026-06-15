#include "PdfOrganizerDialog.hpp"

#include <QListWidget>
#include <QListWidgetItem>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QPushButton>
#include <QLabel>
#include <QFileDialog>
#include <QPixmap>
#include <QPainter>
#include <QMessageBox>
#include <QEvent>

#ifdef HAVE_QT_PDF
#include <QPdfDocument>
#include <QPdfWriter>
#endif

// ── Custom data roles ─────────────────────────────────────────────────────────
static constexpr int RolePdfPath  = Qt::UserRole + 1;
static constexpr int RolePageIdx  = Qt::UserRole + 2;
static constexpr int RoleIsBlank  = Qt::UserRole + 3;

PdfOrganizerDialog::PdfOrganizerDialog(const QString &initialPath, QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("PDF Organizer"));
    setMinimumSize(740, 560);
    buildUi();
    if (!initialPath.isEmpty())
        addPdfPages(initialPath);
}

PdfOrganizerDialog::~PdfOrganizerDialog()
{
#ifdef HAVE_QT_PDF
    qDeleteAll(m_docs);
#endif
}

void PdfOrganizerDialog::buildUi()
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(12, 12, 12, 12);
    root->setSpacing(8);

    m_list = new QListWidget(this);
    m_list->setViewMode(QListWidget::IconMode);
    m_list->setIconSize(QSize(THUMB_W, THUMB_H));
    m_list->setGridSize(QSize(GRID_W, GRID_H));
    m_list->setMovement(QListWidget::Snap);
    m_list->setDragDropMode(QAbstractItemView::InternalMove);
    m_list->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_list->setResizeMode(QListWidget::Adjust);
    m_list->setWordWrap(true);
    root->addWidget(m_list, 1);

    connect(m_list->model(), &QAbstractItemModel::rowsMoved,
            this, &PdfOrganizerDialog::updatePageNumbers);

    // Button row
    auto *btnRow = new QHBoxLayout;
    btnRow->setSpacing(8);

    auto makeBtn = [&](const QString &text) {
        auto *b = new QPushButton(text, this);
        btnRow->addWidget(b);
        return b;
    };

    auto *addPdfBtn     = makeBtn(tr("Add PDF…"));
    auto *addBlankBtn   = makeBtn(tr("Add Blank Page"));
    auto *removeBtn     = makeBtn(tr("Remove Selected"));
    btnRow->addStretch();
    auto *cancelBtn     = makeBtn(tr("Cancel"));
    auto *saveBtn       = makeBtn(tr("Save As…"));
    saveBtn->setDefault(true);

    root->addLayout(btnRow);

    connect(addPdfBtn,   &QPushButton::clicked, this, [this]() {
        const QString path = QFileDialog::getOpenFileName(
            this, tr("Open PDF"), {}, tr("PDF files (*.pdf)"));
        if (!path.isEmpty()) addPdfPages(path);
    });
    connect(addBlankBtn, &QPushButton::clicked, this, &PdfOrganizerDialog::addBlankPage);
    connect(removeBtn,   &QPushButton::clicked, this, &PdfOrganizerDialog::removeSelected);
    connect(cancelBtn,   &QPushButton::clicked, this, &QDialog::reject);
    connect(saveBtn,     &QPushButton::clicked, this, &PdfOrganizerDialog::saveAs);
}

void PdfOrganizerDialog::addPdfPages(const QString &path)
{
#ifdef HAVE_QT_PDF
    if (!m_docs.contains(path)) {
        auto *doc = new QPdfDocument(this);
        if (doc->load(path) != QPdfDocument::Error::None) {
            delete doc;
            return;
        }
        m_docs[path] = doc;
    }

    QPdfDocument *doc = m_docs[path];
    const qreal   scale = (RENDER_DPI / 72.0);

    for (int i = 0; i < doc->pageCount(); ++i) {
        const QSizeF ps = doc->pagePointSize(i);
        const QSize  sz(static_cast<int>(ps.width()  * scale),
                        static_cast<int>(ps.height() * scale));
        QImage img = doc->render(i, sz);

        QPixmap thumb(THUMB_W, THUMB_H);
        thumb.fill(Qt::white);
        QPainter p(&thumb);
        const QPixmap src = QPixmap::fromImage(img).scaled(
            THUMB_W, THUMB_H, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        p.drawPixmap((THUMB_W - src.width()) / 2, (THUMB_H - src.height()) / 2, src);
        p.end();

        const int pageNum = m_list->count() + 1;
        auto *item = new QListWidgetItem(QIcon(thumb),
                                         QStringLiteral("Page %1").arg(pageNum));
        item->setData(RolePdfPath, path);
        item->setData(RolePageIdx, i);
        item->setData(RoleIsBlank, false);
        m_list->addItem(item);
    }
#else
    Q_UNUSED(path)
    addBlankPage();
#endif
}

void PdfOrganizerDialog::addBlankPage()
{
    QPixmap thumb(THUMB_W, THUMB_H);
    thumb.fill(Qt::white);
    QPainter p(&thumb);
    p.setPen(QPen(QColor(200, 200, 200), 1));
    p.drawRect(0, 0, THUMB_W - 1, THUMB_H - 1);
    p.end();

    const int pageNum = m_list->count() + 1;
    auto *item = new QListWidgetItem(QIcon(thumb),
                                     QStringLiteral("Page %1").arg(pageNum));
    item->setData(RoleIsBlank, true);
    m_list->addItem(item);
}

void PdfOrganizerDialog::removeSelected()
{
    const auto selected = m_list->selectedItems();
    for (QListWidgetItem *item : selected)
        delete item;
    updatePageNumbers();
}

void PdfOrganizerDialog::updatePageNumbers()
{
    for (int i = 0; i < m_list->count(); ++i)
        m_list->item(i)->setText(QStringLiteral("Page %1").arg(i + 1));
}

void PdfOrganizerDialog::saveAs()
{
    if (m_list->count() == 0) {
        QMessageBox::warning(this, tr("Empty"), tr("Add at least one page before saving."));
        return;
    }

    const QString outPath = QFileDialog::getSaveFileName(
        this, tr("Save PDF As"), {}, tr("PDF files (*.pdf)"));
    if (outPath.isEmpty()) return;

#ifdef HAVE_QT_PDF
    QPdfWriter writer(outPath);
    writer.setResolution(RENDER_DPI);

    QPainter painter(&writer);
    bool firstPage = true;

    for (int row = 0; row < m_list->count(); ++row) {
        QListWidgetItem *item = m_list->item(row);
        if (!firstPage)
            writer.newPage();
        firstPage = false;

        const bool   isBlank  = item->data(RoleIsBlank).toBool();
        const QString pdfPath = item->data(RolePdfPath).toString();
        const int     pageIdx = item->data(RolePageIdx).toInt();

        if (!isBlank && m_docs.contains(pdfPath)) {
            QPdfDocument *doc = m_docs[pdfPath];
            const QSizeF  ps  = doc->pagePointSize(pageIdx);
            const QSize   sz(static_cast<int>(ps.width()  * (RENDER_DPI / 72.0)),
                             static_cast<int>(ps.height() * (RENDER_DPI / 72.0)));

            const QPageSize pageSize(ps * (RENDER_DPI / 72.0),
                                     QPageSize::Point,
                                     QString(),
                                     QPageSize::ExactMatch);
            writer.setPageSize(pageSize);

            const QImage img = doc->render(pageIdx, sz);
            painter.drawImage(QRect(QPoint(0, 0), sz), img);
        }
        // Blank pages: use default page size, leave white
    }

    painter.end();
    accept();
#else
    Q_UNUSED(outPath)
    QMessageBox::information(this, tr("Not Available"),
                             tr("PDF writing requires Qt6Pdf."));
#endif
}

void PdfOrganizerDialog::retranslateUi()
{
    setWindowTitle(tr("PDF Organizer"));
}

void PdfOrganizerDialog::changeEvent(QEvent *e)
{
    if (e->type() == QEvent::LanguageChange)
        retranslateUi();
    QDialog::changeEvent(e);
}
