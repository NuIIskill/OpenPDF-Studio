#pragma once

#include <QWidget>
#include <QList>

class IconButton;

QT_BEGIN_NAMESPACE
class QLabel;
class QPushButton;
QT_END_NAMESPACE

class RightSidebar : public QWidget
{
    Q_OBJECT

public:
    explicit RightSidebar(QWidget *parent = nullptr);

    void refreshTheme();
    void retranslateUi();
    void setMode(const QString &id);

Q_SIGNALS:
    void modeSelected(const QString &mode);

private:
    void buildLayout();
    QWidget *makeModeButton(const QString &icon, const QString &label,
                            const QString &id, bool selected = false);
    void applyModeStyle(int i, bool selected);

    struct ModeData {
        QString      iconName;
        QLabel      *iconLabel;
        QLabel      *textLabel;
        QPushButton *btn;
        QString      tipKey;
        bool         selected;
    };
    QList<ModeData> m_modes;
};
