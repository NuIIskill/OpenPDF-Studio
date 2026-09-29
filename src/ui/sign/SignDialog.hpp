#pragma once

#include "engine/sign/SignatureManager.hpp"
#include "ui/sign/SignatureAppearance.hpp"

#include <QColor>
#include <QDialog>
#include <QImage>

QT_BEGIN_NAMESPACE
class QButtonGroup;
class QLabel;
class QLineEdit;
class QPushButton;
class QSlider;
class QStackedWidget;
class QToolButton;
class QVBoxLayout;
QT_END_NAMESPACE

class DigitalSignPage;
class SavedSignaturesView;
class SignaturePad;
class SignTabBar;

/// Creates a handwritten signature or collects the settings for a digital one.
class SignDialog : public QDialog
{
    Q_OBJECT

public:
    explicit SignDialog(QWidget *parent = nullptr);

    QImage signatureImage() const;
    bool isDigital() const;
    SignRequest signRequest() const;
    SignatureAppearance appearance() const;

private:
    void applyDialogStyle();
    QWidget *buildTabs();
    QWidget *buildHandwrittenPage();
    QWidget *buildModeRow();
    QWidget *buildCanvas();
    QWidget *buildTypedCanvas();
    QWidget *buildImageCanvas();
    QWidget *buildPenRow();
    QWidget *buildFooter();

    QPushButton *makeModeButton(const QString &icon, const QString &text);

    void setPenColor(const QColor &color);
    void onModeChanged(int mode);
    void onClear();
    void onChooseImage();
    void onSave();
    void updateButtons();

    QColor  m_penColor { QStringLiteral("#1D4ED8") };
    QImage  m_image;
    QString m_imagePath;

    SignTabBar     *m_tabBar       { nullptr };
    QStackedWidget *m_pages        { nullptr };
    QButtonGroup   *m_modeGroup    { nullptr };
    QStackedWidget *m_canvas       { nullptr };
    SignaturePad   *m_pad          { nullptr };
    QLineEdit      *m_typedEdit    { nullptr };
    QLabel         *m_imageLabel   { nullptr };
    QPushButton    *m_chooseImage  { nullptr };
    QPushButton    *m_clearBtn     { nullptr };
    SavedSignaturesView *m_saved  { nullptr };
    QWidget        *m_penRow       { nullptr };
    QWidget        *m_infoRow      { nullptr };
    QToolButton    *m_colorBtn     { nullptr };
    QSlider        *m_widthSlider  { nullptr };
    QLabel         *m_widthLabel   { nullptr };
    DigitalSignPage *m_digital     { nullptr };
    QPushButton    *m_saveBtn      { nullptr };
    QPushButton    *m_placeBtn     { nullptr };
};
