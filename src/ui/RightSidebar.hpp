#pragma once

#include <QWidget>
#include <QList>

QT_BEGIN_NAMESPACE
class QToolButton;
QT_END_NAMESPACE

class RightSidebar : public QWidget
{
    Q_OBJECT

public:
    static constexpr int kWidth = 110;

    explicit RightSidebar(QWidget *parent = nullptr);

    void refreshTheme();
    void retranslateUi();
    void setMode(const QString &id);

Q_SIGNALS:
    void modeSelected(const QString &mode);

private:
    struct ModeData {
        QString      iconName;
        QToolButton *btn;
        QString      tipKey;
        bool         selected;
    };
    QList<ModeData> m_modes;
};
