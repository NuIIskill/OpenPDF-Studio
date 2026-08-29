#include "ui/organizer/PdfOrganizerDialog.hpp"
#include "app/PdfPwStore.hpp"
#include "ui/widgets/PasswordDialog.hpp"
#include "engine/document/OrganizerDoc.hpp"
#include "engine/document/OrganizerWriter.hpp"
#include "ui/organizer/PageCard.hpp"

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
#include <QSet>
#include <QDebug>
#include <algorithm>
#include <memory>

#include "ui/theme/Theme.hpp"
#include "app/SafeWrite.hpp"
#include "app/SessionStore.hpp"

#include <QPdfWriter>

#ifdef HAVE_PDF_RENDERING
#include "engine/document/PdfBackend.hpp"
#include "engine/render/PdfRenderer.hpp"
#endif

#ifdef HAVE_QPDF
#include <qpdf/Constants.h>
#include <qpdf/QPDF.hh>
#include <qpdf/QPDFWriter.hh>
#include <qpdf/QPDFPageDocumentHelper.hh>
#include <qpdf/QPDFPageObjectHelper.hh>
#include <qpdf/QPDFAcroFormDocumentHelper.hh>
#include <qpdf/QPDFObjectHandle.hh>
#include <map>
#include <memory>
#endif

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
        m_initialPath  = initialPath;
        m_initialCount = static_cast<int>(m_pages.size());
    }
}

PdfOrganizerDialog::~PdfOrganizerDialog()
{
#ifdef HAVE_PDF_RENDERING
    qDeleteAll(m_docs);
#endif
}

void PdfOrganizerDialog::buildUi()
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    root->addWidget(buildToolbar());
    root->addWidget(buildInfoBar());

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

    m_addBlankBtn = new QPushButton(tr("Blank Page"), bar);
    m_addBlankBtn->setObjectName(QStringLiteral("OrgBtn"));
    m_addBlankBtn->setIcon(QIcon::fromTheme(QStringLiteral("document-new")));
    m_addBlankBtn->setIconSize(iconSz);
    connect(m_addBlankBtn, &QPushButton::clicked, this, &PdfOrganizerDialog::addBlankPage);
    h->addWidget(m_addBlankBtn);

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

void PdfOrganizerDialog::addPdfPages(const QString &path)
{
#ifdef HAVE_PDF_RENDERING
    if (!m_docs.contains(path)) {

        bool needsPassword = false;
        OrganizerDoc *doc = OrganizerDoc::load(path, PdfPwStore::get(path),
                                               &needsPassword);
        for (int attempt = 0; !doc && needsPassword; ++attempt) {
            PasswordDialog prompt(QFileInfo(path).fileName(), attempt > 0, this);
            if (prompt.exec() != QDialog::Accepted) return;
            doc = OrganizerDoc::load(path, prompt.password(), &needsPassword);
            if (doc) PdfPwStore::set(path, prompt.password());
        }
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

    m_pages[to].selected = true;
    m_lastClickedIndex   = to;

    syncCards();
}

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

#ifdef HAVE_QPDF

#endif

bool PdfOrganizerDialog::writePdf(const QString &outPath)
{
    QList<OrganizerPage> pages;
    pages.reserve(m_pages.size());
    for (const PageEntry &e : m_pages)
        pages.append({ e.pdfPath, e.pageIndex, e.isBlank, e.rotation });

    const OrganizerWriter::Result r = OrganizerWriter(pages, m_docs).write(outPath);
    if (r.ok) return true;

    switch (r.error) {
    case OrganizerWriter::Error::RenderFailures:
        QMessageBox::warning(this, tr("Save failed"),
                             tr("%1 of %2 pages could not be rendered, so the "
                                "document would have been saved blank. Your PDF "
                                "was not changed.")
                                 .arg(r.lostPages).arg(r.totalPages));
        break;
    case OrganizerWriter::Error::WriteFailed:
        QMessageBox::warning(this, tr("Save failed"),
                             tr("The organized document could not be written to "
                                "\"%1\". Your PDF was not changed.")
                                 .arg(QFileInfo(outPath).fileName()));
        break;
    case OrganizerWriter::Error::NoBackend:
        QMessageBox::information(this, tr("Not Available"),
                                 tr("PDF writing requires a PDF backend "
                                    "(Qt6Pdf or Poppler)."));
        break;
    case OrganizerWriter::Error::None:
        break;
    }
    return false;
}

bool PdfOrganizerDialog::writeForTest(const QString &path)
{
    return writePdf(path);
}

void PdfOrganizerDialog::save()
{
    if (m_pages.isEmpty()) {
        QMessageBox::warning(this, tr("Empty"),
                             tr("Add at least one page before saving."));
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

DocumentHistory::Change PdfOrganizerDialog::appliedChange() const
{
    using Kind = DocumentHistory::Kind;

    DocumentHistory::Change c;
    c.kind  = Kind::PagesOrganized;
    c.count = static_cast<int>(m_pages.size());

    QList<int> survivors;
    QSet<int>  seen;
    int rotated = 0, added = 0;
    int firstRotatedPos = -1, firstAddedPos = -1, rotation = 0;

    for (int i = 0; i < m_pages.size(); ++i) {
        const PageEntry &e = m_pages[i];
        const bool fromSource = !e.isBlank && !m_initialPath.isEmpty()
                             && e.pdfPath == m_initialPath && !seen.contains(e.pageIndex);
        if (fromSource) {
            seen.insert(e.pageIndex);
            survivors.append(e.pageIndex);
        } else {
            if (added++ == 0) firstAddedPos = i;
        }
        if (e.rotation != 0) {
            if (rotated++ == 0) { firstRotatedPos = i; rotation = e.rotation; }
        }
    }

    const int deleted = qMax(0, m_initialCount - static_cast<int>(survivors.size()));
    int firstDeleted = -1;
    if (deleted > 0) {
        for (int p = 0; p < m_initialCount; ++p)
            if (!seen.contains(p)) { firstDeleted = p; break; }
    }
    const bool reordered = !std::is_sorted(survivors.cbegin(), survivors.cend());

    const int kinds = (deleted > 0) + (added > 0) + (rotated > 0) + (reordered ? 1 : 0);
    if (kinds != 1) return c;

    if (rotated > 0) {
        c.kind  = Kind::PageRotated;
        c.count = rotated;
        c.page  = firstRotatedPos;

        c.value = rotation == 270 ? -90 : rotation;
    } else if (deleted > 0) {
        c.kind  = Kind::PageDeleted;
        c.count = deleted;
        c.page  = firstDeleted;
    } else if (added > 0) {
        c.kind  = Kind::PageAdded;
        c.count = added;
        c.page  = firstAddedPos;
    } else {
        c.kind = Kind::PagesReordered;
    }
    return c;
}

void PdfOrganizerDialog::resizeEvent(QResizeEvent *e)
{
    QDialog::resizeEvent(e);
    relayout();

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
