#pragma once

#include <QFrame>

#if defined(HAVE_QT_PRINT) && defined(HAVE_PDF_RENDERING)

#include "engine/export/PrintSheets.hpp"

#include <QPageLayout>

#include <optional>

QT_BEGIN_NAMESPACE
class QLabel;
class QPushButton;
QT_END_NAMESPACE

class SheetCanvas;

/// Shows the sheets of a print job one at a time, as the printer will receive them.
class PrintPreview : public QFrame
{
    Q_OBJECT

public:
    explicit PrintPreview(QWidget *parent = nullptr);

    void setSheets(const PrintSheets &sheets, const QPageLayout &layout, bool countSheets);

private:
    void showSheet(int sheet);
    void render();

    std::optional<PrintSheets> m_sheets;
    QPageLayout  m_layout;
    bool         m_countSheets     { false };
    int          m_sheet           { 0 };

    SheetCanvas *m_canvas    { nullptr };
    QPushButton *m_prev      { nullptr };
    QPushButton *m_next      { nullptr };
    QLabel      *m_position  { nullptr };
    QLabel      *m_paper     { nullptr };
};

#endif
