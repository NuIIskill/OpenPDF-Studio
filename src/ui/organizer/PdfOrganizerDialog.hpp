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

class OrganizerDoc;

struct PageEntry {
    QString  pdfPath;
    int      pageIndex { 0 };
    bool     isBlank   { false };
    int      rotation  { 0 };
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

    void setTargetPath(const QString &path) { m_targetPath = path; }
    QString targetPath() const { return m_targetPath; }

    QString resultPath() const { return m_resultPath; }

    bool resultIsWorkingCopy() const { return m_resultIsWorking; }

    DocumentHistory::Change appliedChange() const;

    bool writeForTest(const QString &path);

protected:
    void changeEvent(QEvent *e) override;
    void resizeEvent(QResizeEvent *e) override;

private:

    void buildUi();
    QWidget *buildToolbar();
    QWidget *buildInfoBar();
    QWidget *buildFooter();
    void     buildGrid();

    void addPdfPages(const QString &path);
    void addBlankPage();
    void removeSelected();
    void moveSelected(int delta);
    void rotateSelected(int degrees);
    void selectAll(bool on);

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

    void saveAs();
    void save();
    bool writePdf(const QString &outPath);

#ifdef HAVE_QPDF

#endif
    QPixmap renderThumb(const PageEntry &e);

    QList<PageEntry>  m_pages;
    QList<PageCard *> m_cards;
    int               m_lastClickedIndex { -1 };
    QString           m_targetPath;

    QString           m_initialPath;
    int               m_initialCount { 0 };
    QString           m_resultPath;
    bool              m_resultIsWorking { false };

#ifdef HAVE_PDF_RENDERING
    QMap<QString, OrganizerDoc *> m_docs;
#endif

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
