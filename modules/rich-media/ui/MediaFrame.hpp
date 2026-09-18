// SPDX-License-Identifier: LicenseRef-OpenPDF-Business
#pragma once

#include <QImage>
#include <QWidget>

/// What sits over a medium on the page.
class MediaFrame : public QWidget
{
    Q_OBJECT

public:

    enum class Mode { Existing, Placement, Removed };

    explicit MediaFrame(Mode mode, QWidget *parent = nullptr);

    void setPoster(const QImage &poster);
    void setCaption(const QString &caption);

    void setInteractive(bool interactive);
    void setSelected(bool selected);
    bool isSelected() const { return m_selected; }
    void setMode(Mode mode);

    void setPageRect(const QRect &rect) { m_pageRect = rect; }
    Mode mode() const { return m_mode; }

Q_SIGNALS:
    void activated();
    void selected();

    void contextMenuRequested(const QPoint &globalPos);
    void geometryEdited(const QRect &geometry);
    void geometryEditFinished(const QRect &geometry);
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
    Handle  m_active  { Handle::None };
    QPoint  m_pressPos;
    QRect   m_geometryAtPress;
    QRect   m_pageRect;

    static constexpr int kHandle = 9;
    static constexpr int kMinSize = 24;
};
