#pragma once

#include <QList>
#include <QWidget>

QT_BEGIN_NAMESPACE
class QLabel;
class QPushButton;
class QVBoxLayout;
QT_END_NAMESPACE

/// The list of areas marked for redaction.
class RedactionPanel : public QWidget
{
    Q_OBJECT

public:
    static constexpr int kWidth = 340;

    explicit RedactionPanel(QWidget *parent = nullptr);
    ~RedactionPanel() override;

    static RedactionPanel *instance();

    void setRegions(const QList<int> &pages, int selected);

Q_SIGNALS:
    void closeRequested();
    void regionActivated(int index);
    void removeRequested(int index);

protected:
    void changeEvent(QEvent *event) override;

private:
    void rebuildList();
    void retranslateUi();
    void refreshIcons();

    QLabel      *m_title { nullptr };
    QPushButton *m_close { nullptr };
    QWidget     *m_list { nullptr };
    QVBoxLayout *m_listLayout { nullptr };
    QList<int>   m_pages;
    int          m_selected { -1 };
};
