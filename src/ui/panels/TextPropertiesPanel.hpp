#pragma once

#include <QFrame>
#include <QList>
#include "engine/edit/TextBoxProperties.hpp"

QT_BEGIN_NAMESPACE
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QPushButton;
class QSlider;
class QWidget;
QT_END_NAMESPACE

class TextPropertiesPanel : public QFrame
{
    Q_OBJECT
public:
    static constexpr int kWidth = 300;
    explicit TextPropertiesPanel(QWidget *parent = nullptr);
    void retranslateUi();
    void setProperties(const TextBoxProperties &properties);
    void setEditorActive(bool active);
    TextBoxProperties defaultProperties() const { return m_defaults; }

Q_SIGNALS:
    void closeRequested();
    void propertiesChanged(const TextBoxProperties &properties);
    void defaultsChanged(const TextBoxProperties &properties);

private:
    void emitProperties();
    void updateConditionalControls();
    QPushButton *makeColorButton(const QColor &color);
    void chooseColor(QPushButton *button);
    void loadDefaults();
    void saveDefaults();

    QWidget *m_content { nullptr };
    QLabel *m_title { nullptr };
    QLabel *m_hint { nullptr };
    QDoubleSpinBox *m_x { nullptr }, *m_y { nullptr }, *m_w { nullptr }, *m_h { nullptr };
    QPushButton *m_autoHeight { nullptr };
    QDoubleSpinBox *m_padding { nullptr };
    QComboBox *m_verticalAlign { nullptr };
    QDoubleSpinBox *m_rotation { nullptr };
    QDoubleSpinBox *m_characterSpacing { nullptr }, *m_paragraphSpacing { nullptr };
    QSlider *m_opacity { nullptr };
    QLabel *m_opacityValue { nullptr };
    QDoubleSpinBox *m_cornerRadius { nullptr };
    QPushButton *m_borderToggle { nullptr };
    QComboBox *m_borderStyle { nullptr };
    QDoubleSpinBox *m_borderWidth { nullptr };
    QPushButton *m_borderColor { nullptr };
    QPushButton *m_backgroundToggle { nullptr }, *m_backgroundColor { nullptr };
    QList<QPushButton *> m_backgroundSwatches;
    QPushButton *m_saveDefault { nullptr };
    TextBoxProperties m_properties, m_defaults;
    bool m_syncing { false };
};
