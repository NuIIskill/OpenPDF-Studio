#pragma once

#include <QFrame>
#include <QSet>

QT_BEGIN_NAMESPACE
class QLabel;
class QTimer;
QT_END_NAMESPACE

/// Small note over the document that editing a text will not match it exactly, which the user can turn off.
class EditNotice : public QFrame
{
    Q_OBJECT

public:
    enum class Kind { Scan, Font };

    explicit EditNotice(QWidget *host);

    void offer(Kind kind);
    void forgetShown() { m_shown.clear(); }

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    static QString keyOf(Kind kind);
    void reposition();

    QLabel *m_icon { nullptr };
    QLabel *m_text { nullptr };
    QTimer *m_hold { nullptr };
    Kind    m_kind { Kind::Scan };
    QSet<int> m_shown;
};
