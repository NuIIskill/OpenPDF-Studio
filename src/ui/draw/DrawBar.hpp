#pragma once

#include "ui/draw/DrawTool.hpp"

#include <QColor>
#include <QFrame>
#include <QIcon>
#include <QList>

QT_BEGIN_NAMESPACE
class QButtonGroup;
class QPushButton;
QT_END_NAMESPACE

/// Top bar for freehand drawing tools and properties.
class DrawBar : public QFrame
{
    Q_OBJECT

public:
    explicit DrawBar(QWidget *parent = nullptr);

    void refreshTheme();
    void retranslateUi();
    DrawTool currentTool() const { return m_currentTool; }
    QColor currentColor() const { return m_currentColor; }
    qreal currentWidth() const { return m_currentWidth; }

Q_SIGNALS:
    void toolChanged(DrawTool tool);
    void widthChanged(qreal widthPt);
    void colorChanged(const QColor &color);

protected:
    void changeEvent(QEvent *event) override;

private:
    static QFrame *makeSeparator(QWidget *parent);
    static QIcon makeDotIcon(qreal diameter, const QColor &color);
    static QIcon makeColorIcon(const QColor &color, bool dark);
    QPushButton *makeToolButton(const QString &icon, QWidget *parent);
    void selectColor(QPushButton *button, const QColor &color);

    QPushButton *m_pen { nullptr };
    QPushButton *m_highlighter { nullptr };
    QPushButton *m_eraser { nullptr };
    QList<QPushButton *> m_widthButtons;
    QList<QPushButton *> m_colorButtons;
    DrawTool m_currentTool { DrawTool::Pen };
    QColor m_currentColor { QStringLiteral("#145DFF") };
    qreal m_currentWidth { 3.0 };
};
