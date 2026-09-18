#include "ui/draw/DrawBar.hpp"

#include "ui/theme/Theme.hpp"

#include <QButtonGroup>
#include <QEvent>
#include <QHBoxLayout>
#include <QPainter>
#include <QPushButton>
#include <QStringList>

DrawBar::DrawBar(QWidget *parent) : QFrame(parent)
{
    setObjectName(QStringLiteral("DrawBar"));
    setFixedHeight(60);

    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(16, 7, 16, 7);
    layout->setSpacing(8);
    layout->addStretch(1);

    auto *tools = new QButtonGroup(this);
    tools->setExclusive(true);
    m_pen = makeToolButton(QStringLiteral("pencil"), this);
    m_highlighter = makeToolButton(QStringLiteral("highlighter"), this);
    m_eraser = makeToolButton(QStringLiteral("eraser"), this);
    tools->addButton(m_pen);
    tools->addButton(m_highlighter);
    tools->addButton(m_eraser);
    m_pen->setChecked(true);
    layout->addWidget(m_pen);
    layout->addWidget(m_highlighter);
    layout->addWidget(m_eraser);
    connect(m_pen, &QPushButton::clicked, this,
            [this]() {
        m_currentTool = DrawTool::Pen;
        Q_EMIT toolChanged(m_currentTool);
    });
    connect(m_highlighter, &QPushButton::clicked, this,
            [this]() {
        m_currentTool = DrawTool::Highlighter;
        Q_EMIT toolChanged(m_currentTool);
    });
    connect(m_eraser, &QPushButton::clicked, this,
            [this]() {
        m_currentTool = DrawTool::Eraser;
        Q_EMIT toolChanged(m_currentTool);
    });

    layout->addSpacing(5);
    layout->addWidget(makeSeparator(this));
    layout->addSpacing(5);

    auto *widths = new QButtonGroup(this);
    widths->setExclusive(true);
    const QList<qreal> widthValues { 1.5, 3.0, 6.0 };
    for (int i = 0; i < widthValues.size(); ++i) {
        auto *button = new QPushButton(this);
        button->setProperty("drawChoice", true);
        button->setCheckable(true);
        button->setFixedSize(38, 38);
        button->setIconSize(QSize(20, 20));
        widths->addButton(button);
        m_widthButtons.append(button);
        layout->addWidget(button);
        const qreal width = widthValues.at(i);
        connect(button, &QPushButton::clicked, this,
                [this, width]() {
            m_currentWidth = width;
            Q_EMIT widthChanged(width);
        });
    }
    m_widthButtons.at(1)->setChecked(true);

    layout->addSpacing(5);
    layout->addWidget(makeSeparator(this));
    layout->addSpacing(5);

    auto *colors = new QButtonGroup(this);
    colors->setExclusive(true);
    const QList<QColor> colorValues {
        QColor(QStringLiteral("#145DFF")),
        QColor(QStringLiteral("#FFD426")),
        QColor(QStringLiteral("#F31212")),
        QColor(QStringLiteral("#111111"))
    };
    for (const QColor &color : colorValues) {
        auto *button = new QPushButton(this);
        button->setProperty("drawColor", true);
        button->setCheckable(true);
        button->setFixedSize(38, 38);
        button->setIconSize(QSize(26, 26));
        colors->addButton(button);
        m_colorButtons.append(button);
        layout->addWidget(button);
        connect(button, &QPushButton::clicked, this,
                [this, button, color]() { selectColor(button, color); });
    }
    m_colorButtons.first()->setChecked(true);

    layout->addStretch(1);
    retranslateUi();
    refreshTheme();
}

QFrame *DrawBar::makeSeparator(QWidget *parent)
{
    auto *separator = new QFrame(parent);
    separator->setObjectName(QStringLiteral("DrawBarSeparator"));
    separator->setFrameShape(QFrame::VLine);
    separator->setFixedSize(1, 32);
    return separator;
}

QIcon DrawBar::makeDotIcon(qreal diameter, const QColor &color)
{
    QPixmap pixmap(20, 20);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen);
    painter.setBrush(color);
    const QRectF dot((20.0 - diameter) / 2.0, (20.0 - diameter) / 2.0,
                     diameter, diameter);
    painter.drawEllipse(dot);
    return QIcon(pixmap);
}

QIcon DrawBar::makeColorIcon(const QColor &color, bool dark)
{
    QPixmap pixmap(26, 26);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(QPen(dark ? QColor(QStringLiteral("#707070"))
                             : QColor(QStringLiteral("#D1D5DB")), 1));
    painter.setBrush(color);
    painter.drawEllipse(QRectF(3.5, 3.5, 19, 19));
    return QIcon(pixmap);
}

QPushButton *DrawBar::makeToolButton(const QString &icon, QWidget *parent)
{
    auto *button = new QPushButton(parent);
    button->setProperty("drawTool", true);
    button->setProperty("iconName", icon);
    button->setCheckable(true);
    button->setCursor(Qt::PointingHandCursor);
    button->setFixedSize(46, 42);
    button->setIconSize(QSize(22, 22));
    return button;
}

void DrawBar::selectColor(QPushButton *button, const QColor &color)
{
    if (!button) return;
    button->setChecked(true);
    m_currentColor = color;
    Q_EMIT colorChanged(color);
}

void DrawBar::refreshTheme()
{
    const bool dark = palette().color(QPalette::Window).lightness() < 128;
    const QColor icon = dark ? QColor(QStringLiteral("#D8D8D8"))
                             : QColor(QStringLiteral("#374151"));
    for (QPushButton *button : { m_pen, m_highlighter, m_eraser }) {
        const QString name = button->property("iconName").toString();
        button->setIcon(Theme::makeIcon(name, icon, Theme::Primary,
                                        Theme::IconDisabled, 22));
    }
    const QList<qreal> diameters { 4.0, 7.0, 12.0 };
    for (int i = 0; i < m_widthButtons.size(); ++i)
        m_widthButtons.at(i)->setIcon(makeDotIcon(diameters.at(i), icon));
    const QList<QColor> colors {
        QColor(QStringLiteral("#145DFF")), QColor(QStringLiteral("#FFD426")),
        QColor(QStringLiteral("#F31212")), QColor(QStringLiteral("#111111"))
    };
    for (int i = 0; i < m_colorButtons.size(); ++i)
        m_colorButtons.at(i)->setIcon(makeColorIcon(colors.at(i), dark));
}

void DrawBar::retranslateUi()
{
    m_pen->setToolTip(tr("Pen"));
    m_highlighter->setToolTip(tr("Highlighter"));
    m_eraser->setToolTip(tr("Eraser"));
    const QStringList widthTips { tr("Thin"), tr("Medium"), tr("Thick") };
    for (int i = 0; i < m_widthButtons.size(); ++i)
        m_widthButtons.at(i)->setToolTip(widthTips.at(i));
}

void DrawBar::changeEvent(QEvent *event)
{
    QFrame::changeEvent(event);
    if (event->type() == QEvent::LanguageChange) retranslateUi();
}
