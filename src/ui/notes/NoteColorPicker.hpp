#pragma once

#include <QColor>
#include <QFrame>

class QLineEdit;
class QPushButton;

/// A small popup for mixing a note color: a saturation and brightness field, a hue strip and a hex field.
class NoteColorPicker : public QFrame
{
    Q_OBJECT

public:
    NoteColorPicker(const QColor &start, QWidget *parent);

    void showBelow(QWidget *anchor);
    QColor color() const { return m_typed.isValid() ? m_typed : QColor::fromHsv(m_hue, m_saturation, m_value); }

Q_SIGNALS:
    void chosen(const QColor &color);
    void changed(const QColor &color);

protected:
    void keyPressEvent(QKeyEvent *event) override;

private:
    void setHsv(int hue, int saturation, int value, bool fromHex = false);

    int          m_hue { 0 };
    int          m_saturation { 0 };
    int          m_value { 0 };
    QColor       m_typed;
    QWidget     *m_field { nullptr };
    QWidget     *m_strip { nullptr };
    QWidget     *m_preview { nullptr };
    QLineEdit   *m_hex { nullptr };
    QPushButton *m_apply { nullptr };
};
