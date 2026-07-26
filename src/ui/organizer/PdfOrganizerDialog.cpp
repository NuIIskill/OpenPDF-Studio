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
#include <QStyle>
#include <QMargins>
#include <QPageSize>

#ifdef HAVE_QT_PDF
#include <QPdfDocument>
#include <QPdfWriter>
#endif

// ─────────────────────────────────────────────────────────────────────────────
// DragDots — 2-column × 3-row dot-grid grip icon, painted directly
// ─────────────────────────────────────────────────────────────────────────────
class DragDots : public QWidget
{
public:
    explicit DragDots(QWidget *parent = nullptr) : QWidget(parent)
    {
        setFixedSize(16, 22);
        setCursor(Qt::SizeAllCursor);
    }
protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.setBrush(QColor(0xC4, 0xC9, 0xD4));
        p.setPen(Qt::NoPen);
        constexpr int R    = 2;   // dot radius
        constexpr int xGap = 6;  // horizontal spacing (center-to-center)
        constexpr int yGap = 6;  // vertical spacing
        const int startX = (width()  - xGap) / 2;
        const int startY = (height() - yGap * 2) / 2;
        for (int row = 0; row < 3; ++row)
            for (int col = 0; col < 2; ++col)
                p.drawEllipse(QPoint(startX + col * xGap, startY + row * yGap), R, R);
    }
};

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

        m_dragHandle = new DragDots(header);
        hdr->addWidget(m_dragHandle, 1, Qt::AlignLeft | Qt::AlignVCenter);

        // Checkable QPushButton styled to look like a tick-box.
        // "✓" text is always present; CSS makes it transparent when unchecked
        // and white-on-blue when checked.
        m_check = new QPushButton(QStringLiteral("✓"), header);
        m_check->setObjectName(QStringLiteral("CardCheck"));
        m_check->setCheckable(true);
        m_check->setFixedSize(22, 22);
        m_check->setCursor(Qt::PointingHandCursor);
        m_check->setFocusPolicy(Qt::NoFocus);
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

        connect(m_check, &QPushButton::toggled, this, &PageCard::checkToggled);
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
            m_dragArmed = true;
            Q_EMIT clicked(m_index, e->modifiers());
        }
        QFrame::mousePressEvent(e);
    }

    void mouseMoveEvent(QMouseEvent *e) override
    {
        if (m_dragArmed && (e->buttons() & Qt::LeftButton) &&
            (e->pos() - m_dragStart).manhattanLength() > QApplication::startDragDistance())
        {
            // One drag per press. The signal runs the whole drag synchronously
            // and reorders the grid; a second emit from the same press would
            // carry this card's pre-drop index and move a page the user never
            // picked up.
            m_dragArmed = false;
            Q_EMIT dragStarted(m_index);
            return;
        }
        QFrame::mouseMoveEvent(e);
    }

private:
    // Child styles are bundled here because setStyleSheet on the card frame
    // creates a new style scope — dialog-level rules no longer reach children.
    static QString childStyles()
    {
        return QStringLiteral(
            "QPushButton#CardCheck {"
            "  background:white; border:2px solid #D1D5DB; border-radius:5px;"
            "  color:transparent; font-size:13px; font-weight:700; padding:0;"
            "}"
            "QPushButton#CardCheck:checked {"
            "  background:#2563EB; border-color:#2563EB; color:white;"
            "}"
            "QPushButton#CardCheck:hover { border-color:#93C5FD; }"
            "QLabel#PageCardLabel { font-size:13px; font-weight:600; color:#111827; padding-bottom:6px; }");
    }

    void applyStyle(bool selected)
    {
        if (selected) {
            setStyleSheet(QStringLiteral(
                "QFrame#PageCard { background:#FFFFFF; border:2px solid #2563EB; border-radius:10px; }")
                + childStyles());
        } else {
            setStyleSheet(QStringLiteral(
                "QFrame#PageCard { background:#FFFFFF; border:1px solid #E5E7EB; border-radius:10px; }"
                "QFrame#PageCard:hover { border:1px solid #CBD5E1; }")
                + childStyles());
        }
    }

    int        m_index;
    bool       m_selected { false };
    bool       m_dragArmed { false };
    QPoint     m_dragStart;
    DragDots  *m_dragHandle  { nullptr };
    QLabel    *m_thumbLabel  { nullptr };
    QLabel    *m_pageLabel   { nullptr };
    QPushButton *m_check     { nullptr };
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

    if (!initialPath.isEmpty()) {
        m_sourcePath = initialPath;
        addPdfPages(initialPath);
    }
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
    background: #F9FAFB; border: 1px solid #E5E7EB; border-radius: 6px;
    color: #374151; font-size: 13px; padding: 3px 10px; icon-size: 16px;
}
QPushButton#OrgBtn:hover  { background: #F3F4F6; border-color: #D1D5DB; }
QPushButton#OrgBtn:pressed { background: #E5E7EB; }
QPushButton#OrgBtn:disabled { color: #9CA3AF; background: #F9FAFB; border-color: #E5E7EB; }
QToolButton#OrgAddBtn {
    background: #EFF6FF; border: 1px solid #BFDBFE; border-radius: 6px;
    color: #1D4ED8; font-size: 13px; font-weight: 600; padding: 3px 10px;
    icon-size: 16px;
}
QToolButton#OrgAddBtn:hover { background: #DBEAFE; border-color: #93C5FD; }
QToolButton#OrgAddBtn:pressed { background: #BFDBFE; }
QToolButton#OrgAddBtn::menu-button {
    border-left: 1px solid #BFDBFE; width: 16px; border-radius: 0 6px 6px 0;
}
QToolButton#OrgAddBtn::menu-indicator {
    width: 7px; height: 7px;
    subcontrol-origin: padding; subcontrol-position: right center;
}
QPushButton#OrgDeleteBtn {
    background: transparent; border: none; border-radius: 6px;
    color: #DC2626; font-size: 13px; padding: 3px 10px; icon-size: 16px;
}
QPushButton#OrgDeleteBtn:hover { background: #FEF2F2; }
QPushButton#OrgDeleteBtn:pressed { background: #FEE2E2; }
QPushButton#OrgDeleteBtn:disabled { color: #FCA5A5; }
QPushButton#OrgSaveBtn {
    background: #2563EB; border: none; border-radius: 6px;
    color: white; font-size: 13px; font-weight: 600; padding: 4px 18px;
}
QPushButton#OrgSaveBtn:hover { background: #1D4ED8; }
QPushButton#OrgSaveBtn:pressed { background: #1E40AF; }
)"));
}

QWidget *PdfOrganizerDialog::buildToolbar()
{
    auto *bar = new QWidget(this);
    bar->setObjectName(QStringLiteral("OrgToolbar"));
    bar->setFixedHeight(50);

    auto *h = new QHBoxLayout(bar);
    h->setContentsMargins(12, 0, 12, 0);
    h->setSpacing(4);

    const QSize iconSz(16, 16);

    // Add PDF (menu button)
    m_addPdfBtn = new QToolButton(bar);
    m_addPdfBtn->setObjectName(QStringLiteral("OrgAddBtn"));
    m_addPdfBtn->setText(tr("Add PDF"));
    m_addPdfBtn->setIcon(QIcon::fromTheme(QStringLiteral("document-open")));
    m_addPdfBtn->setIconSize(iconSz);
    m_addPdfBtn->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_addPdfBtn->setPopupMode(QToolButton::MenuButtonPopup);
    auto *addMenu = new QMenu(m_addPdfBtn);
    addMenu->addAction(tr("Open PDF file…"), this, [this]() {
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
    m_addBlankBtn = new QPushButton(tr("Blank Page"), bar);
    m_addBlankBtn->setObjectName(QStringLiteral("OrgBtn"));
    m_addBlankBtn->setIcon(QIcon::fromTheme(QStringLiteral("document-new")));
    m_addBlankBtn->setIconSize(iconSz);
    connect(m_addBlankBtn, &QPushButton::clicked, this, &PdfOrganizerDialog::addBlankPage);
    h->addWidget(m_addBlankBtn);

    // Separator
    auto makeSep = [&]() {
        h->addSpacing(2);
        auto *s = new QWidget(bar);
        s->setFixedSize(1, 20);
        s->setStyleSheet(QStringLiteral("background:#E5E7EB;"));
        h->addWidget(s, 0, Qt::AlignVCenter);
        h->addSpacing(2);
    };
    makeSep();

    // Move left / right
    auto makeBtn = [&](const QString &label, const QString &tip, const QString &iconName) {
        auto *b = new QPushButton(label, bar);
        b->setObjectName(QStringLiteral("OrgBtn"));
        b->setToolTip(tip);
        b->setEnabled(false);
        b->setIcon(QIcon::fromTheme(iconName));
        b->setIconSize(iconSz);
        h->addWidget(b);
        return b;
    };

    m_moveLeftBtn  = makeBtn(tr("Move left"),   tr("Move page left"),    QStringLiteral("go-previous"));
    m_moveRightBtn = makeBtn(tr("Move right"),  tr("Move page right"),   QStringLiteral("go-next"));
    makeSep();

    m_rotLeftBtn   = makeBtn(tr("Rotate left"),  tr("Rotate left 90°"),  QStringLiteral("object-rotate-left"));
    m_rotRightBtn  = makeBtn(tr("Rotate right"), tr("Rotate right 90°"), QStringLiteral("object-rotate-right"));
    makeSep();

    m_deleteBtn = new QPushButton(tr("Delete page"), bar);
    m_deleteBtn->setObjectName(QStringLiteral("OrgDeleteBtn"));
    m_deleteBtn->setIcon(QIcon::fromTheme(QStringLiteral("edit-delete")));
    m_deleteBtn->setIconSize(iconSz);
    m_deleteBtn->setEnabled(false);
    h->addWidget(m_deleteBtn);

    h->addStretch(1);

    // Help button
    auto *helpBtn = new QPushButton(bar);
    helpBtn->setObjectName(QStringLiteral("OrgBtn"));
    helpBtn->setIcon(QIcon::fromTheme(QStringLiteral("help-browser"),
                                      QIcon::fromTheme(QStringLiteral("help-contents"))));
    helpBtn->setIconSize(iconSz);
    helpBtn->setFixedSize(30, 30);
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

    auto *txt = new QLabel(tr("Drag & drop pages to reorder"), m_infoBar);
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

    m_countLabel = new QLabel(QStringLiteral("📄 0 ") + tr("Pages"), footer);
    m_countLabel->setStyleSheet(QStringLiteral("color:#374151; font-size:13px;"));
    h->addWidget(m_countLabel);
    h->addStretch(1);

    m_cancelBtn = new QPushButton(tr("Cancel"), footer);
    m_cancelBtn->setObjectName(QStringLiteral("OrgBtn"));
    connect(m_cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
    h->addWidget(m_cancelBtn);

    m_saveAsBtn = new QPushButton(tr("Save as"), footer);
    m_saveAsBtn->setObjectName(QStringLiteral("OrgBtn"));
    connect(m_saveAsBtn, &QPushButton::clicked, this, &PdfOrganizerDialog::saveAs);
    h->addWidget(m_saveAsBtn);

    m_saveBtn = new QPushButton(tr("Save"), footer);
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
    syncCards();
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

// Reorder path: the cards themselves are interchangeable, so re-point the
// existing ones at their new entries instead of destroying and recreating
// them. Rebuilding mid-drag would delete the card whose mouse handler is still
// on the stack and leave the freed cards painted over the new grid.
void PdfOrganizerDialog::syncCards()
{
    if (m_cards.size() != m_pages.size()) {
        rebuildCards();
        updatePageLabels();
        return;
    }
    for (int i = 0; i < m_pages.size(); ++i) {
        m_cards[i]->setIndex(i);
        m_cards[i]->setThumb(m_pages[i].thumb);
        m_cards[i]->setSelected(m_pages[i].selected);
        m_cards[i]->setPageLabel(tr("Page %1").arg(i + 1));
    }
    updateSelectionButtons();
}

int PdfOrganizerDialog::columnCount() const
{
    // viewport()->width() returns 0 before the dialog is shown (the constructor
    // asks before Qt has applied the layout to children). Fall back to the
    // dialog's own width minus a scrollbar allowance so the initial grid is
    // already correct when the window appears.
    int availW = m_scroll->viewport()->width();
    if (availW < OrgConst::CARD_W + OrgConst::GRID_PAD * 2) {
        const int sbW = style()->pixelMetric(QStyle::PM_ScrollBarExtent);
        availW = qMax(0, width() - sbW);
    }
    return qMax(1, (availW - OrgConst::GRID_PAD * 2 + OrgConst::COL_GAP)
                       / (OrgConst::CARD_W + OrgConst::COL_GAP));
}

void PdfOrganizerDialog::relayout()
{
    if (!m_gridContainer) return;

    const int cols   = columnCount();

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
        m_cards[i]->setPageLabel(tr("Page %1").arg(i + 1));
    for (int i = 0; i < m_cards.size(); ++i)
        m_cards[i]->setIndex(i);
}

void PdfOrganizerDialog::updateFooterCount()
{
    m_countLabel->setText(QStringLiteral("📄 %1 ").arg(m_pages.size()) + tr("Pages"));
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
    if (from < 0 || from >= m_pages.size() || m_pages.isEmpty()) return;
    to = qBound(0, to, m_pages.size() - 1);
    if (from == to) return;

    const PageEntry e = m_pages.takeAt(from);
    m_pages.insert(to, e);

    // Keep the moved page selected and make it the shift-selection anchor —
    // otherwise the anchor still points at whatever page now sits at `from`.
    m_pages[to].selected = true;
    m_lastClickedIndex   = to;

    syncCards();
}

// ── Drag-and-drop ─────────────────────────────────────────────────────────────

// Maps a drop point (in grid-container coordinates, so already scroll-aware)
// onto the grid slot it falls in. Hit-testing the card rectangles instead would
// leave the gaps and the padding unmatched, and every drop that landed a few
// pixels beside a card silently sent the page to the end of the document.
int PdfOrganizerDialog::dropIndexAt(const QPoint &pos) const
{
    if (m_cards.isEmpty()) return 0;

    const int cols = columnCount();
    const int col  = qBound(0, (pos.x() - OrgConst::GRID_PAD)
                                   / (OrgConst::CARD_W + OrgConst::COL_GAP), cols - 1);
    const int row  = qMax(0, (pos.y() - OrgConst::GRID_PAD)
                                 / (OrgConst::CARD_H + OrgConst::ROW_GAP));

    return qBound(0, row * cols + col, m_cards.size() - 1);
}

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
            moveCardTo(from, dropIndexAt(de->position().toPoint()));
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

bool PdfOrganizerDialog::writePdf(const QString &outPath)
{
#ifdef HAVE_QT_PDF
    constexpr int   SAVE_DPI = 150;
    constexpr qreal scale    = SAVE_DPI / 72.0;

    // Returns the output page size in points for a given entry.
    // 90°/270° user rotation transposes width↔height so the saved page has
    // the correct landscape/portrait orientation.
    auto outputPageSizePt = [&](const PageEntry &e) -> QSizeF {
        if (e.isBlank || !m_docs.contains(e.pdfPath))
            return QSizeF(595.0, 842.0);        // A4 fallback
        QSizeF pt = m_docs[e.pdfPath]->pagePointSize(e.pageIndex);
        if (e.rotation == 90 || e.rotation == 270)
            pt.transpose();                     // landscape ↔ portrait
        return pt;
    };

    // Page size for page 1 must be set BEFORE QPainter::begin() so the first
    // page is opened at the correct size immediately.
    QPdfWriter writer(outPath);
    writer.setCreator(QStringLiteral("OpenPDF Studio"));
    writer.setResolution(SAVE_DPI);
    if (!m_pages.isEmpty())
        writer.setPageSize(QPageSize(outputPageSizePt(m_pages[0]),
                                     QPageSize::Point, {}, QPageSize::ExactMatch));

    // QPdfWriter defaults to ~10 pt page margins, which shrink the painter's
    // paint rect and put its origin inside the page — a full-page image would
    // be nudged down/right and clipped off the right and bottom edges.
    // Zeroing them once is enough; setPageSize() below keeps zero margins.
    writer.setPageMargins(QMarginsF(0, 0, 0, 0));

    QPainter painter(&writer);
    if (!painter.isActive()) return false;

    for (int i = 0; i < m_pages.size(); ++i) {
        const PageEntry &e = m_pages[i];

        if (i > 0) {
            // Page size must be set BEFORE newPage() — newPage() opens the new
            // page at whatever size is current; setting it afterwards is too late.
            writer.setPageSize(QPageSize(outputPageSizePt(e),
                                         QPageSize::Point, {}, QPageSize::ExactMatch));
            writer.newPage();
        }

        if (e.isBlank || !m_docs.contains(e.pdfPath))
            continue;   // blank page: leave it white

        QPdfDocument *doc    = m_docs[e.pdfPath];
        const QSizeF  origPt = doc->pagePointSize(e.pageIndex);

        // Render in the native (pre-user-rotation) orientation.
        const QSize renderSz(qRound(origPt.width()  * scale),
                             qRound(origPt.height() * scale));
        QImage img = doc->render(e.pageIndex, renderSz);
        if (img.isNull()) continue;

        // Apply user rotation — for 90°/270° this transposes the image dimensions
        // to match the transposed page size set above.
        if (e.rotation != 0) {
            QTransform t;
            t.rotate(e.rotation);
            img = img.transformed(t, Qt::SmoothTransformation);
        }

        // Target the device's paint rect rather than the image size, so the
        // page is filled exactly even when point→pixel rounding differs.
        painter.drawImage(QRect(0, 0, painter.device()->width(),
                                painter.device()->height()), img);
    }
    painter.end();
    return true;
#else
    Q_UNUSED(outPath)
    QMessageBox::information(this, tr("Not Available"),
                             tr("PDF writing requires Qt6Pdf."));
    return false;
#endif
}

void PdfOrganizerDialog::save()
{
    if (m_pages.isEmpty()) {
        QMessageBox::warning(this, tr("Empty"),
                             tr("Add at least one page before saving."));
        return;
    }

    if (m_sourcePath.isEmpty()) {
        saveAs();
        return;
    }

    if (writePdf(m_sourcePath))
        accept();
}

void PdfOrganizerDialog::saveAs()
{
    if (m_pages.isEmpty()) {
        QMessageBox::warning(this, tr("Empty"),
                             tr("Add at least one page before saving."));
        return;
    }

    const QString outPath = QFileDialog::getSaveFileName(
        this, tr("Save PDF As"), m_sourcePath, tr("PDF files (*.pdf)"));
    if (outPath.isEmpty()) return;

    if (writePdf(outPath)) {
        m_sourcePath = outPath;
        accept();
    }
}

// ── Qt event overrides ────────────────────────────────────────────────────────

void PdfOrganizerDialog::resizeEvent(QResizeEvent *e)
{
    QDialog::resizeEvent(e);
    relayout();
    // Queue a second pass: the scroll viewport finalizes its size (accounting
    // for scrollbar visibility) only after the current resize event returns.
    QMetaObject::invokeMethod(this, [this]() { relayout(); }, Qt::QueuedConnection);
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
