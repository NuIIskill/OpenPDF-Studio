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
class QAbstractButton;
class QButtonGroup;
QT_END_NAMESPACE

// Everything the dialog collects, in one place, so the caller does not have to
// reach into individual widgets to find out what was asked for.
struct ExportRequest {
    QString    path;
    QString    format;                    // "pdf" | "word" | "image"
    QList<int> pages;                     // zero-based, in order; never empty
    int        imageQuality    { 85 };
    bool       compressImages  { true };
    bool       includeComments { true };
    bool       keepForms       { true };
    bool       embedFonts      { true };
    bool       openAfterExport { false };
    QString    password;                  // empty = no protection
};

class ExportDialog : public QDialog
{
    Q_OBJECT

public:
    explicit ExportDialog(const QString &currentFile, int pageCount,
                          int currentPage = 0, QWidget *parent = nullptr);

    ExportRequest request() const;

    // Test hook: drives the format cards from outside so the dialog's
    // appearance per format can be captured without a display.
    void selectFormatForTest(const QString &id);

    QString selectedPath()   const;
    QString selectedFormat() const;
    int selectedImageQuality() const;

private:
    void buildUi();
    QWidget *makeSectionHeader(const QString &num, const QString &title);
    QPushButton *makeFormatCard(const QString &iconChar, const QString &label,
                                const QString &id, bool available);
    void onFormatSelected(const QString &id);
    void updatePasswordFields();
    void updateOptionAvailability();
    void updateEstimate();
    // Parses the range field ("1-3, 5, 8-10"). ok is false on malformed input
    // or on a page number outside the document.
    QList<int> parseRange(bool *ok) const;
    QList<int> selectedPages() const;
    void onBrowse();
    void onExport();

    QString m_currentFile;
    int     m_pageCount     { 1 };
    int     m_currentPage   { 0 };
    qint64  m_sourceBytes   { 0 };
    QString m_selectedFormat{ QStringLiteral("pdf") };

    QButtonGroup *m_formatGroup   { nullptr };
    QLineEdit    *m_filenameEdit  { nullptr };
    QLineEdit    *m_locationEdit  { nullptr };
    QRadioButton *m_allRadio      { nullptr };
    QRadioButton *m_currentRadio  { nullptr };
    QRadioButton *m_rangeRadio    { nullptr };
    QLineEdit    *m_rangeEdit     { nullptr };
    QComboBox    *m_qualityCombo  { nullptr };
    QCheckBox    *m_compressChk   { nullptr };
    QCheckBox    *m_commentsChk   { nullptr };
    QCheckBox    *m_formsChk      { nullptr };
    QCheckBox    *m_fontsChk      { nullptr };
    QCheckBox    *m_openAfterChk  { nullptr };
    QCheckBox    *m_passwordChk   { nullptr };
    QLineEdit    *m_passEdit      { nullptr };
    QLineEdit    *m_passConfirm   { nullptr };
    QLabel       *m_sizeLabel     { nullptr };
};
