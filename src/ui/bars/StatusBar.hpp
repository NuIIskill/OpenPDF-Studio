#pragma once

#include <QWidget>

#include <memory>

QT_BEGIN_NAMESPACE
class QLabel;
class QPushButton;
class QLineEdit;
class QIntValidator;
class QHBoxLayout;
QT_END_NAMESPACE

class IconButton;
struct DocumentInfo;

class StatusBar : public QWidget
{
    Q_OBJECT

public:
    explicit StatusBar(QWidget *parent = nullptr);
    ~StatusBar() override;

    void setPageInfo(int current, int total);
    void setDocumentInfo(bool open, const DocumentInfo *info);
    void setHint(const QString &text);
    QWidget *infoAnchor() const { return m_infoGroup; }
    void refreshTheme();
    void retranslateUi();

Q_SIGNALS:
    void previousPageRequested();
    void nextPageRequested();

    void pageRequested(int page);
    void panelToggleRequested();
    void infoRequested();
    void signaturesRequested();
    void protectionRequested();

private:
    void buildLayout();
    void commitPageInput();
    void buildInfoGroup(QHBoxLayout *layout);
    void refreshInfoBadges();

    int  m_currentPage { 1 };
    int  m_totalPages  { 1 };

    QLabel        *m_hintLabel  { nullptr };
    QLabel        *m_totalLabel { nullptr };
    QLineEdit     *m_pageInput  { nullptr };
    QIntValidator *m_validator  { nullptr };
    IconButton *m_prevBtn    { nullptr };
    IconButton *m_nextBtn    { nullptr };
    IconButton *m_panelBtn   { nullptr };

    QWidget     *m_infoGroup      { nullptr };
    IconButton  *m_infoBtn        { nullptr };
    QPushButton *m_signatureBadge { nullptr };
    QPushButton *m_lockBadge      { nullptr };
    bool         m_infoOpen       { false };
    std::unique_ptr<DocumentInfo> m_info;
};
