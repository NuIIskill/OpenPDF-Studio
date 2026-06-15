#pragma once

#include <QDialog>
#include <QMap>

QT_BEGIN_NAMESPACE
class QListWidget;
QT_END_NAMESPACE

#ifdef HAVE_QT_PDF
#include <QPdfDocument>
#endif

class PdfOrganizerDialog : public QDialog
{
    Q_OBJECT

public:
    explicit PdfOrganizerDialog(const QString &initialPath = {}, QWidget *parent = nullptr);
    ~PdfOrganizerDialog() override;

    void retranslateUi();

protected:
    void changeEvent(QEvent *e) override;

private:
    void buildUi();
    void addPdfPages(const QString &path);
    void addBlankPage();
    void removeSelected();
    void saveAs();
    void updatePageNumbers();

    static constexpr int THUMB_W   = 120;
    static constexpr int THUMB_H   = 155;
    static constexpr int GRID_W    = 148;
    static constexpr int GRID_H    = 198;
    static constexpr int RENDER_DPI = 150;

    QListWidget *m_list { nullptr };

#ifdef HAVE_QT_PDF
    QMap<QString, QPdfDocument *> m_docs;
#endif
};
