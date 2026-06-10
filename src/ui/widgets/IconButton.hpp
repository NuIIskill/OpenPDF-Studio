#pragma once

#include <QPushButton>
#include <QString>

/// A compact square icon button used throughout the toolbar and sidebar.
///
/// Fixed 36×36 px, rounded corners (8 px).
/// Transparent background with a subtle primary-blue hover tint.
/// Pass `label` as a short text glyph or Unicode symbol; real SVG
/// loading can be layered on later via setIconName().
class IconButton : public QPushButton
{
    Q_OBJECT
    Q_PROPERTY(QString iconName READ iconName WRITE setIconName)

public:
    explicit IconButton(const QString &label, QWidget *parent = nullptr);
    explicit IconButton(QWidget *parent = nullptr);

    [[nodiscard]] QString iconName() const { return m_iconName; }
    void setIconName(const QString &name);

    // Convenience: mark this button as a toggle (checkable).
    void setToggle(bool on);

protected:
    void enterEvent(QEnterEvent *event) override;
    void leaveEvent(QEvent *event) override;

private:
    void init();

    QString m_iconName;
};
