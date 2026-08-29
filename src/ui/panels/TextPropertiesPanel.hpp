#pragma once

#include <QFrame>
#include "engine/edit/TextBoxProperties.hpp"

QT_BEGIN_NAMESPACE
class QDoubleSpinBox;
class QLabel;
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
    void propertiesChanged(const TextBoxProperties &properties);

private:
    void emitProperties();
    void updateConditionalControls();
    void loadDefaults();

    QWidget *m_content { nullptr };
    QLabel *m_title { nullptr };
    QLabel *m_hint { nullptr };
    QDoubleSpinBox *m_x { nullptr }, *m_y { nullptr }, *m_w { nullptr }, *m_h { nullptr };
    QDoubleSpinBox *m_rotation { nullptr };
    QSlider *m_opacity { nullptr };
    QLabel *m_opacityValue { nullptr };
    TextBoxProperties m_properties, m_defaults;
    bool m_syncing { false };
};
