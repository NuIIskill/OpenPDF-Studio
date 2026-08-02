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
#include <QFileInfo>
#include <QDebug>
#include <memory>

#include "ui/theme/Theme.hpp"
#include "app/SafeWrite.hpp"
#include "app/SessionStore.hpp"

// QPdfWriter lives in QtGui, not in Qt6Pdf — writing works with either
// rendering backend.
#include <QPdfWriter>

#ifdef HAVE_PDF_RENDERING
#include "engine/view/PdfRenderer.hpp"
#endif
#ifdef HAVE_QT_PDF
#include <QPdfDocument>
#endif

#ifdef HAVE_QPDF
#include <qpdf/QPDF.hh>
#include <qpdf/QPDFWriter.hh>
#include <qpdf/QPDFPageDocumentHelper.hh>
#include <qpdf/QPDFPageObjectHelper.hh>
#include <qpdf/QPDFAcroFormDocumentHelper.hh>
#include <qpdf/QPDFObjectHandle.hh>
#include <map>
#include <memory>
#endif

#ifdef HAVE_PDF_RENDERING
// ─────────────────────────────────────────────────────────────────────────────
// OrganizerDoc — backend-neutral handle for one opened PDF
//
// The organizer needs nothing but page count, page size and a rasteriser, and
// Qt6Pdf as well as Poppler provide all three. Gating the dialog on Qt6Pdf
// alone left the Windows build (which is compiled against Poppler) with an
// organizer that could neither show pages nor save: "PDF writing requires
// Qt6Pdf."
// ─────────────────────────────────────────────────────────────────────────────
class OrganizerDoc
{
public:
    static OrganizerDoc *load(const QString &path)
    {
#ifdef HAVE_QT_PDF
        auto *doc = new QPdfDocument();
        if (doc->load(path) != QPdfDocument::Error::None) {
            delete doc;
            return nullptr;
        }
        return new OrganizerDoc(doc);
#else
        // Poppler can throw on malformed files — a failed load must stay a
        // "could not open" message box, never terminate the app.
        try {
            auto doc = Poppler::Document::load(path);
            if (!doc || doc->isLocked()) return nullptr;
            doc->setRenderHint(Poppler::Document::Antialiasing);
            doc->setRenderHint(Poppler::Document::TextAntialiasing);
            return new OrganizerDoc(std::move(doc));
        } catch (...) { return nullptr; }
#endif
    }

    ~OrganizerDoc()
    {
        m_renderer.reset();   // the renderer only borrows the document
#ifdef HAVE_QT_PDF
        delete m_doc;
#endif
    }

    int pageCount() const
    {
#ifdef HAVE_QT_PDF
        return m_doc->pageCount();
#else
        try { return m_doc->numPages(); } catch (...) { return 0; }
#endif
    }

    QSizeF pageSizePts(int page) const { return m_renderer->pageSizePts(page); }
    // scale = output pixels per PDF point.
    QImage render(int page, qreal scale) const { return m_renderer->renderPage(page, scale); }

private:
#ifdef HAVE_QT_PDF
    explicit OrganizerDoc(QPdfDocument *doc)
        : m_doc(doc), m_renderer(std::make_unique<PdfRenderer>(doc)) {}
    QPdfDocument *m_doc;
#else
    explicit OrganizerDoc(std::unique_ptr<Poppler::Document> doc)
        : m_doc(std::move(doc)),
          m_renderer(std::make_unique<PdfRenderer>(m_doc.get())) {}
    std::unique_ptr<Poppler::Document> m_doc;
#endif
    std::unique_ptr<PdfRenderer> m_renderer;
};
#endif // HAVE_PDF_RENDERING

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
        p.setBrush(Theme::DarkMode ? QColor(0x6A, 0x6A, 0x6A)
                                   : QColor(0xC4, 0xC9, 0xD4));
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
        const bool dk = Theme::DarkMode;
        return QStringLiteral(
            "QPushButton#CardCheck {"
            "  background:%1; border:2px solid %2; border-radius:5px;"
            "  color:transparent; font-size:13px; font-weight:700; padding:0;"
            "}"
            "QPushButton#CardCheck:checked {"
            "  background:#2563EB; border-color:#2563EB; color:white;"
            "}"
            "QPushButton#CardCheck:hover { border-color:%3; }"
            "QLabel#PageCardLabel { font-size:13px; font-weight:600; color:%4; padding-bottom:6px; }")
            .arg(dk ? QLatin1String("#2B2B2B") : QLatin1String("#FFFFFF"),
                 dk ? QLatin1String("#555555") : QLatin1String("#D1D5DB"),
                 dk ? QLatin1String("#3B82F6") : QLatin1String("#93C5FD"),
                 dk ? QLatin1String("#D8D8D8") : QLatin1String("#111827"));
    }

    void applyStyle(bool selected)
    {
        const bool dk = Theme::DarkMode;
        const QString bg     = dk ? QStringLiteral("#3A3A3A") : QStringLiteral("#FFFFFF");
        const QString border = dk ? QStringLiteral("#484848") : QStringLiteral("#E5E7EB");
        const QString hover  = dk ? QStringLiteral("#606060") : QStringLiteral("#CBD5E1");
        const QString sel    = dk ? QStringLiteral("#3B82F6") : QStringLiteral("#2563EB");

        if (selected) {
            setStyleSheet(QStringLiteral(
                "QFrame#PageCard { background:%1; border:2px solid %2; border-radius:10px; }")
                .arg(bg, sel)
                + childStyles());
        } else {
            setStyleSheet(QStringLiteral(
                "QFrame#PageCard { background:%1; border:1px solid %2; border-radius:10px; }"
                "QFrame#PageCard:hover { border:1px solid %3; }")
                .arg(bg, border, hover)
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
        m_targetPath = initialPath;
        addPdfPages(initialPath);
    }
}

PdfOrganizerDialog::~PdfOrganizerDialog()
{
#ifdef HAVE_PDF_RENDERING
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

    // Stylesheet — light and dark are kept as two complete sheets so each
    // theme can be read (and tweaked) as one coherent block.
    setStyleSheet(Theme::DarkMode ? QStringLiteral(R"(
QDialog { background: #2B2B2B; }
QWidget#OrganizerGrid  { background: #2B2B2B; }
QScrollArea#OrganizerScroll { background: #2B2B2B; border: none; }
QScrollArea#OrganizerScroll > QWidget > QWidget { background: #2B2B2B; }
QWidget#OrgToolbar  { background: #353535; border-bottom: 1px solid #484848; }
QWidget#OrgInfoBar  { background: #1E3358; border-bottom: 1px solid #2B4870; }
QWidget#OrgFooter   { background: #353535; border-top: 1px solid #484848; }
QPushButton#OrgBtn  {
    background: #404040; border: 1px solid #505050; border-radius: 6px;
    color: #D8D8D8; font-size: 13px; padding: 3px 10px; icon-size: 16px;
}
QPushButton#OrgBtn:hover  { background: #4A4A4A; border-color: #606060; }
QPushButton#OrgBtn:pressed { background: #555555; }
QPushButton#OrgBtn:disabled { color: #6B6B6B; background: #3A3A3A; border-color: #484848; }
QToolButton#OrgAddBtn {
    background: #1E3358; border: 1px solid #2B4870; border-radius: 6px;
    color: #93C5FD; font-size: 13px; font-weight: 600; padding: 3px 10px;
    icon-size: 16px;
}
QToolButton#OrgAddBtn:hover { background: #24406B; border-color: #3B82F6; }
QToolButton#OrgAddBtn:pressed { background: #2B4870; }
QToolButton#OrgAddBtn::menu-button {
    border-left: 1px solid #2B4870; width: 16px; border-radius: 0 6px 6px 0;
}
QToolButton#OrgAddBtn::menu-indicator {
    width: 7px; height: 7px;
    subcontrol-origin: padding; subcontrol-position: right center;
}
QPushButton#OrgDeleteBtn {
    background: transparent; border: none; border-radius: 6px;
    color: #F87171; font-size: 13px; padding: 3px 10px; icon-size: 16px;
}
QPushButton#OrgDeleteBtn:hover { background: #4A2B2B; }
QPushButton#OrgDeleteBtn:pressed { background: #5A3030; }
QPushButton#OrgDeleteBtn:disabled { color: #7F4A4A; }
QPushButton#OrgSaveBtn {
    background: #2563EB; border: none; border-radius: 6px;
    color: white; font-size: 13px; font-weight: 600; padding: 4px 18px;
}
QPushButton#OrgSaveBtn:hover { background: #1D4ED8; }
QPushButton#OrgSaveBtn:pressed { background: #1E40AF; }
QScrollArea#OrganizerScroll QScrollBar:vertical {
    background: transparent; width: 10px; margin: 0;
}
QScrollArea#OrganizerScroll QScrollBar::handle:vertical {
    background: #555555; border-radius: 5px; min-height: 32px;
}
QScrollArea#OrganizerScroll QScrollBar::handle:vertical:hover { background: #666666; }
QScrollArea#OrganizerScroll QScrollBar::add-line:vertical,
QScrollArea#OrganizerScroll QScrollBar::sub-line:vertical { height: 0; }
QScrollArea#OrganizerScroll QScrollBar::add-page:vertical,
QScrollArea#OrganizerScroll QScrollBar::sub-page:vertical { background: transparent; }
)") : QStringLiteral(R"(
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
QScrollArea#OrganizerScroll QScrollBar:vertical {
    background: transparent; width: 10px; margin: 0;
}
QScrollArea#OrganizerScroll QScrollBar::handle:vertical {
    background: #CBD5E1; border-radius: 5px; min-height: 32px;
}
QScrollArea#OrganizerScroll QScrollBar::handle:vertical:hover { background: #94A3B8; }
QScrollArea#OrganizerScroll QScrollBar::add-line:vertical,
QScrollArea#OrganizerScroll QScrollBar::sub-line:vertical { height: 0; }
QScrollArea#OrganizerScroll QScrollBar::add-page:vertical,
QScrollArea#OrganizerScroll QScrollBar::sub-page:vertical { background: transparent; }
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
        s->setStyleSheet(Theme::DarkMode ? QStringLiteral("background:#484848;")
                                         : QStringLiteral("background:#E5E7EB;"));
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

    const bool dk = Theme::DarkMode;

    auto *ico = new QLabel(QStringLiteral("ℹ"), m_infoBar);
    ico->setStyleSheet(QStringLiteral("color:%1; font-size:16px;")
                           .arg(dk ? QLatin1String("#60A5FA") : QLatin1String("#2563EB")));
    h->addWidget(ico);

    auto *txt = new QLabel(tr("Drag & drop pages to reorder"), m_infoBar);
    txt->setStyleSheet(QStringLiteral("color:%1; font-size:13px;")
                           .arg(dk ? QLatin1String("#93C5FD") : QLatin1String("#1D4ED8")));
    h->addWidget(txt, 1);

    auto *close = new QPushButton(QStringLiteral("✕"), m_infoBar);
    close->setFixedSize(24, 24);
    close->setStyleSheet(QStringLiteral(
        "QPushButton { background:transparent; border:none; color:%1; font-size:12px; }"
        "QPushButton:hover { color:%2; }")
        .arg(dk ? QLatin1String("#60A5FA") : QLatin1String("#3B82F6"),
             dk ? QLatin1String("#BFDBFE") : QLatin1String("#1D4ED8")));
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
    m_countLabel->setStyleSheet(QStringLiteral("color:%1; font-size:13px;")
        .arg(Theme::DarkMode ? QLatin1String("#D8D8D8") : QLatin1String("#374151")));
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
#ifdef HAVE_PDF_RENDERING
    if (!m_docs.contains(path)) {
        OrganizerDoc *doc = OrganizerDoc::load(path);
        if (!doc) {
            QMessageBox::warning(this, tr("Open PDF"),
                                 tr("Could not open: ") + path);
            return;
        }
        m_docs[path] = doc;
    }
    OrganizerDoc *doc = m_docs[path];
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
#ifdef HAVE_PDF_RENDERING
    else if (m_docs.contains(e.pdfPath)) {
        OrganizerDoc *doc   = m_docs[e.pdfPath];
        const qreal   scale = OrgConst::RENDER_DPI / 72.0;
        QImage img = doc->render(e.pageIndex, scale);

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

#ifdef HAVE_QPDF
// Assembles the output from the source page objects themselves: text, fonts,
// vector graphics, links and annotations are carried over unchanged, so the
// saved file stays selectable, searchable and small. Rotation becomes a
// /Rotate entry rather than rotated pixels.
//
// Returns false on any qpdf failure (encrypted or damaged source, unwritable
// target); writePdf() then falls back to the raster path.
bool PdfOrganizerDialog::writeVectorPdf(const QString &outPath)
{
    try {
        QPDF out;
        out.emptyPDF();
        QPDFPageDocumentHelper outPages(out);
        QPDFAcroFormDocumentHelper outForms(out);

        // One QPDF per source file, opened lazily and shared by all pages that
        // come from it. Held alongside its form helper because
        // fixCopiedAnnotations needs the source document's AcroForm view.
        struct Source {
            std::unique_ptr<QPDF>                       pdf;
            std::unique_ptr<QPDFAcroFormDocumentHelper> forms;
            std::vector<QPDFPageObjectHelper>           pages;
        };
        std::map<QString, Source> sources;

        auto sourceFor = [&](const QString &path) -> Source * {
            auto it = sources.find(path);
            if (it != sources.end()) return &it->second;

            Source s;
            s.pdf = std::make_unique<QPDF>();
            s.pdf->processFile(path.toLocal8Bit().constData());
            s.forms = std::make_unique<QPDFAcroFormDocumentHelper>(*s.pdf);
            s.pages = QPDFPageDocumentHelper(*s.pdf).getAllPages();
            return &sources.emplace(path, std::move(s)).first->second;
        };

        // Blank pages inherit the size of the page before them (the one after
        // them if they lead the document), so an inserted sheet matches the
        // document instead of defaulting to A4 in a Letter file.
        auto blankSizePt = [&](int at) -> QSizeF {
            for (int i = at - 1; i >= 0; --i)
                if (!m_pages[i].isBlank && m_docs.contains(m_pages[i].pdfPath))
                    return m_docs[m_pages[i].pdfPath]->pageSizePts(m_pages[i].pageIndex);
            for (int i = at + 1; i < m_pages.size(); ++i)
                if (!m_pages[i].isBlank && m_docs.contains(m_pages[i].pdfPath))
                    return m_docs[m_pages[i].pdfPath]->pageSizePts(m_pages[i].pageIndex);
            return QSizeF(595.0, 842.0);            // A4 fallback
        };

        for (int i = 0; i < m_pages.size(); ++i) {
            const PageEntry &e = m_pages[i];

            if (e.isBlank) {
                const QSizeF sz = blankSizePt(i);
                QPDFObjectHandle page = QPDFObjectHandle::newDictionary();
                page.replaceKey("/Type", QPDFObjectHandle::newName("/Page"));
                page.replaceKey("/MediaBox", QPDFObjectHandle::newFromRectangle(
                    QPDFObjectHandle::Rectangle(0, 0, sz.width(), sz.height())));
                page.replaceKey("/Resources", QPDFObjectHandle::newDictionary());
                page.replaceKey("/Contents", QPDFObjectHandle::newStream(&out, ""));
                outPages.addPage(QPDFPageObjectHelper(out.makeIndirectObject(page)), false);
            } else {
                Source *src = sourceFor(e.pdfPath);
                const auto idx = static_cast<std::size_t>(e.pageIndex);
                if (e.pageIndex < 0 || idx >= src->pages.size()) continue;
                outPages.addPage(src->pages[idx], false);
            }

            // addPage may copy rather than adopt the object, so the page has to
            // be fetched back from the output document before it is modified.
            auto added = outPages.getAllPages();
            if (added.empty()) continue;
            QPDFPageObjectHelper newPage = added.back();

            if (!e.isBlank) {
                Source *src = sourceFor(e.pdfPath);
                const auto idx = static_cast<std::size_t>(e.pageIndex);
                if (idx < src->pages.size()) {
                    // Without this, copies of a page share one annotation set and
                    // form fields drop out entirely (no page → field reference).
                    outForms.fixCopiedAnnotations(
                        newPage.getObjectHandle(),
                        src->pages[idx].getObjectHandle(),
                        *src->forms);
                }
            }

            if (e.rotation != 0)
                newPage.rotatePage(e.rotation, true);   // relative to /Rotate
        }

        // Staged — "Save as" onto one of the source files would otherwise
        // truncate the very document these pages are still being copied from.
        const QString staging = SafeWrite::stagingPath(outPath);
        if (staging.isEmpty()) return false;
        {
            QPDFWriter writer(out, staging.toLocal8Bit().constData());
            writer.write();
        }
        return SafeWrite::commit(staging, outPath);

    } catch (const std::exception &ex) {
        qWarning() << "[QPDF] organizer vector save failed:" << ex.what();
        return false;
    }
}
#endif // HAVE_QPDF

// Opens what was just written and checks it is a PDF with the expected number
// of pages. A file the reader cannot open would otherwise reach the document
// view as a blank document with no indication of what went wrong.
bool PdfOrganizerDialog::verifyWritten(const QString &path) const
{
#ifdef HAVE_PDF_RENDERING
    if (!QFileInfo::exists(path) || QFileInfo(path).size() == 0) {
        qWarning() << "[Organizer] nothing was written to" << path;
        return false;
    }
    std::unique_ptr<OrganizerDoc> check(OrganizerDoc::load(path));
    if (!check) {
        qWarning() << "[Organizer] the written file cannot be opened:" << path;
        return false;
    }
    if (check->pageCount() != m_pages.size()) {
        qWarning() << "[Organizer] wrote" << check->pageCount()
                   << "pages, expected" << m_pages.size();
        return false;
    }
    return true;
#else
    Q_UNUSED(path)
    return true;
#endif
}

bool PdfOrganizerDialog::writePdf(const QString &outPath)
{
#ifdef HAVE_QPDF
    if (writeVectorPdf(outPath) && verifyWritten(outPath)) {
        qInfo() << "[Organizer] saved" << m_pages.size() << "pages (vector) to"
                << outPath;
        return true;
    }
    qWarning() << "[Organizer] falling back to raster save for" << outPath;
#endif
#ifdef HAVE_PDF_RENDERING
    constexpr int   SAVE_DPI = 150;
    constexpr qreal scale    = SAVE_DPI / 72.0;

    // Returns the output page size in points for a given entry.
    // 90°/270° user rotation transposes width↔height so the saved page has
    // the correct landscape/portrait orientation.
    auto outputPageSizePt = [&](const PageEntry &e) -> QSizeF {
        if (e.isBlank || !m_docs.contains(e.pdfPath))
            return QSizeF(595.0, 842.0);        // A4 fallback
        QSizeF pt = m_docs[e.pdfPath]->pageSizePts(e.pageIndex);
        if (e.rotation == 90 || e.rotation == 270)
            pt.transpose();                     // landscape ↔ portrait
        return pt;
    };

    // Staged for the same reason as the vector path: the pages are rendered
    // from documents that may include the file being written.
    const QString staging = SafeWrite::stagingPath(outPath);
    if (staging.isEmpty()) return false;

    // Page size for page 1 must be set BEFORE QPainter::begin() so the first
    // page is opened at the correct size immediately.
    QPdfWriter writer(staging);
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
    if (!painter.isActive()) { SafeWrite::discard(staging); return false; }

    // A page that cannot be rendered would silently come out white. Producing a
    // document that merely looks empty is worse than failing the save, so the
    // misses are counted and reported instead of written.
    int lostPages = 0;

    for (int i = 0; i < m_pages.size(); ++i) {
        const PageEntry &e = m_pages[i];

        if (i > 0) {
            // Page size must be set BEFORE newPage() — newPage() opens the new
            // page at whatever size is current; setting it afterwards is too late.
            writer.setPageSize(QPageSize(outputPageSizePt(e),
                                         QPageSize::Point, {}, QPageSize::ExactMatch));
            writer.newPage();
        }

        if (e.isBlank)
            continue;   // blank page: leave it white — intentional

        if (!m_docs.contains(e.pdfPath)) {
            qWarning() << "[Organizer] page" << (i + 1) << "has no open source:"
                       << e.pdfPath;
            ++lostPages;
            continue;
        }

        // Render in the native (pre-user-rotation) orientation.
        QImage img = m_docs[e.pdfPath]->render(e.pageIndex, scale);
        if (img.isNull()) {
            qWarning() << "[Organizer] page" << (i + 1) << "of" << e.pdfPath
                       << "(index" << e.pageIndex << ") did not render";
            ++lostPages;
            continue;
        }

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

    if (lostPages > 0) {
        SafeWrite::discard(staging);
        QMessageBox::warning(this, tr("Save failed"),
                             tr("%1 of %2 pages could not be rendered, so the "
                                "document would have been saved blank. Your PDF "
                                "was not changed.")
                                 .arg(lostPages).arg(m_pages.size()));
        return false;
    }
    if (!SafeWrite::commit(staging, outPath) || !verifyWritten(outPath)) {
        QMessageBox::warning(this, tr("Save failed"),
                             tr("The organized document could not be written to "
                                "\"%1\". Your PDF was not changed.")
                                 .arg(QFileInfo(outPath).fileName()));
        return false;
    }
    qInfo() << "[Organizer] saved" << m_pages.size() << "pages (raster) to" << outPath;
    return true;
#else
    Q_UNUSED(outPath)
    QMessageBox::information(this, tr("Not Available"),
                             tr("PDF writing requires a PDF backend "
                                "(Qt6Pdf or Poppler)."));
    return false;
#endif
}

// "Save" takes the page changes into the session, it does not touch the file
// the user opened: the result goes to a session working file that the document
// view then shows. The target PDF is written when the user saves the document
// itself — until then the change is undoable by closing without saving, and the
// working file is what crash recovery will pick up.
void PdfOrganizerDialog::save()
{
    if (m_pages.isEmpty()) {
        QMessageBox::warning(this, tr("Empty"),
                             tr("Add at least one page before saving."));
        return;
    }

    // Nothing to take the changes into (organizer opened without a document),
    // or no writable session directory: ask for a destination rather than
    // parking the user's work somewhere they will not find it again.
    if (m_targetPath.isEmpty()) {
        saveAs();
        return;
    }
    const QString workPath = SessionStore::newWorkingFile(m_targetPath);
    if (workPath.isEmpty()) {
        saveAs();
        return;
    }

    if (writePdf(workPath)) {
        m_resultPath      = workPath;
        m_resultIsWorking = true;
        accept();
    }
}

// "Save as" is an explicit destination, so it writes the file for real.
void PdfOrganizerDialog::saveAs()
{
    if (m_pages.isEmpty()) {
        QMessageBox::warning(this, tr("Empty"),
                             tr("Add at least one page before saving."));
        return;
    }

    const QString outPath = QFileDialog::getSaveFileName(
        this, tr("Save PDF As"), m_targetPath, tr("PDF files (*.pdf)"));
    if (outPath.isEmpty()) return;

    if (writePdf(outPath)) {
        m_targetPath      = outPath;
        m_resultPath      = outPath;
        m_resultIsWorking = false;
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
