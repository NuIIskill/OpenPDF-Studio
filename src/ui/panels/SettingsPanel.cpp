#include "ui/panels/SettingsPanel.hpp"
#include "app/AppSettings.hpp"
#include "app/AppConfig.hpp"
#include "drm/LicenseStore.hpp"
#include "drm/LicensePage.hpp"
#include "ui/theme/Theme.hpp"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QStackedWidget>
#include <QScrollArea>
#include <QScrollBar>
#include <QFrame>
#include <QEvent>
#include <QMouseEvent>
#include <QApplication>
#include <QAbstractButton>
#include <QCheckBox>
#include <QComboBox>
#include <QKeyEvent>
#include <QKeySequence>
#include <QLineEdit>
#include <QPainter>
#include <QPainterPath>
#include <QSpinBox>

// ── OptionCard ────────────────────────────────────────────────────────────────

class OptionCard : public QFrame
{
    Q_OBJECT
public:
    explicit OptionCard(const QString &iconName, const QString &title,
                        const QString &desc, QWidget *parent = nullptr)
        : QFrame(parent), m_iconName(iconName)
    {
        setFixedSize(175, 112);
        setCursor(Qt::PointingHandCursor);
        setAttribute(Qt::WA_StyledBackground, true);

        auto *vbox = new QVBoxLayout(this);
        vbox->setContentsMargins(12, 12, 12, 10);
        vbox->setSpacing(5);
        vbox->setAlignment(Qt::AlignHCenter | Qt::AlignTop);

        m_iconLabel = new QLabel(this);
        m_iconLabel->setFixedSize(26, 26);
        m_iconLabel->setAlignment(Qt::AlignCenter);
        m_iconLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
        vbox->addWidget(m_iconLabel, 0, Qt::AlignHCenter);

        m_titleLabel = new QLabel(title, this);
        m_titleLabel->setAlignment(Qt::AlignCenter);
        m_titleLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
        vbox->addWidget(m_titleLabel);

        m_descLabel = new QLabel(desc, this);
        m_descLabel->setAlignment(Qt::AlignCenter);
        m_descLabel->setWordWrap(true);
        m_descLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
        vbox->addWidget(m_descLabel);

        m_check = new QLabel(QStringLiteral("✓"), this);
        m_check->setFixedSize(20, 20);
        m_check->setAlignment(Qt::AlignCenter);
        m_check->hide();

        applyStyle(false);
    }

    bool isSelected() const { return m_selected; }
    void setSelected(bool v) { m_selected = v; m_check->setVisible(v); applyStyle(v); }
    const QString &iconName() const { return m_iconName; }

Q_SIGNALS:
    void clicked();

protected:
    void mousePressEvent(QMouseEvent *e) override
    {
        if (e->button() == Qt::LeftButton) Q_EMIT clicked();
    }
    void resizeEvent(QResizeEvent *e) override
    {
        QFrame::resizeEvent(e);
        m_check->move(width() - 26, 5);
    }

private:
    void applyStyle(bool sel)
    {
        const bool dk = Theme::DarkMode;
        const QColor iconColor = sel ? QColor("#3B82F6")
                                     : QColor(dk ? "#9CA3AF" : "#6B7280");
        const QPixmap px = Theme::renderSvg(m_iconName, iconColor, 22);
        if (!px.isNull()) m_iconLabel->setPixmap(px);

        if (sel) {
            const QString bg = dk ? "#1E3358" : "#FFFFFF";
            setStyleSheet(QStringLiteral(
                "OptionCard { border:2px solid #3B82F6; border-radius:8px; background:%1; }").arg(bg));
            m_check->setStyleSheet(QStringLiteral(
                "background:#3B82F6; border-radius:10px; color:white; font-size:12px; font-weight:700;"));
            m_titleLabel->setStyleSheet(dk
                ? QStringLiteral("font-size:12px; font-weight:600; color:#93C5FD;")
                : QStringLiteral("font-size:12px; font-weight:600; color:#1D4ED8;"));
            m_descLabel->setStyleSheet(dk
                ? QStringLiteral("font-size:10px; color:#60A5FA;")
                : QStringLiteral("font-size:10px; color:#3B82F6;"));
        } else {
            const QString bg  = dk ? "#404040" : "#FFFFFF";
            const QString bdr = dk ? "#555555" : "#E5E7EB";
            setStyleSheet(QStringLiteral(
                "OptionCard { border:1px solid %1; border-radius:8px; background:%2; }").arg(bdr, bg));
            m_titleLabel->setStyleSheet(dk
                ? QStringLiteral("font-size:12px; font-weight:600; color:#D0D0D0;")
                : QStringLiteral("font-size:12px; font-weight:600; color:#111827;"));
            m_descLabel->setStyleSheet(dk
                ? QStringLiteral("font-size:10px; color:#9CA3AF;")
                : QStringLiteral("font-size:10px; color:#6B7280;"));
        }
    }

    QLabel *m_iconLabel  { nullptr };
    QLabel *m_titleLabel { nullptr };
    QLabel *m_descLabel  { nullptr };
    QLabel *m_check      { nullptr };
    QString m_iconName;
    bool    m_selected   { false };
};

// ── LangRow ───────────────────────────────────────────────────────────────────

class LangRow : public QFrame
{
    Q_OBJECT
public:
    explicit LangRow(const QString &code, const QString &displayName,
                     QWidget *parent = nullptr)
        : QFrame(parent), m_code(code)
    {
        setFixedHeight(48);
        setCursor(Qt::PointingHandCursor);
        setAttribute(Qt::WA_StyledBackground, true);

        auto *row = new QHBoxLayout(this);
        row->setContentsMargins(20, 0, 20, 0);
        row->setSpacing(12);

        m_nameLabel = new QLabel(displayName, this);
        m_nameLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
        row->addWidget(m_nameLabel, 1);

        m_checkLabel = new QLabel(QStringLiteral("✓"), this);
        m_checkLabel->setFixedWidth(24);
        m_checkLabel->setAlignment(Qt::AlignCenter);
        m_checkLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
        row->addWidget(m_checkLabel);

        applyStyle(false);
    }

    QString code() const { return m_code; }
    bool isSelected() const { return m_selected; }

    void setSelected(bool v)
    {
        m_selected = v;
        m_checkLabel->setVisible(v);
        applyStyle(v);
    }

Q_SIGNALS:
    void clicked();

protected:
    void mousePressEvent(QMouseEvent *e) override
    {
        if (e->button() == Qt::LeftButton) Q_EMIT clicked();
    }

private:
    void applyStyle(bool sel)
    {
        const bool dk = Theme::DarkMode;
        const QString selBg = dk ? "#1E3A5F" : "#EFF6FF";
        const QString hovBg = dk ? "#3E3E3E" : "#F3F4F6";
        const QString txtSel = dk ? "#93C5FD" : "#1D4ED8";
        const QString txtNor = dk ? "#D8D8D8" : "#111827";
        const QString chkClr = dk ? "#93C5FD" : "#2563EB";

        setStyleSheet(sel
            ? QStringLiteral("LangRow { background:%1; border:none; }"
                              "LangRow:hover { background:%1; }").arg(selBg)
            : QStringLiteral("LangRow { background:transparent; border:none; }"
                              "LangRow:hover { background:%1; }").arg(hovBg));

        m_nameLabel->setStyleSheet(QStringLiteral(
            "font-size:14px; color:%1; font-weight:%2;")
            .arg(sel ? txtSel : txtNor, sel ? QLatin1String("600") : QLatin1String("400")));

        m_checkLabel->setStyleSheet(QStringLiteral(
            "font-size:14px; font-weight:700; color:%1;").arg(chkClr));
    }

    QLabel *m_nameLabel  { nullptr };
    QLabel *m_checkLabel { nullptr };
    QString m_code;
    bool    m_selected   { false };
};

// ── KeyCaptureEdit ────────────────────────────────────────────────────────────

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

// ── ShortcutRow ───────────────────────────────────────────────────────────────

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
        m_captureEdit->grabKeyboard(); // grab all keys so the modal dialog can't intercept them
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

// ── ToggleSwitch ──────────────────────────────────────────────────────────────

class ToggleSwitch : public QAbstractButton
{
    Q_OBJECT
public:
    explicit ToggleSwitch(QWidget *parent = nullptr)
        : QAbstractButton(parent)
    {
        setCheckable(true);
        setFixedSize(44, 26);
        setCursor(Qt::PointingHandCursor);
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        const bool on = isChecked();
        const bool dk = Theme::DarkMode;

        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);

        p.setPen(Qt::NoPen);
        p.setBrush(on ? QColor(QStringLiteral("#2563EB"))
                      : QColor(dk ? QStringLiteral("#555555")
                                  : QStringLiteral("#D1D5DB")));
        p.drawRoundedRect(rect(), 13, 13);

        p.setBrush(Qt::white);
        const int kd = 20;
        const int ky = (height() - kd) / 2;
        const int kx = on ? (width() - kd - 2) : 2;
        p.drawEllipse(kx, ky, kd, kd);
    }
};

// ── SettingCheckBox ───────────────────────────────────────────────────────────
// Painted by hand for the same reason ToggleSwitch is: a style-sheet indicator
// would need a bitmap check mark, and the app deliberately ships without Qt SVG.

class SettingCheckBox : public QAbstractButton
{
    Q_OBJECT
public:
    explicit SettingCheckBox(const QString &text, QWidget *parent = nullptr)
        : QAbstractButton(parent)
    {
        setCheckable(true);
        setText(text);
        setCursor(Qt::PointingHandCursor);
        setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    }

    static constexpr int kBox     = 20;   // indicator edge length
    static constexpr int kSpacing = 14;   // indicator → label gap

    [[nodiscard]] QSize sizeHint() const override
    {
        const QFontMetrics fm(labelFont());
        return { kBox + kSpacing + fm.horizontalAdvance(text()),
                 qMax(kBox, fm.height()) };
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        const bool on = isChecked();
        const bool dk = Theme::DarkMode;

        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);

        const QRectF box(0.5, (height() - kBox) / 2.0 + 0.5, kBox - 1, kBox - 1);
        if (on) {
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(QStringLiteral("#2563EB")));
            p.drawRoundedRect(box, 5, 5);

            QPen check(Qt::white, 2.2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
            p.setPen(check);
            const qreal x = box.left(), y = box.top(), w = box.width(), h = box.height();
            QPainterPath path;
            path.moveTo(x + w * 0.26, y + h * 0.52);
            path.lineTo(x + w * 0.44, y + h * 0.70);
            path.lineTo(x + w * 0.76, y + h * 0.32);
            p.drawPath(path);
        } else {
            p.setPen(QPen(QColor(dk ? QStringLiteral("#4B5563")
                                    : QStringLiteral("#D1D5DB")), 1.5));
            p.setBrush(QColor(dk ? QStringLiteral("#1F2124")
                                 : QStringLiteral("#FFFFFF")));
            p.drawRoundedRect(box, 5, 5);
        }

        p.setFont(labelFont());
        p.setPen(QColor(dk ? QStringLiteral("#E5E7EB") : QStringLiteral("#111827")));
        p.drawText(QRect(kBox + kSpacing, 0, width() - kBox - kSpacing, height()),
                   Qt::AlignLeft | Qt::AlignVCenter, text());
    }

private:
    [[nodiscard]] QFont labelFont() const
    {
        QFont f = font();
        f.setPointSizeF(f.pointSizeF() > 0 ? f.pointSizeF() : 10);
        f.setBold(true);
        return f;
    }
};

#include "SettingsPanel.moc"

// ── SettingsPanel ─────────────────────────────────────────────────────────────

SettingsPanel::SettingsPanel(AppSettings *settings, QWidget *parent)
    : QDialog(parent)
    , m_settings(settings)
    , m_pendingTheme(settings->theme())
    , m_pendingLang(settings->language())
    , m_originalTheme(settings->theme())
    , m_originalLang(settings->language())
{
    setObjectName(QStringLiteral("SettingsPanel"));
    setWindowTitle(tr("Settings - OpenPDF Studio"));
    setAttribute(Qt::WA_DeleteOnClose);
    setModal(true);
    setFixedSize(900, 660);

    buildUi();

    connect(this, &QDialog::rejected, this, [this]() {
        if (m_pendingTheme != m_originalTheme)
            Q_EMIT themeChangeRequested(m_originalTheme);
    });
}

// ── UI skeleton ───────────────────────────────────────────────────────────────

void SettingsPanel::buildUi()
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    auto *contentRow = new QHBoxLayout;
    contentRow->setContentsMargins(0, 0, 0, 0);
    contentRow->setSpacing(0);

    // ── Sidebar ───────────────────────────────────────────────────────────
    auto *sidebar = new QWidget(this);
    sidebar->setObjectName(QStringLiteral("SettingsSidebar"));
    sidebar->setFixedWidth(190);
    auto *sideLayout = new QVBoxLayout(sidebar);
    sideLayout->setContentsMargins(8, 12, 8, 12);
    sideLayout->setSpacing(2);
    buildNav(sidebar, sideLayout);
    contentRow->addWidget(sidebar);

    // ── Pages ─────────────────────────────────────────────────────────────
    m_pages = new QStackedWidget(this);
    m_pages->setObjectName(QStringLiteral("SettingsPages"));
    m_pages->addWidget(buildAppearancePage());  // 0
    m_pages->addWidget(buildLanguagePage());    // 1
    m_pages->addWidget(buildMediaPage());       // 2
    m_pages->addWidget(buildShortcutsPage());   // 3
    m_pages->addWidget(buildZoomPage());        // 4
    m_pages->addWidget(buildAdvancedPage());    // 5
    // License: business installations only — see buildNav().
    if (License::isBusinessInstall()) {
        m_licensePage = new LicensePage(this);
        m_pages->addWidget(m_licensePage);
    }
    m_pages->addWidget(buildAboutPage());       // last

    // Nav entry i shows page i — including the optional license entry, which is
    // why both lists are built from the same condition.
    Q_ASSERT(m_pages->count() == m_navItems.size());

    contentRow->addWidget(m_pages, 1);
    root->addLayout(contentRow, 1);

    // ── Bottom bar ────────────────────────────────────────────────────────
    auto *sep = new QFrame(this);
    sep->setObjectName(QStringLiteral("SettingsSeparator"));
    sep->setFrameShape(QFrame::HLine);
    root->addWidget(sep);

    auto *bar = new QHBoxLayout;
    bar->setContentsMargins(16, 10, 16, 10);
    bar->setSpacing(8);

    bar->addStretch(1);

    auto *cancelBtn = new QPushButton(tr("Cancel"), this);
    cancelBtn->setObjectName(QStringLiteral("SettingsCancelBtn"));
    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
    bar->addWidget(cancelBtn);

    auto *saveBtn = new QPushButton(tr("Save"), this);
    saveBtn->setObjectName(QStringLiteral("SettingsSaveBtn"));
    saveBtn->setDefault(true);
    connect(saveBtn, &QPushButton::clicked, this, &SettingsPanel::applyAndClose);
    bar->addWidget(saveBtn);

    root->addLayout(bar);

    selectPage(0);
}

// ── Nav ───────────────────────────────────────────────────────────────────────

void SettingsPanel::buildNav(QWidget *, QVBoxLayout *layout)
{
    struct Item { const char *icon; const char *label; };
    QList<Item> items = {
        { "monitor",     QT_TR_NOOP("Appearance")     },
        { "globe",       QT_TR_NOOP("Language")       },
        { "play-circle", QT_TR_NOOP("Media Playback") },
        { "keyboard",    QT_TR_NOOP("Shortcuts")      },
        { "zoom-in",     QT_TR_NOOP("Zoom")           },
        { "sliders",     QT_TR_NOOP("Advanced")       },
    };
    // A personal installation has nothing to do with license keys and does not
    // get the page — the entry only exists where a key can actually be needed.
    if (License::isBusinessInstall())
        items.append(Item{ "key", QT_TR_NOOP("License Key") });

    for (int i = 0; i < items.size(); ++i) {
        auto *btn = new QPushButton(this);
        btn->setFixedHeight(38);
        btn->setFlat(true);
        btn->setCursor(Qt::PointingHandCursor);

        auto *row = new QHBoxLayout(btn);
        row->setContentsMargins(10, 0, 10, 0);
        row->setSpacing(8);

        auto *iconLbl = new QLabel(btn);
        iconLbl->setFixedSize(18, 18);
        iconLbl->setAlignment(Qt::AlignCenter);
        iconLbl->setAttribute(Qt::WA_TransparentForMouseEvents);
        row->addWidget(iconLbl);

        auto *textLbl = new QLabel(tr(items[i].label), btn);
        textLbl->setAttribute(Qt::WA_TransparentForMouseEvents);
        row->addWidget(textLbl, 1);

        m_navItems.append({ btn, iconLbl, textLbl,
                            QLatin1String(items[i].icon), items[i].label });

        const int navIdx = i;
        connect(btn, &QPushButton::clicked, this, [this, navIdx]() { selectPage(navIdx); });
        layout->addWidget(btn);
    }

    layout->addStretch(1);

    // About — pinned to bottom
    auto *aboutBtn = new QPushButton(this);
    aboutBtn->setFixedHeight(38);
    aboutBtn->setFlat(true);
    aboutBtn->setCursor(Qt::PointingHandCursor);
    auto *arow = new QHBoxLayout(aboutBtn);
    arow->setContentsMargins(10, 0, 10, 0);
    arow->setSpacing(8);
    auto *aicon = new QLabel(aboutBtn);
    aicon->setFixedSize(18, 18);
    aicon->setAttribute(Qt::WA_TransparentForMouseEvents);
    arow->addWidget(aicon);
    auto *atext = new QLabel(tr("About"), aboutBtn);
    atext->setAttribute(Qt::WA_TransparentForMouseEvents);
    arow->addWidget(atext, 1);
    const int aboutIdx = m_navItems.size();
    m_navItems.append({ aboutBtn, aicon, atext, QStringLiteral("info"),
                        QT_TR_NOOP("About") });
    connect(aboutBtn, &QPushButton::clicked, this, [this, aboutIdx]() { selectPage(aboutIdx); });
    layout->addWidget(aboutBtn);
}

void SettingsPanel::applyNavItemStyle(int i, bool sel)
{
    if (i < 0 || i >= m_navItems.size()) return;
    NavItem &ni = m_navItems[i];
    const bool dk = Theme::DarkMode;

    const QString selBg  = dk ? "#1E3A5F" : "#DBEAFE";
    const QString selHov = dk ? "#2A4A70" : "#BFDBFE";
    const QString norHov = dk ? "#3E3E3E" : "#F3F4F6";
    const QString selTxt = dk ? "#93C5FD" : "#1D4ED8";
    const QString norTxt = dk ? "#C8C8C8" : "#374151";

    const QPixmap px = Theme::renderSvg(ni.iconName,
        QColor(sel ? (dk ? "#93C5FD" : "#3B82F6") : (dk ? "#888888" : "#6B7280")), 16);
    if (!px.isNull()) ni.iconLabel->setPixmap(px);

    ni.textLabel->setStyleSheet(sel
        ? QStringLiteral("font-size:13px; font-weight:600; color:%1;").arg(selTxt)
        : QStringLiteral("font-size:13px; color:%1;").arg(norTxt));

    ni.btn->setStyleSheet(sel
        ? QStringLiteral(
            "QPushButton { border-radius:6px; background:%1; border:none; }"
            "QPushButton:hover { background:%2; }").arg(selBg, selHov)
        : QStringLiteral(
            "QPushButton { border-radius:6px; background:transparent; border:none; }"
            "QPushButton:hover { background:%1; }").arg(norHov));
}

void SettingsPanel::selectPage(int navIndex)
{
    m_currentNav = navIndex;
    m_pages->setCurrentIndex(navIndex);
    for (int i = 0; i < m_navItems.size(); ++i)
        applyNavItemStyle(i, i == navIndex);
}

void SettingsPanel::showLicensePage()
{
    if (m_licensePage)
        selectPage(m_pages->indexOf(m_licensePage));
}

void SettingsPanel::selectPageForTest(const QString &navLabel)
{
    for (int i = 0; i < m_navItems.size(); ++i)
        if (navLabel.compare(QLatin1String(m_navItems[i].labelKey),
                             Qt::CaseInsensitive) == 0) {
            selectPage(i);
            return;
        }
}

void SettingsPanel::refreshThemeColors()
{
    for (int i = 0; i < m_navItems.size(); ++i)
        applyNavItemStyle(i, i == m_currentNav);
    for (auto *w : m_themeCards)
        if (auto *c = qobject_cast<OptionCard*>(w)) c->setSelected(c->isSelected());
    for (auto *w : m_mediaCards)
        if (auto *c = qobject_cast<OptionCard*>(w)) c->setSelected(c->isSelected());
    for (auto *w : m_langRows)
        if (auto *r = qobject_cast<LangRow*>(w)) r->setSelected(r->isSelected());
    if (m_licensePage) m_licensePage->applyTheme();
}

// ── Page builders ─────────────────────────────────────────────────────────────

QWidget *SettingsPanel::buildAppearancePage()
{
    auto *page = new QWidget(this);
    page->setObjectName(QStringLiteral("SettingsScrollContent"));
    auto *vbox = new QVBoxLayout(page);
    vbox->setContentsMargins(32, 28, 32, 32);
    vbox->setSpacing(0);

    auto *title = new QLabel(tr("Appearance"), page);
    title->setObjectName(QStringLiteral("SettingsSectionTitle"));
    vbox->addWidget(title);
    vbox->addSpacing(6);

    auto *desc = new QLabel(tr("Choose how OpenPDF Studio is displayed."), page);
    desc->setObjectName(QStringLiteral("SettingsSectionDesc"));
    vbox->addWidget(desc);
    vbox->addSpacing(24);

    auto *row = new QHBoxLayout;
    row->setSpacing(14);
    row->setAlignment(Qt::AlignLeft);

    struct T { const char *icon, *id, *title, *desc; };
    const T themes[] = {
        { "monitor", "system", QT_TR_NOOP("System"), QT_TR_NOOP("Follows system settings") },
        { "sun",     "light",  QT_TR_NOOP("Light"),  QT_TR_NOOP("Light user interface") },
        { "moon",    "dark",   QT_TR_NOOP("Dark"),   QT_TR_NOOP("Dark user interface") },
    };
    for (const auto &t : themes)
        row->addWidget(buildOptionCard(
            QLatin1String(t.icon), tr(t.title), tr(t.desc),
            QLatin1String(t.id), m_themeCards, m_themeIds, true));
    row->addStretch(1);
    vbox->addLayout(row);
    vbox->addStretch(1);
    return page;
}

QWidget *SettingsPanel::buildLanguagePage()
{
    auto *page = new QWidget(this);
    page->setObjectName(QStringLiteral("SettingsScrollContent"));
    auto *vbox = new QVBoxLayout(page);
    vbox->setContentsMargins(0, 28, 0, 0);
    vbox->setSpacing(0);

    auto *titleWrap = new QWidget(page);
    titleWrap->setObjectName(QStringLiteral("SettingsScrollContent"));
    auto *twl = new QVBoxLayout(titleWrap);
    twl->setContentsMargins(32, 0, 32, 0);
    twl->setSpacing(6);

    auto *title = new QLabel(tr("Language"), titleWrap);
    title->setObjectName(QStringLiteral("SettingsSectionTitle"));
    twl->addWidget(title);

    auto *desc = new QLabel(tr("Choose your preferred interface language."), titleWrap);
    desc->setObjectName(QStringLiteral("SettingsSectionDesc"));
    twl->addWidget(desc);

    vbox->addWidget(titleWrap);
    vbox->addSpacing(16);

    // ── Scrollable language list ──────────────────────────────────────────
    auto *scroll = new QScrollArea(page);
    scroll->setObjectName(QStringLiteral("SettingsScroll"));
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scroll->setWidgetResizable(true);

    auto *listWidget = new QWidget;
    listWidget->setObjectName(QStringLiteral("SettingsScrollContent"));
    auto *listLayout = new QVBoxLayout(listWidget);
    listLayout->setContentsMargins(0, 0, 0, 0);
    listLayout->setSpacing(0);

    // English first, then separator, then alphabetical by display name
    struct L { const char *code; const char *display; };
    const L langs[] = {
        { "en", "English"    },
        { nullptr, nullptr   },  // separator
        { "de", "Deutsch"    },
        { "fr", "Français"   },
        { "es", "Español"    },
        { "it", "Italiano"   },
        { "pt", "Português"  },
        { "nl", "Nederlands" },
        { "pl", "Polski"     },
        { "ru", "Русский"    },
        { "zh", "中文"        },
        { "ja", "日本語"      },
        { "ko", "한국어"      },
    };

    for (const auto &l : langs) {
        if (!l.code) {
            // Separator between English and other languages
            auto *sep = new QFrame(listWidget);
            sep->setObjectName(QStringLiteral("SettingsSeparator"));
            sep->setFrameShape(QFrame::HLine);
            sep->setFixedHeight(1);
            listLayout->addWidget(sep);
        } else {
            addLangRow(listWidget, listLayout, QLatin1String(l.code),
                       QString::fromUtf8(l.display));
        }
    }

    listLayout->addStretch(1);
    scroll->setWidget(listWidget);
    vbox->addWidget(scroll, 1);
    return page;
}

QWidget *SettingsPanel::buildMediaPage()
{
    auto *page = new QWidget(this);
    page->setObjectName(QStringLiteral("SettingsScrollContent"));
    auto *vbox = new QVBoxLayout(page);
    vbox->setContentsMargins(32, 28, 32, 32);
    vbox->setSpacing(0);

    auto *title = new QLabel(tr("Media Playback"), page);
    title->setObjectName(QStringLiteral("SettingsSectionTitle"));
    vbox->addWidget(title);
    vbox->addSpacing(6);

    auto *desc = new QLabel(tr("Choose how media is played in OpenPDF Studio."), page);
    desc->setObjectName(QStringLiteral("SettingsSectionDesc"));
    vbox->addWidget(desc);
    vbox->addSpacing(24);

    auto *row = new QHBoxLayout;
    row->setSpacing(14);
    row->setAlignment(Qt::AlignLeft);

    struct M { const char *icon, *id, *title, *desc; };
    const M media[] = {
        { "play-circle", "default", QT_TR_NOOP("Default"),   QT_TR_NOOP("Use system player") },
        { "folder-open", "custom",  QT_TR_NOOP("Custom..."), QT_TR_NOOP("Custom media player") },
    };
    for (const auto &m : media)
        row->addWidget(buildOptionCard(
            QLatin1String(m.icon), tr(m.title), tr(m.desc),
            QLatin1String(m.id), m_mediaCards, m_mediaIds, false));
    row->addStretch(1);
    vbox->addLayout(row);
    vbox->addStretch(1);
    return page;
}

QWidget *SettingsPanel::buildShortcutsPage()
{
    auto *page = new QWidget(this);
    page->setObjectName(QStringLiteral("SettingsScrollContent"));
    auto *outer = new QVBoxLayout(page);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);

    // Header
    {
        auto *hdr = new QWidget(page);
        hdr->setObjectName(QStringLiteral("SettingsScrollContent"));
        auto *vl = new QVBoxLayout(hdr);
        vl->setContentsMargins(32, 28, 32, 16);
        vl->setSpacing(6);
        auto *title = new QLabel(tr("Keyboard Shortcuts"), hdr);
        title->setObjectName(QStringLiteral("SettingsSectionTitle"));
        vl->addWidget(title);
        auto *desc = new QLabel(tr("Configure keyboard shortcuts for frequent actions."), hdr);
        desc->setObjectName(QStringLiteral("SettingsSectionDesc"));
        vl->addWidget(desc);
        outer->addWidget(hdr);
    }

    // Search row
    {
        auto *row = new QWidget(page);
        row->setObjectName(QStringLiteral("SettingsScrollContent"));
        auto *hl = new QHBoxLayout(row);
        hl->setContentsMargins(32, 0, 32, 12);
        hl->setSpacing(12);
        auto *searchEdit = new QLineEdit(row);
        searchEdit->setObjectName(QStringLiteral("ShortcutSearch"));
        searchEdit->setPlaceholderText(tr("Search action"));
        searchEdit->setFixedHeight(34);
        hl->addWidget(searchEdit, 1);
        auto *globalChk = new QCheckBox(tr("Show only global default shortcuts"), row);
        globalChk->setObjectName(QStringLiteral("ShortcutGlobalChk"));
        hl->addWidget(globalChk);
        outer->addWidget(row);

        // Search filter wired after m_shortcutRows is populated below
        connect(searchEdit, &QLineEdit::textChanged, this, [this](const QString &text) {
            for (ShortcutRow *r : m_shortcutRows)
                r->setVisible(r->matchesFilter(text));
        });
    }

    // Column headers
    {
        auto *hdr = new QWidget(page);
        hdr->setObjectName(QStringLiteral("ShortcutTableHeader"));
        hdr->setFixedHeight(30);
        auto *hl = new QHBoxLayout(hdr);
        hl->setContentsMargins(16, 0, 16, 0);
        hl->setSpacing(8);
        auto *colAction = new QLabel(tr("Action"), hdr);
        colAction->setObjectName(QStringLiteral("ShortcutColHeader"));
        colAction->setFixedWidth(260);
        hl->addWidget(colAction);
        auto *colKey = new QLabel(tr("Key Combination"), hdr);
        colKey->setObjectName(QStringLiteral("ShortcutColHeader"));
        hl->addWidget(colKey, 1);
        outer->addWidget(hdr);
    }

    // Separator
    {
        auto *sep = new QFrame(page);
        sep->setObjectName(QStringLiteral("SettingsSeparator"));
        sep->setFrameShape(QFrame::HLine);
        outer->addWidget(sep);
    }

    // Scrollable list
    {
        auto *scroll = new QScrollArea(page);
        scroll->setObjectName(QStringLiteral("SettingsScroll"));
        scroll->setFrameShape(QFrame::NoFrame);
        scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        scroll->setWidgetResizable(true);

        auto *listW = new QWidget;
        listW->setObjectName(QStringLiteral("SettingsScrollContent"));
        auto *vl = new QVBoxLayout(listW);
        vl->setContentsMargins(0, 0, 0, 0);
        vl->setSpacing(0);

        struct Def { const char *label; const char *key; const char *seq; };
        static const Def kDefs[] = {
            { QT_TR_NOOP("Save"),          "save",     "Ctrl+S"       },
            { QT_TR_NOOP("Save As"),       "saveas",   "Ctrl+Shift+S" },
            { QT_TR_NOOP("Print"),         "print",    "Ctrl+P"       },
            { QT_TR_NOOP("Open"),          "open",     "Ctrl+O"       },
            { QT_TR_NOOP("Undo"),          "undo",     "Ctrl+Z"       },
            { QT_TR_NOOP("Redo"),          "redo",     "Ctrl+Y"       },
            { QT_TR_NOOP("Find"),          "find",     "Ctrl+F"       },
            { QT_TR_NOOP("Text Tool"),     "texttool", "T"            },
            { QT_TR_NOOP("Add Comment"),   "comment",  "C"            },
            { QT_TR_NOOP("Zoom In"),       "zoomin",       "Ctrl++"       },
            { QT_TR_NOOP("Zoom Out"),      "zoomout",      "Ctrl+-"       },
            { QT_TR_NOOP("Presentation"),  "presentation", "F5"           },
        };

        m_shortcutRows.clear();
        m_shortcutKeys.clear();
        bool first = true;
        for (const auto &def : kDefs) {
            if (!first) {
                auto *sep = new QFrame(listW);
                sep->setObjectName(QStringLiteral("SettingsSeparator"));
                sep->setFrameShape(QFrame::HLine);
                vl->addWidget(sep);
            }
            first = false;

            const QKeySequence appDefault = QKeySequence::fromString(
                QLatin1String(def.seq), QKeySequence::PortableText);
            const QKeySequence saved = m_settings->shortcut(
                QLatin1String(def.key), appDefault);

            auto *srow = new ShortcutRow(tr(def.label), appDefault, listW);
            srow->setCurrentSequence(saved);

            m_shortcutRows.append(srow);
            m_shortcutKeys.append(QLatin1String(def.key));

            connect(srow, &ShortcutRow::editStarted, this, [this, srow]() {
                for (ShortcutRow *r : m_shortcutRows)
                    if (r != srow) r->cancelIfEditing();
            });
            vl->addWidget(srow);
        }
        vl->addStretch(1);
        scroll->setWidget(listW);
        outer->addWidget(scroll, 1);
    }

    // Info bar
    {
        auto *bar = new QFrame(page);
        bar->setObjectName(QStringLiteral("ShortcutInfoBar"));
        bar->setFrameShape(QFrame::NoFrame);
        bar->setFixedHeight(42);
        auto *hl = new QHBoxLayout(bar);
        hl->setContentsMargins(20, 0, 20, 0);
        hl->setSpacing(8);
        auto *icon = new QLabel(QStringLiteral("ℹ"), bar);
        icon->setObjectName(QStringLiteral("ShortcutInfoIcon"));
        hl->addWidget(icon);
        auto *text = new QLabel(tr("Double-click on a key combination to change it."), bar);
        text->setObjectName(QStringLiteral("ShortcutInfoText"));
        hl->addWidget(text, 1);
        outer->addWidget(bar);
    }

    return page;
}

QWidget *SettingsPanel::buildZoomPage()
{
    auto *page = new QWidget(this);
    page->setObjectName(QStringLiteral("SettingsScrollContent"));
    auto *outerLayout = new QVBoxLayout(page);
    outerLayout->setContentsMargins(0, 0, 0, 0);
    outerLayout->setSpacing(0);

    auto *scroll = new QScrollArea(page);
    scroll->setObjectName(QStringLiteral("SettingsScroll"));
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setWidgetResizable(true);

    auto *content = new QWidget;
    content->setObjectName(QStringLiteral("SettingsScrollContent"));
    auto *vl = new QVBoxLayout(content);
    vl->setContentsMargins(32, 28, 32, 32);
    vl->setSpacing(12);

    auto *title = new QLabel(tr("Zoom"), content);
    title->setObjectName(QStringLiteral("SettingsSectionTitle"));
    vl->addWidget(title);
    vl->addSpacing(4);
    auto *desc = new QLabel(tr("Configure how zoom works with the mouse wheel."), content);
    desc->setObjectName(QStringLiteral("SettingsSectionDesc"));
    vl->addWidget(desc);
    vl->addSpacing(16);

    // Helper: single-row card with a toggle switch
    const auto makeToggleCard = [&](const QString &label, bool checked,
                                     QAbstractButton **out) -> QFrame * {
        auto *card = new QFrame(content);
        card->setObjectName(QStringLiteral("ZoomCard"));
        card->setAttribute(Qt::WA_StyledBackground, true);
        auto *hl = new QHBoxLayout(card);
        hl->setContentsMargins(16, 14, 16, 14);
        hl->setSpacing(12);
        auto *lbl = new QLabel(label, card);
        lbl->setObjectName(QStringLiteral("ZoomRowLabel"));
        hl->addWidget(lbl, 1);
        auto *toggle = new ToggleSwitch(card);
        toggle->setChecked(checked);
        hl->addWidget(toggle);
        if (out) *out = toggle;
        return card;
    };

    // ── Card 1: Mouse wheel zoom step ────────────────────────────────────────
    {
        auto *card = new QFrame(content);
        card->setObjectName(QStringLiteral("ZoomCard"));
        card->setAttribute(Qt::WA_StyledBackground, true);
        auto *cl = new QVBoxLayout(card);
        cl->setContentsMargins(16, 16, 16, 16);
        cl->setSpacing(10);

        auto *cardTitle = new QLabel(tr("Mouse Wheel Zoom Step"), card);
        cardTitle->setObjectName(QStringLiteral("ZoomCardTitle"));
        cl->addWidget(cardTitle);

        auto *stepRow = new QHBoxLayout;
        stepRow->setSpacing(12);
        auto *stepLbl = new QLabel(tr("Zoom step with Ctrl + Mouse Wheel"), card);
        stepLbl->setObjectName(QStringLiteral("ZoomRowLabel"));
        stepRow->addWidget(stepLbl, 1);
        m_zoomStepSpin = new QSpinBox(card);
        m_zoomStepSpin->setObjectName(QStringLiteral("ZoomStepSpin"));
        m_zoomStepSpin->setRange(1, 50);
        m_zoomStepSpin->setValue(m_settings->zoomStep());
        m_zoomStepSpin->setSuffix(QStringLiteral(" %"));
        m_zoomStepSpin->setFixedWidth(88);
        stepRow->addWidget(m_zoomStepSpin);
        cl->addLayout(stepRow);

        auto *stepDesc = new QLabel(tr("Sets how much is zoomed per mouse wheel movement."), card);
        stepDesc->setObjectName(QStringLiteral("ZoomRowDesc"));
        cl->addWidget(stepDesc);

        auto *exBox = new QFrame(card);
        exBox->setObjectName(QStringLiteral("ZoomExampleBox"));
        exBox->setAttribute(Qt::WA_StyledBackground, true);
        auto *exRow = new QHBoxLayout(exBox);
        exRow->setContentsMargins(10, 8, 10, 8);
        exRow->setSpacing(6);
        auto *exIcon = new QLabel(QStringLiteral("↗"), exBox);
        exIcon->setObjectName(QStringLiteral("ZoomExampleIcon"));
        exRow->addWidget(exIcon);
        m_zoomExampleLabel = new QLabel(exBox);
        m_zoomExampleLabel->setObjectName(QStringLiteral("ZoomExampleText"));
        exRow->addWidget(m_zoomExampleLabel, 1);
        cl->addWidget(exBox);

        vl->addWidget(card);
    }

    // ── Card 2: Ctrl+Wheel toggle ─────────────────────────────────────────────
    vl->addWidget(makeToggleCard(
        tr("Zoom with Ctrl + Mouse Wheel"),
        m_settings->ctrlWheelZoom(),
        &m_ctrlWheelToggle));

    // ── Card 3: Zoom to pointer toggle ────────────────────────────────────────
    vl->addWidget(makeToggleCard(
        tr("Zoom to Mouse Pointer"),
        m_settings->zoomToPointer(),
        &m_zoomPtrToggle));

    // ── Card 4: Wheel action combo ────────────────────────────────────────────
    {
        auto *card = new QFrame(content);
        card->setObjectName(QStringLiteral("ZoomCard"));
        card->setAttribute(Qt::WA_StyledBackground, true);
        auto *hl = new QHBoxLayout(card);
        hl->setContentsMargins(16, 14, 16, 14);
        hl->setSpacing(12);
        auto *lbl = new QLabel(tr("Action without Ctrl"), card);
        lbl->setObjectName(QStringLiteral("ZoomRowLabel"));
        hl->addWidget(lbl, 1);
        m_wheelActionCombo = new QComboBox(card);
        m_wheelActionCombo->setObjectName(QStringLiteral("ZoomActionCombo"));
        m_wheelActionCombo->addItem(tr("Scroll"), QStringLiteral("scroll"));
        m_wheelActionCombo->addItem(tr("Zoom"),   QStringLiteral("zoom"));
        const QString act = m_settings->wheelAction();
        for (int i = 0; i < m_wheelActionCombo->count(); ++i) {
            if (m_wheelActionCombo->itemData(i).toString() == act) {
                m_wheelActionCombo->setCurrentIndex(i);
                break;
            }
        }
        hl->addWidget(m_wheelActionCombo);
        vl->addWidget(card);
    }

    vl->addStretch(1);
    scroll->setWidget(content);
    outerLayout->addWidget(scroll);

    // Update example label whenever the step changes
    const auto updateExample = [this]() {
        m_zoomExampleLabel->setText(
            tr("Example: 100 % → %1 %").arg(100 + m_zoomStepSpin->value()));
    };
    updateExample();
    connect(m_zoomStepSpin, QOverload<int>::of(&QSpinBox::valueChanged),
            this, updateExample);

    return page;
}

QWidget *SettingsPanel::buildAdvancedPage()
{
    auto *page = new QWidget(this);
    page->setObjectName(QStringLiteral("SettingsScrollContent"));
    auto *vbox = new QVBoxLayout(page);
    vbox->setContentsMargins(32, 28, 32, 32);
    vbox->setSpacing(0);

    auto *title = new QLabel(tr("Advanced"), page);
    title->setObjectName(QStringLiteral("SettingsSectionTitle"));
    vbox->addWidget(title);
    vbox->addSpacing(6);

    auto *desc = new QLabel(tr("Advanced settings for OpenPDF Studio."), page);
    desc->setObjectName(QStringLiteral("SettingsSectionDesc"));
    vbox->addWidget(desc);
    vbox->addSpacing(24);

    // ── Interface ─────────────────────────────────────────────────────────────
    auto *interfaceLabel = new QLabel(tr("INTERFACE"), page);
    interfaceLabel->setObjectName(QStringLiteral("SettingsGroupLabel"));
    vbox->addWidget(interfaceLabel);
    vbox->addSpacing(14);

    m_preserveLayoutCheck = new SettingCheckBox(tr("Preserve panel layout"), page);
    m_preserveLayoutCheck->setChecked(m_settings->preservePanelLayout());
    vbox->addWidget(m_preserveLayoutCheck, 0, Qt::AlignLeft);
    vbox->addSpacing(6);

    auto *preserveDesc = new QLabel(
        tr("Restore expanded and collapsed panels after restart."), page);
    preserveDesc->setObjectName(QStringLiteral("SettingsSectionDesc"));
    vbox->addWidget(preserveDesc);
    // Keep the description under the label, not under the check box.
    preserveDesc->setContentsMargins(SettingCheckBox::kBox + SettingCheckBox::kSpacing,
                                     0, 0, 0);
    vbox->addSpacing(24);

    auto *sep = new QFrame(page);
    sep->setObjectName(QStringLiteral("SettingsSeparator"));
    sep->setFrameShape(QFrame::HLine);
    sep->setFixedHeight(1);
    vbox->addWidget(sep);
    vbox->addSpacing(24);

    // ── Reset ─────────────────────────────────────────────────────────────────
    auto *resetLabel = new QLabel(tr("RESET"), page);
    resetLabel->setObjectName(QStringLiteral("SettingsGroupLabel"));
    vbox->addWidget(resetLabel);
    vbox->addSpacing(14);

    auto *resetAllBtn = new QPushButton(tr("Reset All Settings to Defaults"), page);
    resetAllBtn->setFixedWidth(280);
    resetAllBtn->setStyleSheet(Theme::DarkMode
        ? QStringLiteral(
            "QPushButton { background:#3D1A1A; color:#F87171; border:1px solid #7F1D1D;"
            " border-radius:6px; font-size:13px; padding:8px 16px; }"
            "QPushButton:hover { background:#4D2020; }")
        : QStringLiteral(
            "QPushButton { background:#FEF2F2; color:#DC2626; border:1px solid #FCA5A5;"
            " border-radius:6px; font-size:13px; padding:8px 16px; }"
            "QPushButton:hover { background:#FEE2E2; }"));
    connect(resetAllBtn, &QPushButton::clicked, this, [this]() {
        m_pendingTheme = QStringLiteral("system");
        selectCardGroup(m_pendingTheme, m_themeCards, m_themeIds);
        Q_EMIT themeChangeRequested(m_pendingTheme);
        m_pendingLang = QStringLiteral("en");
        selectLangCode(m_pendingLang);
        if (m_preserveLayoutCheck) m_preserveLayoutCheck->setChecked(true);
    });
    vbox->addWidget(resetAllBtn);
    vbox->addStretch(1);
    return page;
}

QWidget *SettingsPanel::buildAboutPage()
{
    auto *page = new QWidget(this);
    page->setObjectName(QStringLiteral("SettingsScrollContent"));
    auto *vbox = new QVBoxLayout(page);
    vbox->setContentsMargins(32, 32, 32, 32);
    vbox->setSpacing(10);
    vbox->setAlignment(Qt::AlignCenter);

    auto *logo = new QLabel(QStringLiteral("O"), page);
    logo->setObjectName(QStringLiteral("AppLogo"));
    logo->setFixedSize(56, 56);
    logo->setAlignment(Qt::AlignCenter);
    vbox->addWidget(logo, 0, Qt::AlignHCenter);
    vbox->addSpacing(8);

    auto *appName = new QLabel(QStringLiteral("OpenPDF Studio"), page);
    appName->setObjectName(QStringLiteral("SettingsSectionTitle"));
    appName->setAlignment(Qt::AlignCenter);
    vbox->addWidget(appName, 0, Qt::AlignHCenter);

    auto *ver = new QLabel(
        QStringLiteral("Version %1").arg(QLatin1String(APP_VERSION)), page);
    ver->setObjectName(QStringLiteral("SettingsSectionDesc"));
    ver->setAlignment(Qt::AlignCenter);
    vbox->addWidget(ver, 0, Qt::AlignHCenter);
    vbox->addSpacing(16);

    auto *tagline = new QLabel(
        tr("A modern, open-source PDF editor built with Qt."), page);
    tagline->setObjectName(QStringLiteral("SettingsSectionDesc"));
    tagline->setAlignment(Qt::AlignCenter);
    vbox->addWidget(tagline, 0, Qt::AlignHCenter);
    vbox->addSpacing(16);

    // Wo die Einstellungen liegen, ist eine Support-Frage — hier steht die
    // Antwort, markierbar, statt in einer Anleitung, die niemand liest.
    auto *cfg = new QLabel(AppConfig::isPortable()
        ? tr("Configuration (portable): %1").arg(AppConfig::path())
        : tr("Configuration: %1").arg(AppConfig::path()), page);
    cfg->setObjectName(QStringLiteral("SettingsSectionDesc"));
    cfg->setAlignment(Qt::AlignCenter);
    cfg->setWordWrap(true);
    cfg->setTextInteractionFlags(Qt::TextSelectableByMouse);
    vbox->addWidget(cfg, 0, Qt::AlignHCenter);
    return page;
}

// ── Option cards ──────────────────────────────────────────────────────────────

QWidget *SettingsPanel::buildOptionCard(const QString &icon, const QString &title,
                                         const QString &desc,  const QString &id,
                                         QList<QWidget*> &group, QList<QString> &ids,
                                         bool isThemeGroup)
{
    auto *card = new OptionCard(icon, title, desc, this);

    if (isThemeGroup)
        card->setSelected(id == m_pendingTheme);
    else
        card->setSelected(id == QStringLiteral("default"));

    group.append(card);
    ids.append(id);

    connect(card, &OptionCard::clicked, this, [this, id, &group, &ids, isThemeGroup]() {
        selectCardGroup(id, group, ids);
        if (isThemeGroup) {
            m_pendingTheme = id;
            Q_EMIT themeChangeRequested(id);
        }
    });
    return card;
}

void SettingsPanel::selectCardGroup(const QString &id,
                                     QList<QWidget*> &cards, QList<QString> &ids)
{
    for (int i = 0; i < cards.size(); ++i)
        qobject_cast<OptionCard *>(cards[i])->setSelected(ids[i] == id);
}

// ── Language rows ─────────────────────────────────────────────────────────────

void SettingsPanel::addLangRow(QWidget *parent, QVBoxLayout *layout,
                                const QString &code, const QString &display)
{
    auto *row = new LangRow(code, display, parent);
    row->setSelected(code == m_pendingLang);
    m_langRows.append(row);
    m_langCodes.append(code);

    connect(row, &LangRow::clicked, this, [this, code]() {
        selectLangCode(code);
        m_pendingLang = code;
    });
    layout->addWidget(row);
}

void SettingsPanel::selectLangCode(const QString &code)
{
    for (int i = 0; i < m_langRows.size(); ++i)
        qobject_cast<LangRow *>(m_langRows[i])->setSelected(m_langCodes[i] == code);
}

// ── Misc ──────────────────────────────────────────────────────────────────────

void SettingsPanel::applyAndClose()
{
    m_settings->setTheme(m_pendingTheme);
    m_settings->setLanguage(m_pendingLang);

    // Flush any open shortcut edit before saving
    for (ShortcutRow *r : m_shortcutRows) r->cancelIfEditing();

    // Persist all shortcuts and apply them immediately
    for (int i = 0; i < m_shortcutRows.size() && i < m_shortcutKeys.size(); ++i)
        m_settings->setShortcut(m_shortcutKeys[i], m_shortcutRows[i]->currentSequence());

    // Save zoom settings
    if (m_zoomStepSpin) {
        m_settings->setZoomStep(m_zoomStepSpin->value());
        m_settings->setCtrlWheelZoom(m_ctrlWheelToggle && m_ctrlWheelToggle->isChecked());
        m_settings->setZoomToPointer(m_zoomPtrToggle && m_zoomPtrToggle->isChecked());
        if (m_wheelActionCombo)
            m_settings->setWheelAction(m_wheelActionCombo->currentData().toString());
    }

    // Panel layout
    bool panelLayoutChanged = false;
    if (m_preserveLayoutCheck) {
        const bool preserve = m_preserveLayoutCheck->isChecked();
        panelLayoutChanged = (preserve != m_settings->preservePanelLayout());
        m_settings->setPreservePanelLayout(preserve);
    }

    m_settings->sync();

    if (m_pendingLang != m_originalLang)
        Q_EMIT languageChangeRequested(m_pendingLang);

    Q_EMIT shortcutsChanged();      // always reload shortcuts in MainWindow
    Q_EMIT zoomSettingsChanged();   // always reload zoom settings in MainWindow
    // Switching the option on captures the layout that is on screen right now,
    // so the very next start already restores what the user is looking at.
    if (panelLayoutChanged)
        Q_EMIT panelLayoutSettingChanged();

    accept();
}

void SettingsPanel::retranslateUi()
{
    setWindowTitle(tr("Settings - OpenPDF Studio"));

    for (const NavItem &ni : m_navItems)
        ni.textLabel->setText(tr(ni.labelKey));
}

void SettingsPanel::changeEvent(QEvent *e)
{
    if (e->type() == QEvent::LanguageChange)
        retranslateUi();
    else if (e->type() == QEvent::StyleChange)
        refreshThemeColors();
    QDialog::changeEvent(e);
}
