#include "TextAnnotation.hpp"

#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QTextEdit>
#include <QMouseEvent>

TextAnnotation::TextAnnotation(QWidget *parent)
    : QFrame(parent)
{
    setObjectName(QStringLiteral("TextAnnotation"));
    setFixedSize(210, 110);
    setStyleSheet(QStringLiteral(
        "TextAnnotation { background:#FEFCE8; border:1px solid #EAB308; border-radius:4px; }"));

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    // Drag bar
    auto *bar = new QWidget(this);
    bar->setFixedHeight(BAR_H);
    bar->setStyleSheet(QStringLiteral("background:#FDE047; border-radius:3px 3px 0 0;"));
    auto *barLayout = new QHBoxLayout(bar);
    barLayout->setContentsMargins(6, 0, 4, 0);
    barLayout->setSpacing(0);

    auto *titleLbl = new QLabel(tr("Text"), bar);
    titleLbl->setStyleSheet(QStringLiteral("font-size:11px; font-weight:600; color:#713F12;"));
    titleLbl->setAttribute(Qt::WA_TransparentForMouseEvents);
    barLayout->addWidget(titleLbl);
    barLayout->addStretch();

    auto *closeBtn = new QPushButton(QStringLiteral("✕"), bar);
    closeBtn->setFixedSize(16, 16);
    closeBtn->setFlat(true);
    closeBtn->setStyleSheet(QStringLiteral(
        "QPushButton { font-size:10px; color:#713F12; border:none; background:transparent; }"
        "QPushButton:hover { color:#000; }"));
    closeBtn->setCursor(Qt::PointingHandCursor);
    connect(closeBtn, &QPushButton::clicked, this, &TextAnnotation::deleteRequested);
    barLayout->addWidget(closeBtn);

    root->addWidget(bar);

    // Text area
    m_edit = new QTextEdit(this);
    m_edit->setPlaceholderText(tr("Enter text…"));
    m_edit->setStyleSheet(QStringLiteral(
        "QTextEdit { background:transparent; border:none; font-size:12px; }"));
    root->addWidget(m_edit, 1);
}

void TextAnnotation::mousePressEvent(QMouseEvent *e)
{
    if (e->button() == Qt::LeftButton && e->pos().y() <= BAR_H) {
        m_posBeforeDrag = pos();
        m_dragOff       = e->pos();
        m_dragging      = true;
        e->accept();
        return;
    }
    QFrame::mousePressEvent(e);
}

void TextAnnotation::mouseMoveEvent(QMouseEvent *e)
{
    if (m_dragging) {
        move(mapToParent(e->pos()) - m_dragOff);
        e->accept();
        return;
    }
    QFrame::mouseMoveEvent(e);
}

void TextAnnotation::mouseReleaseEvent(QMouseEvent *e)
{
    if (m_dragging && e->button() == Qt::LeftButton) {
        m_dragging = false;
        if (pos() != m_posBeforeDrag)
            Q_EMIT moved(m_posBeforeDrag, pos());
        e->accept();
        return;
    }
    QFrame::mouseReleaseEvent(e);
}
