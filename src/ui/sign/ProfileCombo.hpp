#pragma once

#include <QComboBox>

/// Signature profile picker: the profiles with their logos, then the commands to manage them.
class ProfileCombo : public QComboBox
{
    Q_OBJECT

public:
    enum class Command { New = 1, Edit, Duplicate, Delete };

    struct Entry {
        QString id;
        QString name;
        QImage  logo;
    };

    explicit ProfileCombo(QWidget *parent = nullptr);

    void setProfiles(const QList<Entry> &profiles, const QString &currentId, bool canDelete);
    QString currentId() const;

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

Q_SIGNALS:
    void profileChosen(const QString &id);
    void commandChosen(ProfileCombo::Command command);

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    void onActivated(int index);
    void addCommand(Command command, const QString &icon, const QString &text, bool enabled);

    int m_current { -1 };
};
