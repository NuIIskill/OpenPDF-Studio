#pragma once

#include <QList>
#include <QStringList>
#include <QWidget>

QT_BEGIN_NAMESPACE
class QButtonGroup;
class QLabel;
class QVBoxLayout;
QT_END_NAMESPACE

/// The export dialog's list of target formats in collapsible groups, each with an icon, a name and what it is for.
class ExportFormatList : public QWidget
{
    Q_OBJECT

public:
    explicit ExportFormatList(QWidget *parent = nullptr);

    void addGroup(const QString &id, const QString &title, bool expanded);
    void addFormat(const QString &id, const QPixmap &icon, const QString &title,
                   const QString &subtitle, bool available);
    void select(const QString &id);
    QStringList expandedGroups() const;

Q_SIGNALS:
    void formatChanged(const QString &id);

private:
    struct Group {
        QString      id;
        QWidget     *items   { nullptr };
        QVBoxLayout *layout  { nullptr };
        QLabel      *chevron { nullptr };
        QLabel      *formats { nullptr };
        QStringList  names;
    };

    void setExpanded(Group &group, bool expanded);

    QButtonGroup *m_group  { nullptr };
    QVBoxLayout  *m_layout { nullptr };
    QList<Group>  m_groups;
};
