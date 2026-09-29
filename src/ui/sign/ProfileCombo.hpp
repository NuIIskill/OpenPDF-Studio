#pragma once

#include <QComboBox>

/// Appearance profile picker showing each profile's logo beside its name.
class ProfileCombo : public QComboBox
{
    Q_OBJECT

public:
    static constexpr int IdRole   = Qt::UserRole;
    static constexpr int LogoRole = Qt::UserRole + 1;

    explicit ProfileCombo(QWidget *parent = nullptr);

    void addProfile(const QString &id, const QString &name, const QImage &logo);
    void addNewEntry(const QString &text);
    bool isNewEntry(int index) const;

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;
};
