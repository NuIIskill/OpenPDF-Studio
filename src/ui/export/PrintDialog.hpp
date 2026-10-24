#pragma once

#include <QDialog>

#if defined(HAVE_QT_PRINT) && defined(HAVE_PDF_RENDERING)

#include "engine/export/PrintSheets.hpp"

#include <QPageSize>
#include <QTimer>

#include <optional>

QT_BEGIN_NAMESPACE
class QCheckBox;
class QComboBox;
class QFrame;
class QLabel;
class QLineEdit;
class QPrinter;
class QPushButton;
class QRadioButton;
class QSpinBox;
class QVBoxLayout;
QT_END_NAMESPACE

class DocumentView;
class PrintPreview;

/// Chooses printer, pages and layout for printing a document, with a preview of every sheet.
class PrintDialog : public QDialog
{
    Q_OBJECT

public:
    PrintDialog(QPrinter *printer, DocumentView *view, const DocumentExporter::Sources &pages,
                QWidget *parent = nullptr);

    PrintJob job() const;

private:
    void applyDialogStyle();
    QWidget *buildHeader();
    QWidget *buildSettings();
    QFrame  *buildPrinterSection();
    QFrame  *buildPagesSection();
    QFrame  *buildPaperSection();
    QFrame  *buildOutputSection();
    QFrame  *buildMoreSection();
    QWidget *buildFooter();

    void onPrinterChanged();
    void applyToPrinter();
    void chooseOrientation();
    void refresh();
    void updateSummary();
    void printWithSystemDialog();

    QList<int> selectedPages(bool *ok = nullptr) const;
    int  posterSheets() const;
    bool colorPrinter() const;

    QPrinter     *m_printer;
    DocumentView *m_view;
    DocumentExporter::Sources m_pages;
    int           m_pageCount   { 1 };
    int           m_currentPage { 0 };
    bool          m_orientationChosen { false };
    QList<QPageSize> m_pageSizes;
    std::optional<PrintJob> m_systemJob;
    QTimer        m_refreshTimer;

    PrintPreview *m_preview          { nullptr };
    QComboBox    *m_printerCombo     { nullptr };
    QLabel       *m_statusIcon       { nullptr };
    QLabel       *m_statusLabel      { nullptr };
    QLabel       *m_kindLabel        { nullptr };
    QRadioButton *m_allRadio         { nullptr };
    QRadioButton *m_currentRadio     { nullptr };
    QRadioButton *m_rangeRadio       { nullptr };
    QLineEdit    *m_rangeEdit        { nullptr };
    QSpinBox     *m_copiesSpin       { nullptr };
    QCheckBox    *m_collateChk       { nullptr };
    QComboBox    *m_paperCombo       { nullptr };
    QComboBox    *m_orientationCombo { nullptr };
    QComboBox    *m_scalingCombo     { nullptr };
    QComboBox    *m_perSheetCombo    { nullptr };
    QComboBox    *m_duplexCombo      { nullptr };
    QCheckBox    *m_grayChk          { nullptr };
    QWidget      *m_grayNote         { nullptr };
    QCheckBox    *m_cutMarksChk      { nullptr };
    QPushButton  *m_moreToggle       { nullptr };
    QWidget      *m_moreBody         { nullptr };
    QLabel       *m_summaryMain      { nullptr };
    QLabel       *m_summarySub       { nullptr };
    QPushButton  *m_printButton      { nullptr };
};

#endif
