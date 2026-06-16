#pragma once

#include <QPushButton>
#include <QColor>
#include <QIcon>

/// Compact square icon button (36×36 px) used throughout the UI.
///
/// Pass a Lucide icon name via setIconName() — it renders the SVG at the
/// correct color for normal, checked, and disabled states automatically.
/// Falls back to a text label if no icon name is set.
class IconButton : public QPushButton
{
    Q_OBJECT

public:
    explicit IconButton(const QString &label, QWidget *parent = nullptr);
    explicit IconButton(QWidget *parent = nullptr);

    /// Load a Lucide SVG icon from :/icons/<name>.svg.
    /// Pass QColor{} (default) to use Theme::IconNormal automatically.
    void setIconName(const QString &name,
                     const QColor  &normalColor = QColor{});

    [[nodiscard]] QString iconName() const { return m_iconName; }

    void setToggle(bool on);

protected:
    void enterEvent(QEnterEvent *event) override;
    void leaveEvent(QEvent *event) override;

private:
    void init();
    void onToggled(bool checked);

    QString m_iconName;
    QColor  m_normalColor;
    QColor  m_hoverColor  { "#111827" };
    QIcon   m_normalIcon;
    QIcon   m_hoverIcon;
};
