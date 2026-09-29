#pragma once

#include "app/SignatureStore.hpp"

#include <QFrame>
#include <QImage>

QT_BEGIN_NAMESPACE
class QLabel;
class QToolButton;
QT_END_NAMESPACE

/// One saved signature in the sign dialog's grid: preview, name, date and its menu.
class SignatureCard : public QFrame
{
    Q_OBJECT

public:
    explicit SignatureCard(const SignatureStore::Entry &entry, QWidget *parent = nullptr);

    const SignatureStore::Entry &entry() const { return m_entry; }
    void setSelected(bool selected);

Q_SIGNALS:
    void clicked();
    void renameRequested();
    void deleteRequested();

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    void updatePreview();
    void placeMenuButton();
    QString displayName() const;

    SignatureStore::Entry m_entry;
    QImage       m_image;
    QLabel      *m_preview  { nullptr };
    QLabel      *m_name     { nullptr };
    QToolButton *m_more     { nullptr };
    bool         m_selected { false };
};
