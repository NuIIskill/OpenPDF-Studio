#pragma once

#include <QDialog>

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

class ExportDialog : public QDialog
{
    Q_OBJECT

public:
    explicit ExportDialog(const QString &currentFile, int pageCount,
                          QWidget *parent = nullptr);

    QString selectedPath()   const;
    QString selectedFormat() const;

private:
    void buildUi();
    QWidget *makeSectionHeader(const QString &num, const QString &title);
    QPushButton *makeFormatCard(const QString &iconChar, const QString &label,
                                const QString &id, bool available);
    void onFormatSelected(const QString &id);
    void updatePasswordFields();
    void onBrowse();
    void onExport();

    QString m_currentFile;
    int     m_pageCount     { 1 };
    QString m_selectedFormat{ QStringLiteral("pdf") };

    QButtonGroup *m_formatGroup   { nullptr };
    QLineEdit    *m_filenameEdit  { nullptr };
    QLineEdit    *m_locationEdit  { nullptr };
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
