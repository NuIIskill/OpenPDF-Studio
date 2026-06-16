#pragma once
#include <QFrame>

QT_BEGIN_NAMESPACE
class QLabel;
class QComboBox;
class QSlider;
QT_END_NAMESPACE

class TextPropertiesPanel : public QFrame
{
    Q_OBJECT
public:
    static constexpr int kWidth = 260;

    explicit TextPropertiesPanel(QWidget *parent = nullptr);
    void retranslateUi();

Q_SIGNALS:
    void closeRequested();

private:
    QLabel    *m_title      { nullptr };
    QLabel    *m_lSpacing   { nullptr };
    QLabel    *m_lOpacity   { nullptr };
    QLabel    *m_opacityVal { nullptr };
    QLabel    *m_lBorder    { nullptr };
    QLabel    *m_lBg        { nullptr };
    QComboBox *m_spacing    { nullptr };
    QSlider   *m_opacity    { nullptr };
};
