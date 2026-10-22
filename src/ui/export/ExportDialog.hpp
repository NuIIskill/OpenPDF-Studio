#pragma once

#include <QDialog>
#include <QList>

QT_BEGIN_NAMESPACE
class QLineEdit;
class QComboBox;
class QCheckBox;
class QRadioButton;
class QLabel;
class QPushButton;
class QFrame;
QT_END_NAMESPACE

/// Stores the user's export selections.
struct ExportRequest {
    QString    path;
    QString    format;
    QList<int> pages;
    int        imageQuality    { 85 };
    bool       compressImages  { true };
    bool       includeComments { true };
    bool       keepForms       { true };
    bool       embedFonts      { true };
    bool       openAfterExport { false };
};

class ExportFormatList;

class ExportDialog : public QDialog
{
    Q_OBJECT

public:
    explicit ExportDialog(const QString &currentFile, int pageCount,
                          int currentPage = 0, QWidget *parent = nullptr);

    ExportRequest request() const;

    void selectFormatForTest(const QString &id);

    QString selectedPath()   const;
    QString selectedFormat() const;
    int selectedImageQuality() const;

private:
    void buildUi();

    void applyDialogStyle();
    QWidget *buildFormatList();
    QWidget *buildPage();
    QFrame *buildFileSection();
    QFrame *buildPagesSection();
    QFrame *buildSettingsSection();
    QWidget *buildFooter();

    void onFormatSelected(const QString &id);
    void updateOptionAvailability();
    void updateEstimate();

    QList<int> parseRange(bool *ok) const;
    QList<int> selectedPages() const;
    void onBrowse();
    void onExport();

    QString m_currentFile;
    int     m_pageCount     { 1 };
    int     m_currentPage   { 0 };
    qint64  m_sourceBytes   { 0 };
    QString m_selectedFormat{ QStringLiteral("pdf") };

    ExportFormatList *m_formats { nullptr };
    QLineEdit    *m_filenameEdit  { nullptr };
    QLineEdit    *m_locationEdit  { nullptr };
    QRadioButton *m_allRadio      { nullptr };
    QRadioButton *m_currentRadio  { nullptr };
    QRadioButton *m_rangeRadio    { nullptr };
    QLineEdit    *m_rangeEdit     { nullptr };
    QComboBox    *m_qualityCombo  { nullptr };
    QWidget      *m_compressRow   { nullptr };
    QWidget      *m_contentColumn { nullptr };
    QCheckBox    *m_compressChk   { nullptr };
    QCheckBox    *m_commentsChk   { nullptr };
    QCheckBox    *m_formsChk      { nullptr };
    QCheckBox    *m_fontsChk      { nullptr };
    QCheckBox    *m_openAfterChk  { nullptr };
    QLabel       *m_sizeLabel     { nullptr };
};
