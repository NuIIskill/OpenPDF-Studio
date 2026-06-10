#pragma once

#include <QWidget>

class ToolPanel;

/// The light right-side properties panel.
///
/// Fixed width: 260 px.
/// White background with a left border.
/// Contains a "Eigenschaften" header and a ToolPanel.
class RightSidebar : public QWidget
{
    Q_OBJECT

public:
    explicit RightSidebar(QWidget *parent = nullptr);

    [[nodiscard]] ToolPanel *toolPanel() const { return m_toolPanel; }

private:
    ToolPanel *m_toolPanel { nullptr };
};
