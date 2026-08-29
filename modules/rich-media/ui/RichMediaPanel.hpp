// SPDX-License-Identifier: LicenseRef-OpenPDF-Business
#pragma once

#include "rich-media/engine/MediaSpec.hpp"

#include <QFrame>

QT_BEGIN_NAMESPACE
class QCheckBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QRadioButton;
QT_END_NAMESPACE

/// The panel for editing selected media or adding a new media source.
class RichMediaPanel : public QFrame
{
    Q_OBJECT

public:
    static constexpr int kWidth = 344;

    explicit RichMediaPanel(QWidget *parent = nullptr);

    void setPlacement(int page, const QRectF &pdfBounds);
    void clearPlacement();
    void editMedia(const MediaSpec &spec);
    void resetForInsert();

    MediaSpec spec() const;

    void retranslateUi();

Q_SIGNALS:
    void applyRequested(const MediaSpec &spec);
    void closeRequested();

    void previewChanged(const QImage &poster);

protected:
    void changeEvent(QEvent *event) override;

private:
    void buildUi();
    void applyStyle();
    void chooseSource();
    void choosePoster();
    void refreshPoster();
    void updateInsertEnabled();
    void setSource(const QString &path, const QString &displayName = QString());

    QImage  m_poster;
    bool    m_posterFromUser { false };
    QString m_sourcePath;
    QString m_sourceDisplayName;

    int    m_page { -1 };
    QRectF m_bounds;
    bool   m_editing { false };

    bool   m_stylingNow { false };
    MediaSpec::Type m_type { MediaSpec::Type::Video };

    QLabel       *m_title        { nullptr };
    QLabel       *m_subtitle     { nullptr };
    QPushButton  *m_close        { nullptr };
    QLabel       *m_sourceLabel  { nullptr };
    QLineEdit    *m_source       { nullptr };
    QPushButton  *m_browse       { nullptr };
    QLabel       *m_posterLabel  { nullptr };
    QLabel       *m_posterView   { nullptr };
    QPushButton  *m_posterChange { nullptr };
    QPushButton  *m_posterRemove { nullptr };
    QLabel       *m_triggerLabel { nullptr };
    QRadioButton *m_onClick      { nullptr };
    QRadioButton *m_onPageOpen   { nullptr };
    QLabel       *m_playbackLabel{ nullptr };
    QCheckBox    *m_autoPlay     { nullptr };
    QLabel       *m_autoPlayText { nullptr };
    QCheckBox    *m_muted        { nullptr };
    QLabel       *m_mutedText    { nullptr };
    QCheckBox    *m_loop         { nullptr };
    QLabel       *m_loopText     { nullptr };
    QCheckBox    *m_controls     { nullptr };
    QLabel       *m_controlsText { nullptr };
    QLabel       *m_placementLabel { nullptr };
    QRadioButton *m_inline       { nullptr };
    QRadioButton *m_floating     { nullptr };
    QPushButton  *m_insert       { nullptr };
    QLabel       *m_hint         { nullptr };
};
