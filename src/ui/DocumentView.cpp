#include "DocumentView.hpp"

#include <QWidget>

DocumentView::DocumentView(QWidget *parent)
    : QScrollArea(parent)
{
    setObjectName(QStringLiteral("DocumentView"));
    setFrameShape(QFrame::NoFrame);
    setWidgetResizable(true);
    setAlignment(Qt::AlignCenter);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);

    m_container = new QWidget(this);
    m_container->setObjectName(QStringLiteral("DocumentContainer"));
    m_container->setStyleSheet(QStringLiteral(
        "#DocumentContainer { background: #F1F5F9; }"));
    setWidget(m_container);
}
