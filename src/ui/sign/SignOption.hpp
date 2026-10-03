#pragma once

#include <QAbstractButton>

/// A checkbox with a title and a short description below it.
class SignOption : public QAbstractButton
{
    Q_OBJECT

public:
    SignOption(const QString &title, const QString &description, QWidget *parent = nullptr);

    QSize sizeHint() const override;
    bool  hasHeightForWidth() const override { return true; }
    int   heightForWidth(int width) const override;

protected:
    void paintEvent(QPaintEvent *event) override;
    void changeEvent(QEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    QFont descriptionFont() const;
    QRect descriptionRect(int width) const;

    QString m_description;
};
