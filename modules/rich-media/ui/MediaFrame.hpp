// SPDX-License-Identifier: LicenseRef-OpenPDF-Business
#pragma once

#include <QColor>
#include <QImage>
#include <QWidget>

/// What sits over a medium on the page. Works in widget pixels; MediaLayer
/// holds the PDF points.
class MediaFrame : public QWidget
{
    Q_OBJECT

public:
    /// Existing   in the document or waiting to be saved; click plays.
    /// Placement  being dragged out: dashed, eight handles.
    /// Removed    marked for removal, covering the spot until the save.
    enum class Mode { Existing, Placement, Removed };

    explicit MediaFrame(Mode mode, QWidget *parent = nullptr);

    void setPoster(const QImage &poster);
    void setCaption(const QString &caption);
    /// With the media tool active the frame can be selected and deleted;
    /// otherwise it is a play button.
    void setInteractive(bool interactive);
    void setSelected(bool selected);
    bool isSelected() const { return m_selected; }
    /// Removed mode: the colour the spot is covered with.
    void setCoverColor(const QColor &color);
    void setMode(Mode mode);
    /// Confines move and resize to the page (parent coordinates).
    void setPageRect(const QRect &rect) { m_pageRect = rect; }
    Mode mode() const { return m_mode; }

Q_SIGNALS:
    void activated();
    /// The caller builds the menu; only it knows what this medium allows.
    void contextMenuRequested(const QPoint &globalPos);
    void geometryEdited(const QRect &geometry);
    void deleteRequested();

protected:
    void paintEvent(QPaintEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void enterEvent(QEnterEvent *event) override;
    void leaveEvent(QEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

private:
    /// The eight handles clockwise from top left, plus "none".
    enum class Handle { None, TopLeft, Top, TopRight, Right,
                        BottomRight, Bottom, BottomLeft, Left, Body };

    Handle handleAt(const QPoint &pos) const;
    QRect  handleRect(Handle handle) const;
    void   applyCursor(Handle handle);
    QRect  playButtonRect() const;

    Mode    m_mode;
    QImage  m_poster;
    QString m_caption;

    bool    m_hovered     { false };
    bool    m_interactive { false };
    bool    m_selected    { false };
    QColor  m_coverColor  { Qt::white };
    Handle  m_active  { Handle::None };
    QPoint  m_pressPos;
    QRect   m_geometryAtPress;
    QRect   m_pageRect;

    static constexpr int kHandle = 9;   // handle edge length
    static constexpr int kMinSize = 24;
};
