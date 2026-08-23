#pragma once

#include "app/DocumentHistory.hpp"

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

// Opened source documents are held through OrganizerDoc, which wraps whichever
// PDF backend the build uses (Qt6Pdf or Poppler). Defined in the .cpp.
class OrganizerDoc;

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

    /// The PDF the organized document belongs to. Defaults to the path passed
    /// to the constructor; set it explicitly when that path is a session
    /// working copy rather than the document the user opened.
    void setTargetPath(const QString &path) { m_targetPath = path; }
    QString targetPath() const { return m_targetPath; }

    /// File holding the organized document after the dialog was accepted:
    /// a session working file for "Save", the chosen file for "Save as".
    QString resultPath() const { return m_resultPath; }

    /// True when resultPath() is a session working file, i.e. the pages were
    /// taken into the session but targetPath() has not been written yet.
    bool resultIsWorkingCopy() const { return m_resultIsWorking; }

    /// What the dialog did to the document, for the change history. One entry
    /// per organizer run on purpose: everything the run did lands in a single
    /// written file, so a single file is what going back to it can restore.
    DocumentHistory::Change appliedChange() const;

    /// Writes the current page list to `path` without showing the dialog.
    /// For the headless --organize-save check.
    bool writeForTest(const QString &path);

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
    void      syncCards();
    int       columnCount() const;
    int       dropIndexAt(const QPoint &pos) const;
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
    // Reopens a written file and checks page count — a broken result must not
    // reach the document view as a silently blank document.
#ifdef HAVE_QPDF
    // Vector page assembly (qpdf). writePdf falls back to rasterising when
    // this fails or qpdf is unavailable.
#endif
    QPixmap renderThumb(const PageEntry &e);

    // State
    QList<PageEntry>  m_pages;
    QList<PageCard *> m_cards;
    int               m_lastClickedIndex { -1 };
    QString           m_targetPath;      // document the changes belong to ("" = none yet)
    // The document as it came in, to diff the result against.
    QString           m_initialPath;
    int               m_initialCount { 0 };
    QString           m_resultPath;      // file written on accept
    bool              m_resultIsWorking { false };  // m_resultPath is a session file

#ifdef HAVE_PDF_RENDERING
    QMap<QString, OrganizerDoc *> m_docs;
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
