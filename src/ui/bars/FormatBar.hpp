#pragma once
#include <QColor>
#include <QEvent>
#include <QFrame>
#include <QIcon>

QT_BEGIN_NAMESPACE
class QComboBox;
class QLabel;
class QPushButton;
class QFontComboBox;
QT_END_NAMESPACE

class FormatBar : public QFrame
{
    Q_OBJECT
public:
    explicit FormatBar(QWidget *parent = nullptr);
    void retranslateUi();
    void setFontSize(int ptSize);
    void setTextColor(const QColor &c);
    // Programmatic sync from the active editor — no signals re-emitted.
    void setFontFamily(const QString &family);
    void setBoldChecked(bool on);
    void setItalicChecked(bool on);

Q_SIGNALS:
    void fontFamilyChanged(const QString &family);
    void fontSizeChanged(int ptSize);
    void textColorChanged(const QColor &color);
    void boldToggled(bool on);
    void italicToggled(bool on);
    void underlineToggled(bool on);
    void alignmentChanged(Qt::Alignment align);

protected:
    void changeEvent(QEvent *e) override;

private:
    static QFrame      *makeSep(QWidget *parent);
    static QPushButton *makeFmtBtn(const QIcon &icon, QWidget *parent);
    static QPushButton *makeAlignBtn(const QString &iconName, const QString &fallback,
                                     const QString &tip, QWidget *parent);
    void updateColorSwatch(const QColor &c);

    QFontComboBox *m_fontFamily { nullptr };
    QComboBox     *m_fontSize   { nullptr };
    QPushButton   *m_bold       { nullptr };
    QPushButton   *m_italic     { nullptr };
    QPushButton   *m_underline  { nullptr };
    QPushButton   *m_color      { nullptr };
    QComboBox     *m_spacing    { nullptr };

    // Group labels — updated on LanguageChange via retranslateUi()
    QLabel *m_lblFont    { nullptr };
    QLabel *m_lblSize    { nullptr };
    QLabel *m_lblColor   { nullptr };
    QLabel *m_lblAlign   { nullptr };
    QLabel *m_lblList    { nullptr };
    QLabel *m_lblSpacing { nullptr };

    QColor m_currentColor  { Qt::black };
    int    m_lastFontSize  { 14 };
};
