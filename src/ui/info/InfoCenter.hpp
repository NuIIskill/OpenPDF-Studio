#pragma once

#include <QFrame>
#include <QPointer>
#include <QSet>

#include <functional>

class DocumentView;
class QScrollArea;
class QVBoxLayout;

/// The document information card that rises from the status bar: file, protection, signatures and features.
class InfoCenter : public QFrame
{
    Q_OBJECT

public:
    static void toggle(DocumentView *view, QWidget *host, QWidget *anchor,
                       const QString &unfold = {});
    static void dismiss();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void hideEvent(QHideEvent *event) override;

private:
    InfoCenter(DocumentView *view, QWidget *host, QWidget *anchor, const QString &unfold);

    void rebuild();
    void place();
    void closeThen(const std::function<void()> &next);
    void scrollToUnfolded();

    QPointer<DocumentView> m_view;
    QPointer<QWidget>      m_anchor;
    QScrollArea           *m_scroll { nullptr };
    QWidget               *m_body   { nullptr };
    QVBoxLayout           *m_rows   { nullptr };
    QSet<QString>          m_expanded;
    QString                m_unfold;
    QPointer<QWidget>      m_unfolded;
};
