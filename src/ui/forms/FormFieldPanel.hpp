#pragma once

#include "engine/forms/FormSpec.hpp"

#include <QHash>
#include <QList>
#include <QPointer>
#include <QWidget>

#include <optional>

QT_BEGIN_NAMESPACE
class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QFrame;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QSpinBox;
class QTimer;
class QVariantAnimation;
class QVBoxLayout;
QT_END_NAMESPACE

/// The kinds of field the design tool adds and the properties of the one picked.
class FormFieldPanel : public QWidget
{
    Q_OBJECT

public:
    static constexpr int kWidth = 340;

    explicit FormFieldPanel(QWidget *parent = nullptr);
    ~FormFieldPanel() override;

    static FormFieldPanel *instance();

    void setPlacement(std::optional<FormSpec::Kind> kind);
    void setGhostShown(bool shown);
    void setHint(bool shake, bool repeat);
    void setDocumentAvailable(bool available);
    void showField(const std::optional<FormSpec> &spec, int place);

Q_SIGNALS:
    void closeRequested();
    void kindChosen(FormSpec::Kind kind);
    void kindDragged(FormSpec::Kind kind, const QPoint &global);
    void kindDropped(FormSpec::Kind kind, const QPoint &global);
    void specEdited(const FormSpec &spec);
    void removeRequested();
    void kindHintSeen();

protected:
    void changeEvent(QEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void buildKinds(QVBoxLayout *root);
    void buildProperties(QVBoxLayout *root);
    void buildOptions();
    bool kindMouse(int row, QEvent *event);
    void startShaking();
    void shake(qreal progress);
    void stopShaking();
    void edited();
    FormSpec gathered() const;
    void showRows();
    void addRow(QVBoxLayout *layout, QWidget *field, bool labelled = true);
    void setRowVisible(QWidget *field, bool shown);
    void setRowLabel(QWidget *field, const QString &text);
    void addOption();
    void fitOptions();
    void refreshIcons();
    void retranslateUi();

    QLabel         *m_title { nullptr };
    QPushButton    *m_close { nullptr };
    QFrame         *m_kindList { nullptr };
    QList<QPushButton *> m_kindRows;
    QList<QLabel *> m_grips;
    QVariantAnimation *m_shake { nullptr };
    QTimer         *m_shakeAgain { nullptr };
    bool            m_shakes { false };
    bool            m_shakeRepeats { false };
    bool            m_shakeShown { false };
    QLabel         *m_hint { nullptr };
    QWidget        *m_props { nullptr };
    QHash<QWidget *, QWidget *> m_rows;
    QComboBox      *m_kind { nullptr };
    QLineEdit      *m_name { nullptr };
    QWidget        *m_optionsBox { nullptr };
    QListWidget    *m_options { nullptr };
    QPushButton    *m_addOption { nullptr };
    QLineEdit      *m_exportValue { nullptr };
    QCheckBox      *m_required { nullptr };
    QCheckBox      *m_multiline { nullptr };
    QComboBox      *m_format { nullptr };
    QSpinBox       *m_decimals { nullptr };
    QLineEdit      *m_pattern { nullptr };
    QComboBox      *m_calculation { nullptr };
    QLineEdit      *m_sources { nullptr };
    QLabel         *m_note { nullptr };
    QLineEdit      *m_tooltip { nullptr };
    QPushButton    *m_addTooltip { nullptr };
    QPushButton    *m_more { nullptr };
    QCheckBox      *m_readOnly { nullptr };
    QCheckBox      *m_editable { nullptr };
    QDoubleSpinBox *m_fontSize { nullptr };
    QLineEdit      *m_minimum { nullptr };
    QLineEdit      *m_maximum { nullptr };
    QPushButton    *m_remove { nullptr };

    std::optional<FormSpec> m_spec;
    std::optional<FormSpec::Kind> m_placement;
    int                     m_place { 0 };
    bool                    m_filling { false };
    bool                    m_tooltipOpen { false };
    int                     m_dragRow { -1 };
    QPoint                  m_pressGlobal;
    QPointer<QLabel>        m_ghost;
};
