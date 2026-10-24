// SPDX-License-Identifier: LicenseRef-OpenPDF-Business
#pragma once

#include "rich-media/engine/MediaSpec.hpp"

#include <QFrame>

QT_BEGIN_NAMESPACE
class QCheckBox;
class QComboBox;
class QLabel;
class QPushButton;
QT_END_NAMESPACE

/// The panel for the options of the selected media and of the next one inserted.
class RichMediaPanel : public QFrame
{
    Q_OBJECT

public:
    static constexpr int kWidth = 344;

    explicit RichMediaPanel(QWidget *parent = nullptr);

    void setPlacement(int page, const QRectF &pdfBounds);
    void editMedia(const MediaSpec &spec);
    void resetForInsert();

    MediaSpec spec() const;

    void retranslateUi();

Q_SIGNALS:
    void applyRequested(const MediaSpec &spec);
    void closeRequested();

protected:
    void changeEvent(QEvent *event) override;

private:
    void buildUi();
    void applyStyle();
    void choosePoster();
    void refreshPoster();
    void updateApplyEnabled();
    bool changed() const;

    QImage  m_poster;
    bool    m_posterFromUser { false };
    QString m_sourcePath;
    QString m_sourceDisplayName;
    bool    m_floating { false };

    int    m_page { -1 };
    QRectF m_bounds;
    bool   m_editing { false };
    MediaSpec m_loaded;

    bool   m_stylingNow { false };
    MediaSpec::Type m_type { MediaSpec::Type::Video };

    QLabel       *m_icon         { nullptr };
    QLabel       *m_title        { nullptr };
    QLabel       *m_subtitle     { nullptr };
    QPushButton  *m_close        { nullptr };
    QLabel       *m_posterLabel  { nullptr };
    QLabel       *m_posterView   { nullptr };
    QPushButton  *m_posterChange { nullptr };
    QPushButton  *m_posterReset  { nullptr };
    QLabel       *m_playbackLabel{ nullptr };
    QLabel       *m_triggerLabel { nullptr };
    QComboBox    *m_trigger      { nullptr };
    QCheckBox    *m_autoPlay     { nullptr };
    QLabel       *m_autoPlayText { nullptr };
    QCheckBox    *m_muted        { nullptr };
    QLabel       *m_mutedText    { nullptr };
    QCheckBox    *m_loop         { nullptr };
    QLabel       *m_loopText     { nullptr };
    QCheckBox    *m_controls     { nullptr };
    QLabel       *m_controlsText { nullptr };
    QPushButton  *m_apply        { nullptr };
};
