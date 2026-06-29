#pragma once

#include <QDialog>
#include <QMap>
#include <QList>

QT_BEGIN_NAMESPACE
class QScrollArea;
class QWidget;
class QPushButton;
class QLabel;
class QToolButton;
QT_END_NAMESPACE

#ifdef HAVE_QT_PDF
#include <QPdfDocument>
#endif

// ── One page entry ────────────────────────────────────────────────────────────
struct PageEntry {
    QString  pdfPath;
    int      pageIndex { 0 };
    bool     isBlank   { false };
    int      rotation  { 0 };      // 0, 90, 180, 270
    QPixmap  thumb;
    bool     selected  { false };
};

class PageCard;

class PdfOrganizerDialog : public QDialog
{
    Q_OBJECT

public:
    explicit PdfOrganizerDialog(const QString &initialPath = {}, QWidget *parent = nullptr);
    ~PdfOrganizerDialog() override;

    void retranslateUi();
    QString savedPath() const { return m_sourcePath; }

protected:
    void changeEvent(QEvent *e) override;
    void resizeEvent(QResizeEvent *e) override;

private:
    // UI builders
    void buildUi();
    QWidget *buildToolbar();
    QWidget *buildInfoBar();
    QWidget *buildFooter();
    void     buildGrid();

    // Page operations
    void addPdfPages(const QString &path);
    void addBlankPage();
    void removeSelected();
    void moveSelected(int delta);
    void rotateSelected(int degrees);
    void selectAll(bool on);

    // Card management
    PageCard *makeCard(int index);
    void      rebuildCards();
    void      relayout();
    void      updatePageLabels();
    void      updateFooterCount();
    void      onCardClicked(int index, bool ctrl, bool shift);
    void      onCardCheckToggled(int index, bool checked);
    void      moveCardTo(int from, int to);
    void      startDrag(int fromIndex);
    void      updateSelectionButtons();

protected:
    bool eventFilter(QObject *obj, QEvent *e) override;

private:
    // Save
    void saveAs();
    void save();
    bool writePdf(const QString &outPath);
    QPixmap renderThumb(const PageEntry &e);

    // State
    QList<PageEntry>  m_pages;
    QList<PageCard *> m_cards;
    int               m_lastClickedIndex { -1 };
    QString           m_sourcePath;   // path to overwrite on "Save" (empty = unknown)

#ifdef HAVE_QT_PDF
    QMap<QString, QPdfDocument *> m_docs;
#endif


    // Widgets
    QWidget     *m_gridContainer { nullptr };
    QScrollArea *m_scroll        { nullptr };
    QWidget     *m_infoBar       { nullptr };
    QLabel      *m_countLabel    { nullptr };
    QPushButton *m_moveLeftBtn   { nullptr };
    QPushButton *m_moveRightBtn  { nullptr };
    QPushButton *m_rotLeftBtn    { nullptr };
    QPushButton *m_rotRightBtn   { nullptr };
    QPushButton *m_deleteBtn     { nullptr };
    QPushButton *m_saveBtn       { nullptr };
    QPushButton *m_saveAsBtn     { nullptr };
    QPushButton *m_cancelBtn     { nullptr };
    QToolButton *m_addPdfBtn     { nullptr };
    QPushButton *m_addBlankBtn   { nullptr };
};
