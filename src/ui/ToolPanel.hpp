#pragma once

#include <QWidget>

/// The content widget inside the RightSidebar.
///
/// Shows collapsible-style section headers (static for now) for
/// "Dokument", "Seite", and "Objekt" with placeholder content.
class ToolPanel : public QWidget
{
    Q_OBJECT

public:
    explicit ToolPanel(QWidget *parent = nullptr);

private:
    QWidget *makeSectionHeader(const QString &title);
    QWidget *makeSectionContent(const QString &placeholderText);
};
