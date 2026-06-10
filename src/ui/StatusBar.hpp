#pragma once

#include <QWidget>

QT_BEGIN_NAMESPACE
class QLabel;
QT_END_NAMESPACE

/// Slim status bar at the bottom of the main window.
///
/// Fixed height: 28 px.
/// Shows: [page indicator] [stretch] [zoom level] [file size placeholder]
class StatusBar : public QWidget
{
    Q_OBJECT

public:
    explicit StatusBar(QWidget *parent = nullptr);

    void setCurrentPage(int page, int total);
    void setZoom(int percent);
    void setFileSize(const QString &text);

private:
    QLabel *m_pageLabel    { nullptr };
    QLabel *m_zoomLabel    { nullptr };
    QLabel *m_fileSizeLabel{ nullptr };
};
