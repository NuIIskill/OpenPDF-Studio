#pragma once

#include <QWidget>

QT_BEGIN_NAMESPACE
class QButtonGroup;
class QVBoxLayout;
QT_END_NAMESPACE

/// The export dialog's list of target formats, each with an icon, a name and what it is for.
class ExportFormatList : public QWidget
{
    Q_OBJECT

public:
    explicit ExportFormatList(QWidget *parent = nullptr);

    void addFormat(const QString &id, const QPixmap &icon, const QString &title,
                   const QString &subtitle, bool available);
    void select(const QString &id);

Q_SIGNALS:
    void formatChanged(const QString &id);

private:
    QButtonGroup *m_group  { nullptr };
    QVBoxLayout  *m_layout { nullptr };
};
