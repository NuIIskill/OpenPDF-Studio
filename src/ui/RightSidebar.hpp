#pragma once

#include <QWidget>
#include <QList>

class IconButton;

QT_BEGIN_NAMESPACE
class QLabel;
QT_END_NAMESPACE

class RightSidebar : public QWidget
{
    Q_OBJECT

public:
    explicit RightSidebar(QWidget *parent = nullptr);

    void refreshTheme();
    void retranslateUi();

Q_SIGNALS:
    void modeSelected(const QString &mode);

private:
    void buildLayout();
    QWidget *makeModeButton(const QString &icon, const QString &label,
                            const QString &id, bool selected = false);

    struct ModeData {
        QString  iconName;
        QLabel  *iconLabel;
        QLabel  *textLabel;
        QString  tipKey;
        bool     selected;
    };
    QList<ModeData> m_modes;
};
