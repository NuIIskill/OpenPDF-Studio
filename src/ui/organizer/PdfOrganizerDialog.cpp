#include "PdfOrganizerDialog.hpp"

namespace OrgConst {
    constexpr int CARD_W    = 220;
    constexpr int CARD_H    = 265;
    constexpr int THUMB_W   = 172;
    constexpr int THUMB_H   = 210;
    constexpr int RENDER_DPI = 96;
    constexpr int COL_GAP   = 16;
    constexpr int ROW_GAP   = 16;
    constexpr int GRID_PAD  = 20;
}

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QScrollArea>
#include <QPushButton>
#include <QToolButton>
#include <QLabel>
#include <QFrame>
#include <QFileDialog>
#include <QMessageBox>
#include <QPixmap>
#include <QPainter>
#include <QCheckBox>
#include <QMenu>
#include <QMouseEvent>
#include <QApplication>
#include <QDrag>
#include <QMimeData>
#include <QResizeEvent>
#include <QEvent>
#include <QScrollBar>

#ifdef HAVE_QT_PDF
#include <QPdfDocument>
#include <QPdfWriter>
#endif

// ─────────────────────────────────────────────────────────────────────────────
// PageCard — individual page tile shown in the grid
// ─────────────────────────────────────────────────────────────────────────────
class PageCard : public QFrame
{
    Q_OBJECT
public:
    explicit PageCard(int index, QWidget *parent = nullptr)
        : QFrame(parent), m_index(index)
    {
        setObjectName(QStringLiteral("PageCard"));
        setFixedSize(OrgConst::CARD_W, OrgConst::CARD_H);
        setCursor(Qt::ArrowCursor);

        auto *root = new QVBoxLayout(this);
        root->setContentsMargins(0, 0, 0, 0);
        root->setSpacing(0);

        // ── Header row (drag handle + checkbox) ───────────────────────────
        auto *header = new QWidget(this);
        header->setFixedHeight(28);
        auto *hdr = new QHBoxLayout(header);
        hdr->setContentsMargins(8, 4, 8, 0);
        hdr->setSpacing(0);

        m_dragHandle = new QLabel(QStringLiteral("⠿⠿"), header);
        m_dragHandle->setObjectName(QStringLiteral("DragHandle"));
        m_dragHandle->setCursor(Qt::SizeAllCursor);
        hdr->addWidget(m_dragHandle, 1, Qt::AlignLeft | Qt::AlignVCenter);

        m_check = new QCheckBox(header);
        m_check->setFixedSize(20, 20);
        m_check->setCursor(Qt::PointingHandCursor);
        hdr->addWidget(m_check, 0, Qt::AlignRight | Qt::AlignVCenter);

        root->addWidget(header);

        // ── Thumbnail ─────────────────────────────────────────────────────
        m_thumbLabel = new QLabel(this);
        m_thumbLabel->setObjectName(QStringLiteral("ThumbLabel"));
        m_thumbLabel->setFixedSize(OrgConst::THUMB_W, OrgConst::THUMB_H);
        m_thumbLabel->setAlignment(Qt::AlignCenter);
        root->addWidget(m_thumbLabel, 0, Qt::AlignHCenter);

        // ── Page label ────────────────────────────────────────────────────
        m_pageLabel = new QLabel(this);
        m_pageLabel->setObjectName(QStringLiteral("PageCardLabel"));
        m_pageLabel->setAlignment(Qt::AlignCenter);
        root->addWidget(m_pageLabel, 1, Qt::AlignHCenter | Qt::AlignBottom);

        applyStyle(false);

        connect(m_check, &QCheckBox::toggled, this, &PageCard::checkToggled);
    }

    void setThumb(const QPixmap &px)  { m_thumbLabel->setPixmap(px); }
    void setPageLabel(const QString &t) { m_pageLabel->setText(t); }
    void setSelected(bool s)
    {
        if (m_selected == s) return;
        m_selected = s;
        applyStyle(s);
        QSignalBlocker blk(m_check);
        m_check->setChecked(s);
    }
    bool isSelected() const { return m_selected; }
    int  index() const      { return m_index; }
    void setIndex(int i)    { m_index = i; }

Q_SIGNALS:
    void clicked(int index, Qt::KeyboardModifiers mods);
    void checkToggled(bool checked);
    void dragStarted(int index);

protected:
    void mousePressEvent(QMouseEvent *e) override
    {
        if (e->button() == Qt::LeftButton) {
            m_dragStart = e->pos();
            Q_EMIT clicked(m_index, e->modifiers());
        }
        QFrame::mousePressEvent(e);
    }

    void mouseMoveEvent(QMouseEvent *e) override
    {
        if ((e->buttons() & Qt::LeftButton) &&
            (e->pos() - m_dragStart).manhattanLength() > QApplication::startDragDistance())
        {
            Q_EMIT dragStarted(m_index);
        }
        QFrame::mouseMoveEvent(e);
    }

private:
    void applyStyle(bool selected)
    {
        if (selected) {
            setStyleSheet(QStringLiteral(
                "QFrame#PageCard { background:#FFFFFF; border:2px solid #2563EB; border-radius:10px; }"));
        } else {
            setStyleSheet(QStringLiteral(
                "QFrame#PageCard { background:#FFFFFF; border:1px solid #E5E7EB; border-radius:10px; }"
                "QFrame#PageCard:hover { border:1px solid #CBD5E1; }"));
        }
    }

    int       m_index;
    bool      m_selected { false };
    QPoint    m_dragStart;
    QLabel   *m_dragHandle  { nullptr };
    QLabel   *m_thumbLabel  { nullptr };
    QLabel   *m_pageLabel   { nullptr };
    QCheckBox *m_check      { nullptr };
};

#include "PdfOrganizerDialog.moc"

// ─────────────────────────────────────────────────────────────────────────────
// PdfOrganizerDialog
// ─────────────────────────────────────────────────────────────────────────────

PdfOrganizerDialog::PdfOrganizerDialog(const QString &initialPath, QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("PDF Organizer — OpenPDF Studio"));
    setMinimumSize(860, 620);
    resize(1040, 700);
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

// ── UI construction ───────────────────────────────────────────────────────────

void PdfOrganizerDialog::buildUi()
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    root->addWidget(buildToolbar());
    root->addWidget(buildInfoBar());

    // Grid scroll area
    m_scroll = new QScrollArea(this);
    m_scroll->setFrameShape(QFrame::NoFrame);
    m_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_scroll->setWidgetResizable(true);
    m_scroll->setObjectName(QStringLiteral("OrganizerScroll"));

    m_gridContainer = new QWidget;
    m_gridContainer->setObjectName(QStringLiteral("OrganizerGrid"));
    m_gridContainer->setMinimumHeight(OrgConst::CARD_H + OrgConst::GRID_PAD * 2);
    m_scroll->setWidget(m_gridContainer);
    root->addWidget(m_scroll, 1);

    root->addWidget(buildFooter());

    // Stylesheet
    setStyleSheet(QStringLiteral(R"(
QDialog { background: #F8FAFC; }
QWidget#OrganizerGrid  { background: #F8FAFC; }
QScrollArea#OrganizerScroll { background: #F8FAFC; border: none; }
QScrollArea#OrganizerScroll > QWidget > QWidget { background: #F8FAFC; }
QWidget#OrgToolbar  { background: #FFFFFF; border-bottom: 1px solid #E5E7EB; }
QWidget#OrgInfoBar  { background: #EFF6FF; border-bottom: 1px solid #BFDBFE; }
QWidget#OrgFooter   { background: #FFFFFF; border-top: 1px solid #E5E7EB; }
QPushButton#OrgBtn  {
    background: transparent; border: 1px solid #D1D5DB; border-radius: 7px;
    color: #374151; font-size: 13px; padding: 5px 12px;
}
QPushButton#OrgBtn:hover { background: #F3F4F6; border-color: #9CA3AF; }
QPushButton#OrgBtn:disabled { color: #9CA3AF; border-color: #E5E7EB; }
QToolButton#OrgAddBtn {
    background: transparent; border: 1px solid #2563EB; border-radius: 7px;
    color: #2563EB; font-size: 13px; font-weight: 600; padding: 5px 14px;
}
QToolButton#OrgAddBtn:hover { background: #EFF6FF; }
QToolButton#OrgAddBtn::menu-button {
    border-left: 1px solid #2563EB; width: 20px; border-radius: 0 7px 7px 0;
}
QPushButton#OrgDeleteBtn {
    background: transparent; border: none;
    color: #DC2626; font-size: 13px; padding: 5px 12px;
}
QPushButton#OrgDeleteBtn:hover { background: #FEF2F2; border-radius: 7px; }
QPushButton#OrgDeleteBtn:disabled { color: #FCA5A5; }
QPushButton#OrgSaveBtn {
    background: #2563EB; border: none; border-radius: 7px;
    color: white; font-size: 13px; font-weight: 600; padding: 6px 20px;
}
QPushButton#OrgSaveBtn:hover { background: #1D4ED8; }
QLabel#PageCardLabel { font-size: 13px; font-weight: 600; color: #111827; padding-bottom: 6px; }
QLabel#DragHandle { color: #9CA3AF; font-size: 14px; letter-spacing: 2px; }
QCheckBox { spacing: 0; }
QCheckBox::indicator { width: 18px; height: 18px; border-radius: 4px;
    border: 2px solid #D1D5DB; background: white; }
QCheckBox::indicator:checked { background: #2563EB; border-color: #2563EB; image: none; }
QCheckBox::indicator:hover { border-color: #2563EB; }
)"));
}

QWidget *PdfOrganizerDialog::buildToolbar()
{
    auto *bar = new QWidget(this);
    bar->setObjectName(QStringLiteral("OrgToolbar"));
    bar->setFixedHeight(60);

    auto *h = new QHBoxLayout(bar);
    h->setContentsMargins(16, 0, 16, 0);
    h->setSpacing(6);

    // Add PDF (menu button)
    m_addPdfBtn = new QToolButton(bar);
    m_addPdfBtn->setObjectName(QStringLiteral("OrgAddBtn"));
    m_addPdfBtn->setText(tr("PDF hinzufügen"));
    m_addPdfBtn->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_addPdfBtn->setPopupMode(QToolButton::MenuButtonPopup);
    auto *addMenu = new QMenu(m_addPdfBtn);
    addMenu->addAction(tr("PDF-Datei öffnen…"), this, [this]() {
        const QString p = QFileDialog::getOpenFileName(
            this, tr("Open PDF"), {}, tr("PDF files (*.pdf)"));
        if (!p.isEmpty()) addPdfPages(p);
    });
    m_addPdfBtn->setMenu(addMenu);
    connect(m_addPdfBtn, &QToolButton::clicked, this, [this]() {
        const QString p = QFileDialog::getOpenFileName(
            this, tr("Open PDF"), {}, tr("PDF files (*.pdf)"));
        if (!p.isEmpty()) addPdfPages(p);
    });
    h->addWidget(m_addPdfBtn);

    // Add blank page
    m_addBlankBtn = new QPushButton(tr("Leere Seite"), bar);
    m_addBlankBtn->setObjectName(QStringLiteral("OrgBtn"));
    connect(m_addBlankBtn, &QPushButton::clicked, this, &PdfOrganizerDialog::addBlankPage);
    h->addWidget(m_addBlankBtn);

    // Separator
    auto makeSep = [&]() {
        auto *s = new QFrame(bar);
        s->setFrameShape(QFrame::VLine);
        s->setFixedWidth(1);
        s->setStyleSheet(QStringLiteral("background:#E5E7EB;"));
        h->addWidget(s);
        h->addSpacing(2);
    };
    makeSep();

    // Move left / right
    auto makeBtn = [&](const QString &label, const QString &tip) {
        auto *b = new QPushButton(label, bar);
        b->setObjectName(QStringLiteral("OrgBtn"));
        b->setToolTip(tip);
        b->setEnabled(false);
        h->addWidget(b);
        return b;
    };

    m_moveLeftBtn  = makeBtn(QStringLiteral("← ") + tr("Nach links"),  tr("Move page left"));
    m_moveRightBtn = makeBtn(QStringLiteral("→ ") + tr("Nach rechts"), tr("Move page right"));
    makeSep();

    m_rotLeftBtn   = makeBtn(QStringLiteral("↺ ") + tr("Links drehen"),  tr("Rotate left 90°"));
    m_rotRightBtn  = makeBtn(QStringLiteral("↻ ") + tr("Rechts drehen"), tr("Rotate right 90°"));
    makeSep();

    m_deleteBtn = new QPushButton(QStringLiteral("🗑 ") + tr("Seite löschen"), bar);
    m_deleteBtn->setObjectName(QStringLiteral("OrgDeleteBtn"));
    m_deleteBtn->setEnabled(false);
    h->addWidget(m_deleteBtn);

    h->addStretch(1);

    // Help button
    auto *helpBtn = new QPushButton(QStringLiteral("?"), bar);
    helpBtn->setObjectName(QStringLiteral("OrgBtn"));
    helpBtn->setFixedSize(32, 32);
    helpBtn->setToolTip(tr("Help"));
    h->addWidget(helpBtn);

    connect(m_moveLeftBtn,  &QPushButton::clicked, this, [this]() { moveSelected(-1); });
    connect(m_moveRightBtn, &QPushButton::clicked, this, [this]() { moveSelected(+1); });
    connect(m_rotLeftBtn,   &QPushButton::clicked, this, [this]() { rotateSelected(-90); });
    connect(m_rotRightBtn,  &QPushButton::clicked, this, [this]() { rotateSelected(+90); });
    connect(m_deleteBtn,    &QPushButton::clicked, this, &PdfOrganizerDialog::removeSelected);

    return bar;
}

QWidget *PdfOrganizerDialog::buildInfoBar()
{
    m_infoBar = new QWidget(this);
    m_infoBar->setObjectName(QStringLiteral("OrgInfoBar"));
    m_infoBar->setFixedHeight(40);

    auto *h = new QHBoxLayout(m_infoBar);
    h->setContentsMargins(16, 0, 12, 0);
    h->setSpacing(8);

    auto *ico = new QLabel(QStringLiteral("ℹ"), m_infoBar);
    ico->setStyleSheet(QStringLiteral("color:#2563EB; font-size:16px;"));
    h->addWidget(ico);

    auto *txt = new QLabel(tr("Seiten per Drag & Drop verschieben"), m_infoBar);
    txt->setStyleSheet(QStringLiteral("color:#1D4ED8; font-size:13px;"));
    h->addWidget(txt, 1);

    auto *close = new QPushButton(QStringLiteral("✕"), m_infoBar);
    close->setFixedSize(24, 24);
    close->setStyleSheet(QStringLiteral(
        "QPushButton { background:transparent; border:none; color:#3B82F6; font-size:12px; }"
        "QPushButton:hover { color:#1D4ED8; }"));
    connect(close, &QPushButton::clicked, m_infoBar, &QWidget::hide);
    h->addWidget(close);

    return m_infoBar;
}

QWidget *PdfOrganizerDialog::buildFooter()
{
    auto *footer = new QWidget(this);
    footer->setObjectName(QStringLiteral("OrgFooter"));
    footer->setFixedHeight(56);

    auto *h = new QHBoxLayout(footer);
    h->setContentsMargins(20, 0, 20, 0);
    h->setSpacing(8);

    m_countLabel = new QLabel(QStringLiteral("📄 0 ") + tr("Seiten"), footer);
    m_countLabel->setStyleSheet(QStringLiteral("color:#374151; font-size:13px;"));
    h->addWidget(m_countLabel);
    h->addStretch(1);

    m_cancelBtn = new QPushButton(tr("Abbrechen"), footer);
    m_cancelBtn->setObjectName(QStringLiteral("OrgBtn"));
    connect(m_cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
    h->addWidget(m_cancelBtn);

    m_saveAsBtn = new QPushButton(tr("Speichern als"), footer);
    m_saveAsBtn->setObjectName(QStringLiteral("OrgBtn"));
    connect(m_saveAsBtn, &QPushButton::clicked, this, &PdfOrganizerDialog::saveAs);
    h->addWidget(m_saveAsBtn);

    m_saveBtn = new QPushButton(tr("Speichern"), footer);
    m_saveBtn->setObjectName(QStringLiteral("OrgSaveBtn"));
    m_saveBtn->setDefault(true);
    connect(m_saveBtn, &QPushButton::clicked, this, &PdfOrganizerDialog::save);
    h->addWidget(m_saveBtn);

    return footer;
}

// ── Page operations ───────────────────────────────────────────────────────────

void PdfOrganizerDialog::addPdfPages(const QString &path)
{
#ifdef HAVE_QT_PDF
    if (!m_docs.contains(path)) {
        auto *doc = new QPdfDocument(this);
        if (doc->load(path) != QPdfDocument::Error::None) {
            delete doc;
            QMessageBox::warning(this, tr("Open PDF"),
                                 tr("Could not open: ") + path);
            return;
        }
        m_docs[path] = doc;
    }
    QPdfDocument *doc = m_docs[path];
    for (int i = 0; i < doc->pageCount(); ++i) {
        PageEntry e;
        e.pdfPath  = path;
        e.pageIndex = i;
        e.isBlank   = false;
        e.rotation  = 0;
        e.thumb     = renderThumb(e);
        m_pages.append(e);
    }
#else
    Q_UNUSED(path)
    PageEntry e;
    e.isBlank = true;
    e.thumb   = renderThumb(e);
    m_pages.append(e);
#endif
    rebuildCards();
    updatePageLabels();
    updateFooterCount();
}

void PdfOrganizerDialog::addBlankPage()
{
    PageEntry e;
    e.isBlank = true;
    e.thumb   = renderThumb(e);
    m_pages.append(e);
    rebuildCards();
    updatePageLabels();
    updateFooterCount();
}

void PdfOrganizerDialog::removeSelected()
{
    QList<int> toRemove;
    for (int i = 0; i < m_pages.size(); ++i)
        if (m_pages[i].selected) toRemove.prepend(i);
    for (int idx : toRemove)
        m_pages.removeAt(idx);
    m_lastClickedIndex = -1;
    rebuildCards();
    updatePageLabels();
    updateFooterCount();
}

void PdfOrganizerDialog::moveSelected(int delta)
{
    // Collect selected indices in order
    QList<int> sel;
    for (int i = 0; i < m_pages.size(); ++i)
        if (m_pages[i].selected) sel.append(i);
    if (sel.isEmpty()) return;

    if (delta < 0 && sel.first() == 0) return;
    if (delta > 0 && sel.last() == m_pages.size() - 1) return;

    if (delta < 0) {
        for (int i : sel)
            m_pages.swapItemsAt(i, i - 1);
    } else {
        for (int i = sel.size() - 1; i >= 0; --i)
            m_pages.swapItemsAt(sel[i], sel[i] + 1);
    }
    rebuildCards();
    updatePageLabels();
}

void PdfOrganizerDialog::rotateSelected(int degrees)
{
    for (int i = 0; i < m_pages.size(); ++i) {
        if (!m_pages[i].selected) continue;
        m_pages[i].rotation = (m_pages[i].rotation + degrees + 360) % 360;
        m_pages[i].thumb    = renderThumb(m_pages[i]);
        if (i < m_cards.size())
            m_cards[i]->setThumb(m_pages[i].thumb);
    }
}

void PdfOrganizerDialog::selectAll(bool on)
{
    for (int i = 0; i < m_pages.size(); ++i) {
        m_pages[i].selected = on;
        if (i < m_cards.size())
            m_cards[i]->setSelected(on);
    }
    updateSelectionButtons();
}

// ── Card management ───────────────────────────────────────────────────────────

PageCard *PdfOrganizerDialog::makeCard(int index)
{
    auto *card = new PageCard(index, m_gridContainer);

    connect(card, &PageCard::clicked, this,
            [this](int idx, Qt::KeyboardModifiers mods) {
        onCardClicked(idx, mods & Qt::ControlModifier, mods & Qt::ShiftModifier);
    });
    connect(card, &PageCard::checkToggled, this,
            [this, card](bool checked) {
        onCardCheckToggled(card->index(), checked);
    });
    connect(card, &PageCard::dragStarted, this, &PdfOrganizerDialog::startDrag);

    card->setThumb(m_pages[index].thumb);
    card->setSelected(m_pages[index].selected);
    return card;
}

void PdfOrganizerDialog::rebuildCards()
{
    for (PageCard *c : m_cards) c->deleteLater();
    m_cards.clear();

    for (int i = 0; i < m_pages.size(); ++i) {
        PageCard *card = makeCard(i);
        m_cards.append(card);
    }
    relayout();
    updateSelectionButtons();
}

void PdfOrganizerDialog::relayout()
{
    if (!m_gridContainer) return;

    const int availW = m_scroll->viewport()->width();
    const int cols   = qMax(1, (availW - OrgConst::GRID_PAD * 2 + OrgConst::COL_GAP) / (OrgConst::CARD_W + OrgConst::COL_GAP));

    const int rows   = m_cards.isEmpty() ? 0
                     : (m_cards.size() + cols - 1) / cols;

    const int totalH = OrgConst::GRID_PAD + rows * (OrgConst::CARD_H + OrgConst::ROW_GAP) + OrgConst::GRID_PAD;
    m_gridContainer->setMinimumHeight(qMax(totalH, OrgConst::CARD_H + OrgConst::GRID_PAD * 2));

    for (int i = 0; i < m_cards.size(); ++i) {
        const int col = i % cols;
        const int row = i / cols;
        const int x   = OrgConst::GRID_PAD + col * (OrgConst::CARD_W + OrgConst::COL_GAP);
        const int y   = OrgConst::GRID_PAD + row * (OrgConst::CARD_H + OrgConst::ROW_GAP);
        m_cards[i]->move(x, y);
        m_cards[i]->show();
    }
}

void PdfOrganizerDialog::updatePageLabels()
{
    for (int i = 0; i < m_cards.size(); ++i)
        m_cards[i]->setPageLabel(tr("Seite %1").arg(i + 1));
    for (int i = 0; i < m_cards.size(); ++i)
        m_cards[i]->setIndex(i);
}

void PdfOrganizerDialog::updateFooterCount()
{
    m_countLabel->setText(QStringLiteral("📄 %1 ").arg(m_pages.size()) + tr("Seiten"));
}

void PdfOrganizerDialog::updateSelectionButtons()
{
    bool anySelected = false;
    for (const auto &e : m_pages)
        if (e.selected) { anySelected = true; break; }

    m_moveLeftBtn->setEnabled(anySelected);
    m_moveRightBtn->setEnabled(anySelected);
    m_rotLeftBtn->setEnabled(anySelected);
    m_rotRightBtn->setEnabled(anySelected);
    m_deleteBtn->setEnabled(anySelected);
}

void PdfOrganizerDialog::onCardClicked(int index, bool ctrl, bool shift)
{
    if (shift && m_lastClickedIndex >= 0) {
        const int lo = qMin(m_lastClickedIndex, index);
        const int hi = qMax(m_lastClickedIndex, index);
        for (int i = 0; i < m_pages.size(); ++i) {
            const bool sel = (i >= lo && i <= hi);
            if (!ctrl) m_pages[i].selected = sel;
            else if (sel) m_pages[i].selected = true;
            if (i < m_cards.size()) m_cards[i]->setSelected(m_pages[i].selected);
        }
    } else if (ctrl) {
        m_pages[index].selected = !m_pages[index].selected;
        m_cards[index]->setSelected(m_pages[index].selected);
        m_lastClickedIndex = index;
    } else {
        for (int i = 0; i < m_pages.size(); ++i) {
            m_pages[i].selected = (i == index);
            if (i < m_cards.size()) m_cards[i]->setSelected(i == index);
        }
        m_lastClickedIndex = index;
    }
    updateSelectionButtons();
}

void PdfOrganizerDialog::onCardCheckToggled(int index, bool checked)
{
    if (index < 0 || index >= m_pages.size()) return;
    m_pages[index].selected = checked;
    m_cards[index]->setSelected(checked);
    if (checked) m_lastClickedIndex = index;
    updateSelectionButtons();
}

void PdfOrganizerDialog::moveCardTo(int from, int to)
{
    if (from == to || from < 0 || to < 0 ||
        from >= m_pages.size() || to >= m_pages.size()) return;
    const PageEntry e = m_pages.takeAt(from);
    m_pages.insert(to, e);
    rebuildCards();
    updatePageLabels();
    updateFooterCount();
    // Re-select the moved card
    if (to < m_pages.size()) {
        m_pages[to].selected = true;
        m_cards[to]->setSelected(true);
    }
    updateSelectionButtons();
}

// ── Drag-and-drop ─────────────────────────────────────────────────────────────

void PdfOrganizerDialog::startDrag(int fromIndex)
{
    if (fromIndex < 0 || fromIndex >= m_cards.size()) return;

    auto *mime = new QMimeData;
    mime->setData(QStringLiteral("application/x-page-index"),
                  QByteArray::number(fromIndex));

    auto *drag = new QDrag(this);
    drag->setMimeData(mime);

    // Thumbnail as drag pixmap
    QPixmap px = m_cards[fromIndex]->grab().scaled(
        OrgConst::CARD_W / 2, OrgConst::CARD_H / 2, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    drag->setPixmap(px);
    drag->setHotSpot(px.rect().center());

    m_gridContainer->setAcceptDrops(true);
    m_gridContainer->installEventFilter(this);
    drag->exec(Qt::MoveAction);
    m_gridContainer->setAcceptDrops(false);
    m_gridContainer->removeEventFilter(this);
}

bool PdfOrganizerDialog::eventFilter(QObject *obj, QEvent *e)
{
    if (obj == m_gridContainer) {
        if (e->type() == QEvent::DragEnter) {
            auto *de = static_cast<QDragEnterEvent *>(e);
            if (de->mimeData()->hasFormat(
                    QStringLiteral("application/x-page-index")))
                de->acceptProposedAction();
            return true;
        }
        if (e->type() == QEvent::DragMove) {
            static_cast<QDragMoveEvent *>(e)->acceptProposedAction();
            return true;
        }
        if (e->type() == QEvent::Drop) {
            auto *de = static_cast<QDropEvent *>(e);
            const int from = de->mimeData()
                ->data(QStringLiteral("application/x-page-index")).toInt();

            // Find the card we're dropping onto
            const QPoint pos = de->position().toPoint();
            int to = m_cards.size() - 1;
            for (int i = 0; i < m_cards.size(); ++i) {
                if (m_cards[i]->geometry().contains(pos)) { to = i; break; }
            }
            moveCardTo(from, to);
            de->acceptProposedAction();
            return true;
        }
    }
    return QDialog::eventFilter(obj, e);
}

// ── Rendering ─────────────────────────────────────────────────────────────────

QPixmap PdfOrganizerDialog::renderThumb(const PageEntry &e)
{
    QPixmap thumb(OrgConst::THUMB_W, OrgConst::THUMB_H);
    thumb.fill(Qt::white);
    QPainter p(&thumb);

    if (e.isBlank) {
        p.setPen(QPen(QColor(200, 200, 200), 1));
        p.drawRect(1, 1, OrgConst::THUMB_W - 2, OrgConst::THUMB_H - 2);
        p.setPen(QColor(180, 180, 180));
        QFont f; f.setPointSize(11); p.setFont(f);
        p.drawText(thumb.rect(), Qt::AlignCenter, QStringLiteral("□"));
    }
#ifdef HAVE_QT_PDF
    else if (m_docs.contains(e.pdfPath)) {
        QPdfDocument *doc = m_docs[e.pdfPath];
        const qreal   scale = OrgConst::RENDER_DPI / 72.0;
        const QSizeF  ps    = doc->pagePointSize(e.pageIndex);
        const QSize   sz(qRound(ps.width() * scale), qRound(ps.height() * scale));
        QImage img = doc->render(e.pageIndex, sz);

        // Apply rotation
        if (e.rotation != 0) {
            QTransform t;
            t.rotate(e.rotation);
            img = img.transformed(t, Qt::SmoothTransformation);
        }

        const QPixmap src = QPixmap::fromImage(img).scaled(
            OrgConst::THUMB_W - 4, OrgConst::THUMB_H - 4,
            Qt::KeepAspectRatio, Qt::SmoothTransformation);
        p.drawPixmap((OrgConst::THUMB_W - src.width()) / 2,
                     (OrgConst::THUMB_H - src.height()) / 2, src);
    }
#endif

    p.end();
    return thumb;
}

// ── Save ──────────────────────────────────────────────────────────────────────

void PdfOrganizerDialog::save()
{
    // If we know the original file (single source), save to it; otherwise saveAs
    saveAs();
}

void PdfOrganizerDialog::saveAs()
{
    if (m_pages.isEmpty()) {
        QMessageBox::warning(this, tr("Empty"),
                             tr("Add at least one page before saving."));
        return;
    }

    const QString outPath = QFileDialog::getSaveFileName(
        this, tr("Save PDF As"), {}, tr("PDF files (*.pdf)"));
    if (outPath.isEmpty()) return;

#ifdef HAVE_QT_PDF
    QPdfWriter writer(outPath);
    writer.setResolution(OrgConst::RENDER_DPI);
    QPainter painter(&writer);
    bool firstPage = true;

    for (const PageEntry &e : m_pages) {
        if (!firstPage) writer.newPage();
        firstPage = false;

        if (!e.isBlank && m_docs.contains(e.pdfPath)) {
            QPdfDocument *doc = m_docs[e.pdfPath];
            const QSizeF  ps  = doc->pagePointSize(e.pageIndex);
            const QSize   sz(qRound(ps.width() * (OrgConst::RENDER_DPI / 72.0)),
                             qRound(ps.height() * (OrgConst::RENDER_DPI / 72.0)));

            writer.setPageSize(QPageSize(ps * (OrgConst::RENDER_DPI / 72.0),
                               QPageSize::Point, {}, QPageSize::ExactMatch));

            QImage img = doc->render(e.pageIndex, sz);
            if (e.rotation != 0) {
                QTransform t; t.rotate(e.rotation);
                img = img.transformed(t, Qt::SmoothTransformation);
            }
            painter.drawImage(QRect({0,0}, img.size()), img);
        }
    }
    painter.end();
    accept();
#else
    QMessageBox::information(this, tr("Not Available"),
                             tr("PDF writing requires Qt6Pdf."));
#endif
}

// ── Qt event overrides ────────────────────────────────────────────────────────

void PdfOrganizerDialog::resizeEvent(QResizeEvent *e)
{
    QDialog::resizeEvent(e);
    relayout();
}

void PdfOrganizerDialog::retranslateUi()
{
    setWindowTitle(tr("PDF Organizer — OpenPDF Studio"));
    updatePageLabels();
    updateFooterCount();
}

void PdfOrganizerDialog::changeEvent(QEvent *e)
{
    if (e->type() == QEvent::LanguageChange)
        retranslateUi();
    QDialog::changeEvent(e);
}
