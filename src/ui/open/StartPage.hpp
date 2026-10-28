#pragma once

#include <QWidget>

QT_BEGIN_NAMESPACE
class QDateTime;
class QLabel;
class QPushButton;
class QVBoxLayout;
QT_END_NAMESPACE

class AppSettings;

/// What an empty document tab shows: where to drop a document and the recently opened ones.
class StartPage : public QWidget
{
    Q_OBJECT

public:
    explicit StartPage(AppSettings *settings, QWidget *parent = nullptr);

Q_SIGNALS:
    void openRequested(const QString &path);

protected:
    void showEvent(QShowEvent *e) override;
    void changeEvent(QEvent *e) override;

private:
    void retranslateUi();
    void refreshIcons();
    void rebuild();
    QString openedText(const QDateTime &opened) const;

    AppSettings *m_settings;
    QLabel      *m_art;
    QLabel      *m_title;
    QLabel      *m_hint;
    QLabel      *m_emptyNote;
    QWidget     *m_recent;
    QLabel      *m_recentHeading;
    QPushButton *m_clear;
    QVBoxLayout *m_rows;
};
