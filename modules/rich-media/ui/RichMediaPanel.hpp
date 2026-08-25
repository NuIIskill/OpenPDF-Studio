// SPDX-License-Identifier: LicenseRef-OpenPDF-Business
#pragma once

#include "rich-media/engine/MediaSpec.hpp"

#include <QFrame>

QT_BEGIN_NAMESPACE
class QCheckBox;
class QTimer;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QRadioButton;
QT_END_NAMESPACE

/// The insert panel on the right, visible while the media tool is chosen.
/// Registered through ToolPanels, in the same slot and under the same rule as
/// the text tool's panel.
///
/// It only collects; inserting happens through insertRequested(). MediaLayer
/// reports what happens on the page back through setPlacement().
class RichMediaPanel : public QFrame
{
    Q_OBJECT

public:
    static constexpr int kWidth = 344;

    explicit RichMediaPanel(QWidget *parent = nullptr);

    /// The area currently dragged out on the page. Without one the insert
    /// button stays disabled.
    void setPlacement(int page, const QRectF &pdfBounds);
    void clearPlacement();

    /// The panel's state as one insert.
    MediaSpec spec() const;

    void retranslateUi();

Q_SIGNALS:
    void insertRequested(const MediaSpec &spec);
    /// X, Y, width or height were typed in by hand.
    void placementEdited(const QRectF &pdfBounds);
    /// A source was chosen, so the page can show a preview.
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
    void pushGeometry();
    void setType(MediaSpec::Type type);

    /// The preview in the small box, and what goes into the document.
    QImage  m_poster;
    bool    m_posterFromUser { false };
    QTimer *m_posterDelay    { nullptr };

    int    m_page { -1 };
    QRectF m_bounds;
    bool   m_syncing { false };
    /// setStyleSheet() raises a StyleChange itself. Without this guard
    /// changeEvent() calls applyStyle() calls setStyleSheet(), forever.
    bool   m_stylingNow { false };
    double m_aspect  { 0.0 };   // kept while the lock is closed

    MediaSpec::Type m_type { MediaSpec::Type::Video };

    QLabel       *m_title        { nullptr };
    QLabel       *m_typeLabel    { nullptr };
    QList<QPushButton *> m_typeButtons;
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
    QCheckBox    *m_muted        { nullptr };
    QCheckBox    *m_loop         { nullptr };
    QCheckBox    *m_controls     { nullptr };
    QLabel       *m_placementLabel { nullptr };
    QRadioButton *m_inline       { nullptr };
    QRadioButton *m_floating     { nullptr };
    QLabel       *m_geometryLabel{ nullptr };
    QDoubleSpinBox *m_x { nullptr }, *m_y { nullptr }, *m_w { nullptr }, *m_h { nullptr };
    QPushButton  *m_lock         { nullptr };
    QPushButton  *m_insert       { nullptr };
    QLabel       *m_hint         { nullptr };
};
