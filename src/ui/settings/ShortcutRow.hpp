#pragma once

#include "ui/theme/Theme.hpp"

#include <QFrame>
#include <QLabel>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QKeyEvent>
#include <QKeySequence>
#include <QPushButton>
#include <QPainter>

class KeyCaptureEdit : public QLineEdit
{
    Q_OBJECT
public:
    explicit KeyCaptureEdit(QWidget *parent = nullptr) : QLineEdit(parent)
    {
        setReadOnly(true);
        setPlaceholderText(QStringLiteral("..."));
    }

    QKeySequence capturedSequence() const { return m_seq; }

    void setSequence(const QKeySequence &seq)
    {
        m_seq = seq;
        if (seq.isEmpty()) clear();
        else setText(seq.toString(QKeySequence::NativeText));
    }

protected:
    void keyPressEvent(QKeyEvent *e) override
    {
        const int key = e->key();
        if (key == Qt::Key_unknown || key == 0
            || key == Qt::Key_Control || key == Qt::Key_Shift
            || key == Qt::Key_Alt    || key == Qt::Key_Meta)
            return;
        if (key == Qt::Key_Escape) { QLineEdit::keyPressEvent(e); return; }
        m_seq = QKeySequence(QKeyCombination(e->modifiers(), static_cast<Qt::Key>(key)));
        setText(m_seq.toString(QKeySequence::NativeText));
        e->accept();
    }

private:
    QKeySequence m_seq;
};

class ShortcutRow : public QFrame
{
    Q_OBJECT
public:
    ShortcutRow(const QString &action, const QKeySequence &defaultSeq,
                QWidget *parent = nullptr)
        : QFrame(parent), m_defaultSeq(defaultSeq), m_currentSeq(defaultSeq)
    {
        setObjectName(QStringLiteral("ShortcutRowFrame"));
        setFrameShape(QFrame::NoFrame);
        setFixedHeight(46);
        buildUi(action);
    }

    bool matchesFilter(const QString &text) const
    {
        return text.isEmpty()
            || m_actionLabel->text().contains(text, Qt::CaseInsensitive);
    }

    bool isEditing() const { return m_editing; }
    void cancelIfEditing() { if (m_editing) exitEdit(false); }

    QKeySequence currentSequence() const { return m_currentSeq; }
    void setCurrentSequence(const QKeySequence &seq)
    {
        m_currentSeq = seq;
        m_seqDisplay->setText(seq.isEmpty() ? QString{}
                                             : seq.toString(QKeySequence::NativeText));
    }

Q_SIGNALS:
    void editStarted();

private:
    void buildUi(const QString &action)
    {
        auto *hl = new QHBoxLayout(this);
        hl->setContentsMargins(16, 0, 16, 0);
        hl->setSpacing(8);

        m_actionLabel = new QLabel(action, this);
        m_actionLabel->setFixedWidth(260);
        m_actionLabel->setObjectName(QStringLiteral("ShortcutAction"));
        hl->addWidget(m_actionLabel);

        m_seqDisplay = new QLabel(m_currentSeq.toString(QKeySequence::NativeText), this);
        m_seqDisplay->setObjectName(QStringLiteral("ShortcutDisplay"));
        m_seqDisplay->setAlignment(Qt::AlignCenter);
        m_seqDisplay->setCursor(Qt::PointingHandCursor);
        m_seqDisplay->installEventFilter(this);
        hl->addWidget(m_seqDisplay, 1);

        m_editArea = new QWidget(this);
        m_editArea->hide();
        {
            auto *ehl = new QHBoxLayout(m_editArea);
            ehl->setContentsMargins(0, 0, 0, 0);
            ehl->setSpacing(4);
            m_captureEdit = new KeyCaptureEdit(m_editArea);
            m_captureEdit->setObjectName(QStringLiteral("ShortcutCapture"));
            m_captureEdit->setFixedHeight(32);
            ehl->addWidget(m_captureEdit, 1);
            m_clearBtn = new QPushButton(m_editArea);
            m_clearBtn->setFlat(true);
            m_clearBtn->setObjectName(QStringLiteral("ShortcutClearBtn"));
            m_clearBtn->setFixedSize(28, 28);
            m_clearBtn->setCursor(Qt::PointingHandCursor);
            m_clearBtn->setIcon(Theme::makeIcon(QStringLiteral("x"),
                                                QColor(QStringLiteral("#6B7280")),
                                                QColor(QStringLiteral("#DC2626")),
                                                Theme::IconDisabled, 12));
            connect(m_clearBtn, &QPushButton::clicked, this, [this]() { m_captureEdit->clear(); });
            ehl->addWidget(m_clearBtn);
        }
        hl->addWidget(m_editArea, 1);

        m_resetBtn = new QPushButton(this);
        m_resetBtn->setObjectName(QStringLiteral("ShortcutResetBtn"));
        m_resetBtn->setFixedSize(32, 32);
        m_resetBtn->setCursor(Qt::PointingHandCursor);
        m_resetBtn->setToolTip(tr("Reset to default"));
        m_resetBtn->setIcon(Theme::makeIcon(QStringLiteral("undo-2"),
                                            Theme::IconMuted, Theme::IconChecked,
                                            Theme::IconDisabled, 14));
        connect(m_resetBtn, &QPushButton::clicked, this, [this]() {
            m_currentSeq = m_defaultSeq;
            m_seqDisplay->setText(m_defaultSeq.toString(QKeySequence::NativeText));
            if (m_editing) m_captureEdit->setSequence(m_defaultSeq);
        });
        hl->addWidget(m_resetBtn);

        m_editBtn = new QPushButton(tr("Edit"), this);
        m_editBtn->setObjectName(QStringLiteral("ShortcutEditBtn"));
        m_editBtn->setFixedSize(90, 32);
        m_editBtn->setCursor(Qt::PointingHandCursor);
        m_editBtn->setIcon(Theme::makeIcon(QStringLiteral("pencil"),
                                           Theme::IconNormal, Theme::IconChecked,
                                           Theme::IconDisabled, 13));
        connect(m_editBtn, &QPushButton::clicked, this, &ShortcutRow::enterEdit);
        hl->addWidget(m_editBtn);

        m_saveBtn = new QPushButton(tr("Save"), this);
        m_saveBtn->setObjectName(QStringLiteral("ShortcutSaveBtn"));
        m_saveBtn->setFixedSize(90, 32);
        m_saveBtn->hide();
        m_saveBtn->setCursor(Qt::PointingHandCursor);
        connect(m_saveBtn, &QPushButton::clicked, this, [this]() { exitEdit(true); });
        hl->addWidget(m_saveBtn);

        m_cancelBtn = new QPushButton(tr("Cancel"), this);
        m_cancelBtn->setObjectName(QStringLiteral("ShortcutCancelBtn"));
        m_cancelBtn->setFixedSize(90, 32);
        m_cancelBtn->hide();
        m_cancelBtn->setCursor(Qt::PointingHandCursor);
        connect(m_cancelBtn, &QPushButton::clicked, this, [this]() { exitEdit(false); });
        hl->addWidget(m_cancelBtn);
    }

    bool eventFilter(QObject *obj, QEvent *e) override
    {
        if (obj == m_seqDisplay && e->type() == QEvent::MouseButtonDblClick) {
            enterEdit();
            return true;
        }
        return QFrame::eventFilter(obj, e);
    }

    void enterEdit()
    {
        m_editing = true;
        m_captureEdit->setSequence(m_currentSeq);
        m_seqDisplay->hide();
        m_editArea->show();
        m_editBtn->hide();
        m_saveBtn->show();
        m_cancelBtn->show();
        m_actionLabel->setStyleSheet(QStringLiteral("font-size:13px;font-weight:700;"));
        m_captureEdit->setFocus();
        m_captureEdit->grabKeyboard();
        Q_EMIT editStarted();
    }

    void exitEdit(bool save)
    {
        m_captureEdit->releaseKeyboard();
        if (save && !m_captureEdit->text().trimmed().isEmpty())
            m_currentSeq = m_captureEdit->capturedSequence();
        m_editing = false;
        m_seqDisplay->setText(m_currentSeq.toString(QKeySequence::NativeText));
        m_editArea->hide();
        m_seqDisplay->show();
        m_saveBtn->hide();
        m_cancelBtn->hide();
        m_editBtn->show();
        m_actionLabel->setStyleSheet(QStringLiteral("font-size:13px;"));
    }

    bool           m_editing    { false };
    QKeySequence   m_defaultSeq;
    QKeySequence   m_currentSeq;

    QLabel         *m_actionLabel { nullptr };
    QLabel         *m_seqDisplay  { nullptr };
    QWidget        *m_editArea    { nullptr };
    KeyCaptureEdit *m_captureEdit { nullptr };
    QPushButton    *m_clearBtn    { nullptr };
    QPushButton    *m_resetBtn    { nullptr };
    QPushButton    *m_editBtn     { nullptr };
    QPushButton    *m_saveBtn     { nullptr };
    QPushButton    *m_cancelBtn   { nullptr };
};
