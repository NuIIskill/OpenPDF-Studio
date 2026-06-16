#pragma once
#include <QFrame>

QT_BEGIN_NAMESPACE
class QComboBox;
class QPushButton;
class QFontComboBox;
QT_END_NAMESPACE

class FormatBar : public QFrame
{
    Q_OBJECT
public:
    explicit FormatBar(QWidget *parent = nullptr);
    void retranslateUi();

private:
    static QFrame      *makeSep(QWidget *parent);
    static QPushButton *makeFmtBtn(const QString &text, QWidget *parent,
                                   bool bold = false, bool italic = false,
                                   bool underline = false);
    static QPushButton *makeAlignBtn(const QString &text, QWidget *parent);

    QFontComboBox *m_fontFamily { nullptr };
    QComboBox     *m_fontSize   { nullptr };
    QPushButton   *m_bold       { nullptr };
    QPushButton   *m_italic     { nullptr };
    QPushButton   *m_underline  { nullptr };
    QPushButton   *m_color      { nullptr };
    QComboBox     *m_spacing    { nullptr };
};
