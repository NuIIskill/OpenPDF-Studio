#pragma once

#include <QScrollArea>

/// The central canvas area that will display PDF pages.
///
/// Inherits QScrollArea. Background: #F1F5F9.
/// Currently empty — content is added once PDF loading is implemented.
class DocumentView : public QScrollArea
{
    Q_OBJECT

public:
    explicit DocumentView(QWidget *parent = nullptr);

private:
    QWidget *m_container { nullptr };
};
