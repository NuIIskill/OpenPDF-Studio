#pragma once

#include <QList>
#include <QObject>
#include <QPointer>
#include <QTimer>

QT_BEGIN_NAMESPACE
class QAction;
class QLabel;
class QLineEdit;
class QStackedWidget;
class QWidget;
QT_END_NAMESPACE

/// Finds settings by the texts their pages show and points at the first match.
class SettingsSearch : public QObject
{
    Q_OBJECT

public:
    SettingsSearch(QStackedWidget *pages, QWidget *dialog);

    QLineEdit *field() const { return m_field; }
    QLabel    *noResults() const { return m_noResults; }

    void reveal(int page);
    void retranslate();
    void applyTheme();

Q_SIGNALS:
    void filtered(const QList<int> &pages);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void run();
    QString query() const;
    bool matches(QWidget *widget) const;
    QWidget *firstMatch(QWidget *page) const;
    void flash(QWidget *target);

    QStackedWidget   *m_pages     { nullptr };
    QLineEdit        *m_field     { nullptr };
    QLabel           *m_noResults { nullptr };
    QAction          *m_icon      { nullptr };
    QTimer            m_delay;
    QPointer<QWidget> m_flash;
};
