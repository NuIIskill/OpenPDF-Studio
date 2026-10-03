#pragma once

#include <QComboBox>

/// Certificate picker that shows each certificate's name with its distinguished name below.
class CertificateCombo : public QComboBox
{
    Q_OBJECT

public:
    static constexpr int DetailRole = Qt::UserRole + 1;

    explicit CertificateCombo(QWidget *parent = nullptr);

    void addNewEntry(const QString &text);
    bool isNewEntry(int index) const;

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

Q_SIGNALS:
    void menuRequested(int index, const QPoint &globalPos);

protected:
    void paintEvent(QPaintEvent *event) override;
};
