#pragma once

#include <QScrollArea>

class PagePlaceholder;

/// The central canvas — shows one blank PDF page centred on a gray background.
class DocumentView : public QScrollArea
{
    Q_OBJECT

public:
    explicit DocumentView(QWidget *parent = nullptr);

    void setZoom(int percent);

private:
    void updatePageSize();

    PagePlaceholder *m_page    { nullptr };
    QWidget         *m_canvas  { nullptr };
    int              m_zoom    { 100 };
};
