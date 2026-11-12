#include "ui/forms/FormFieldPanel.hpp"

#include "ui/forms/FormFlags.hpp"
#include "ui/theme/Theme.hpp"
#include "ui/widgets/CheckBoxStyle.hpp"

#include "engine/forms/FormScript.hpp"

#include <QApplication>
#include <QMouseEvent>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QEvent>
#include <QFrame>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QLocale>
#include <QPointer>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QTimer>
#include <QVariantAnimation>
#include <QtMath>
#include <QVBoxLayout>

namespace {

QPointer<FormFieldPanel> g_instance;

enum FormatChoice { None, Number, Percent, Date, Time, Zip, Phone, Custom };

const char *const kOperations[] = { "", "SUM", "AVG", "PRD", "MIN", "MAX" };
const char *const kKindIcons[]  = { "type", "square-check", "circle-dot", "square-chevron-down", "list" };

constexpr int kMaxOptionRows = 6;
constexpr int kGhostSize     = 28;

bool isButton(FormSpec::Kind kind) { return kind == FormSpec::Kind::CheckBox || kind == FormSpec::Kind::Radio; }
bool isChoice(FormSpec::Kind kind) { return kind == FormSpec::Kind::ComboBox || kind == FormSpec::Kind::ListBox; }

int separators() { return QLocale().decimalPoint() == QLatin1String(",") ? 2 : 0; }

QString defaultPattern(int choice)
{
    return choice == Time ? QStringLiteral("HH:MM") : QStringLiteral("dd.mm.yyyy");
}

QColor linkColor() { return Theme::DarkMode ? QColor(QStringLiteral("#60A5FA")) : QColor(QStringLiteral("#2563EB")); }
QColor dangerColor() { return Theme::DarkMode ? QColor(QStringLiteral("#F87171")) : QColor(QStringLiteral("#DC2626")); }

QIcon gripIcon()
{
    return Theme::makeIcon(QStringLiteral("grip-vertical"), Theme::IconMuted, Theme::IconMuted, Theme::IconMuted, 14);
}

QLabel *rowLabel(QWidget *parent)
{
    auto *label = new QLabel(parent);
    label->setObjectName(QStringLiteral("FormFieldLabel"));
    return label;
}

QPushButton *linkButton(QWidget *parent)
{
    auto *button = new QPushButton(parent);
    button->setObjectName(QStringLiteral("FormFieldLink"));
    button->setCursor(Qt::PointingHandCursor);
    button->setIconSize(QSize(16, 16));
    return button;
}

}

FormFieldPanel::FormFieldPanel(QWidget *parent)
    : QWidget(parent)
{
    g_instance = this;
    setObjectName(QStringLiteral("FormFieldPanel"));
    setAttribute(Qt::WA_StyledBackground, true);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(20, 22, 20, 18);
    root->setSpacing(14);

    auto *header = new QHBoxLayout;
    m_title = new QLabel(this);
    m_title->setObjectName(QStringLiteral("FormFieldTitle"));
    header->addWidget(m_title);
    header->addStretch();
    m_close = new QPushButton(this);
    m_close->setObjectName(QStringLiteral("FormFieldClose"));
    m_close->setFixedSize(30, 30);
    m_close->setCursor(Qt::PointingHandCursor);
    connect(m_close, &QPushButton::clicked, this, &FormFieldPanel::closeRequested);
    header->addWidget(m_close);
    root->addLayout(header);

    buildKinds(root);

    auto *divider = new QFrame(this);
    divider->setObjectName(QStringLiteral("FormFieldDivider"));
    divider->setFixedHeight(1);
    root->addWidget(divider);

    m_hint = new QLabel(this);
    m_hint->setObjectName(QStringLiteral("FormFieldHint"));
    m_hint->setWordWrap(true);
    root->addWidget(m_hint);

    buildProperties(root);

    m_remove = new QPushButton(this);
    m_remove->setObjectName(QStringLiteral("FormFieldRemove"));
    m_remove->setCursor(Qt::PointingHandCursor);
    m_remove->setIconSize(QSize(16, 16));
    connect(m_remove, &QPushButton::clicked, this, &FormFieldPanel::removeRequested);
    root->addWidget(m_remove, 0, Qt::AlignLeft);

    refreshIcons();
    retranslateUi();
    showField(std::nullopt, 0);
}

void FormFieldPanel::buildKinds(QVBoxLayout *root)
{
    // Every kind is dragged onto the page, or clicked and then placed with a click on the page.
    m_kindList = new QFrame(this);
    m_kindList->setObjectName(QStringLiteral("FormFieldKinds"));
    auto *kindLayout = new QVBoxLayout(m_kindList);
    kindLayout->setContentsMargins(4, 4, 4, 4);
    kindLayout->setSpacing(0);
    for (int i = 0; i <= int(FormSpec::Kind::ListBox); ++i) {
        auto *row = new QPushButton(m_kindList);
        row->setObjectName(QStringLiteral("FormFieldKind"));
        row->setCheckable(true);
        row->setMinimumHeight(34);
        row->setIconSize(QSize(16, 16));
        row->setCursor(Qt::OpenHandCursor);
        row->installEventFilter(this);
        // The grip stands before the kind's icon, where the row is taken hold of; the style sheet
        // keeps the room for it.
        auto *rowLayout = new QHBoxLayout(row);
        rowLayout->setContentsMargins(8, 0, 0, 0);
        auto *grip = new QLabel(row);
        grip->setAttribute(Qt::WA_TransparentForMouseEvents);
        rowLayout->addWidget(grip);
        rowLayout->addStretch();
        m_grips.append(grip);
        connect(row, &QPushButton::clicked, this, [this, i] {
            stopShaking();
            Q_EMIT kindChosen(FormSpec::Kind(i));
        });
        kindLayout->addWidget(row);
        m_kindRows.append(row);
    }
    m_kindList->installEventFilter(this);
    root->addWidget(m_kindList);

    // The kinds rock from side to side, as the note colors do, until the mouse comes over them.
    m_shake = new QVariantAnimation(this);
    m_shake->setStartValue(0.0);
    m_shake->setEndValue(1.0);
    m_shake->setDuration(750);
    connect(m_shake, &QVariantAnimation::valueChanged, this, [this](const QVariant &v) { shake(v.toReal()); });
    connect(m_shake, &QVariantAnimation::finished, this, [this] { shake(-1); });
    m_shakeAgain = new QTimer(this);
    m_shakeAgain->setInterval(2600);
    connect(m_shakeAgain, &QTimer::timeout, this, [this] {
        if (isVisible() && m_shakes) m_shake->start();
    });
}

void FormFieldPanel::buildProperties(QVBoxLayout *root)
{
    auto *scroll = new QScrollArea(this);
    scroll->setObjectName(QStringLiteral("FormFieldScroll"));
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_props = new QWidget(scroll);
    m_props->setObjectName(QStringLiteral("FormFieldProps"));
    auto *props = new QVBoxLayout(m_props);
    props->setContentsMargins(0, 0, 0, 0);
    props->setSpacing(18);
    const auto line = [this]() {
        auto *edit = new QLineEdit(m_props);
        connect(edit, &QLineEdit::editingFinished, this, &FormFieldPanel::edited);
        return edit;
    };
    auto *checkStyle = new CheckBoxStyle;
    checkStyle->setParent(m_props);
    const auto check = [this, checkStyle]() {
        auto *box = new QCheckBox(m_props);
        box->setObjectName(QStringLiteral("FormFieldCheck"));
        box->setStyle(checkStyle);
        box->setMinimumHeight(24);
        connect(box, &QCheckBox::clicked, this, &FormFieldPanel::edited);
        return box;
    };

    m_kind = new QComboBox(m_props);
    m_kind->setIconSize(QSize(16, 16));
    for (const char *icon : kKindIcons) m_kind->addItem(Theme::makeIcon(QString::fromLatin1(icon)), QString());
    connect(m_kind, &QComboBox::activated, this, &FormFieldPanel::edited);
    m_name = line();

    buildOptions();
    m_exportValue = line();
    m_required    = check();
    m_multiline   = check();
    m_format      = new QComboBox(m_props);
    connect(m_format, &QComboBox::activated, this, [this] { showRows(); edited(); });
    m_decimals    = new QSpinBox(m_props);
    m_decimals->setRange(0, 6);
    m_decimals->setValue(2);
    connect(m_decimals, &QSpinBox::editingFinished, this, &FormFieldPanel::edited);
    m_pattern     = line();
    m_calculation = new QComboBox(m_props);
    connect(m_calculation, &QComboBox::activated, this, [this] { showRows(); edited(); });
    m_sources     = line();
    m_tooltip     = line();

    for (QWidget *w : std::initializer_list<QWidget *> { m_kind, m_name, m_optionsBox, m_exportValue })
        addRow(props, w);
    addRow(props, m_required, false);
    addRow(props, m_multiline, false);
    for (QWidget *w : std::initializer_list<QWidget *> {
             m_format, m_decimals, m_pattern, m_calculation, m_sources, m_tooltip })
        addRow(props, w);

    m_note = new QLabel(m_props);
    m_note->setObjectName(QStringLiteral("FormFieldNote"));
    m_note->setWordWrap(true);
    props->addWidget(m_note);

    m_addTooltip = linkButton(m_props);
    connect(m_addTooltip, &QPushButton::clicked, this, [this] {
        m_tooltipOpen = true;
        showRows();
        m_tooltip->setFocus();
    });
    props->addWidget(m_addTooltip, 0, Qt::AlignLeft);
    m_more = linkButton(m_props);
    m_more->setCheckable(true);
    connect(m_more, &QPushButton::toggled, this, [this] {
        refreshIcons();
        m_more->setText(m_more->isChecked() ? tr("Fewer settings") : tr("More settings"));
        showRows();
    });
    props->addWidget(m_more, 0, Qt::AlignLeft);

    m_readOnly = check();
    m_editable = check();
    m_fontSize = new QDoubleSpinBox(m_props);
    m_fontSize->setRange(0, 72);
    m_fontSize->setDecimals(1);
    connect(m_fontSize, &QDoubleSpinBox::editingFinished, this, &FormFieldPanel::edited);
    m_minimum = line();
    m_maximum = line();
    addRow(props, m_readOnly, false);
    addRow(props, m_editable, false);
    for (QWidget *w : std::initializer_list<QWidget *> { m_fontSize, m_minimum, m_maximum })
        addRow(props, w);
    props->addStretch();
    scroll->setWidget(m_props);
    root->addWidget(scroll, 1);
}

void FormFieldPanel::buildOptions()
{
    m_optionsBox = new QWidget(m_props);
    auto *optionsLayout = new QVBoxLayout(m_optionsBox);
    optionsLayout->setContentsMargins(0, 0, 0, 0);
    optionsLayout->setSpacing(10);
    m_options = new QListWidget(m_optionsBox);
    m_options->setObjectName(QStringLiteral("FormFieldOptions"));
    m_options->setDragDropMode(QAbstractItemView::InternalMove);
    m_options->setDefaultDropAction(Qt::MoveAction);
    m_options->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed
                               | QAbstractItemView::SelectedClicked);
    m_options->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_options->setUniformItemSizes(true);
    m_options->setIconSize(QSize(14, 14));
    m_options->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_options->installEventFilter(this);
    // The list is rebuilt from the committed field, so it must not happen inside its own edit or drop.
    connect(m_options, &QListWidget::itemChanged, this, [this] {
        if (!m_filling) QTimer::singleShot(0, this, &FormFieldPanel::edited);
    });
    connect(m_options->model(), &QAbstractItemModel::rowsMoved, this, [this] {
        if (!m_filling) QTimer::singleShot(0, this, &FormFieldPanel::edited);
    });
    optionsLayout->addWidget(m_options);
    m_addOption = new QPushButton(m_optionsBox);
    m_addOption->setObjectName(QStringLiteral("FormFieldSecondary"));
    m_addOption->setCursor(Qt::PointingHandCursor);
    connect(m_addOption, &QPushButton::clicked, this, &FormFieldPanel::addOption);
    optionsLayout->addWidget(m_addOption, 0, Qt::AlignLeft);
}

FormFieldPanel::~FormFieldPanel() = default;

FormFieldPanel *FormFieldPanel::instance()
{
    return g_instance.data();
}

void FormFieldPanel::setPlacement(std::optional<FormSpec::Kind> kind)
{
    for (int i = 0; i < m_kindRows.size(); ++i) m_kindRows.at(i)->setChecked(kind == FormSpec::Kind(i));
    if (m_placement == kind) return;
    m_placement = kind;
    showRows();
}

void FormFieldPanel::setGhostShown(bool shown)
{
    if (m_ghost) m_ghost->setVisible(shown);
}

bool FormFieldPanel::kindMouse(int row, QEvent *event)
{
    auto *mouse = static_cast<QMouseEvent *>(event);
    const QPoint global = mouse->globalPosition().toPoint();
    const auto kind = FormSpec::Kind(row);
    switch (event->type()) {
    case QEvent::MouseButtonPress:
        if (mouse->button() == Qt::LeftButton) {
            m_dragRow     = row;
            m_pressGlobal = global;
        }
        return false;
    case QEvent::MouseMove:
        if (m_dragRow != row || !(mouse->buttons() & Qt::LeftButton)) return false;
        if (!m_ghost) {
            if ((global - m_pressGlobal).manhattanLength() < QApplication::startDragDistance()) return false;
            stopShaking();
            m_ghost = new QLabel(window());
            m_ghost->setObjectName(QStringLiteral("FormFieldGhost"));
            m_ghost->setAttribute(Qt::WA_TransparentForMouseEvents);
            m_ghost->setAlignment(Qt::AlignCenter);
            m_ghost->setFixedSize(kGhostSize, kGhostSize);
            m_ghost->setPixmap(Theme::renderSvg(QString::fromLatin1(kKindIcons[row]), Qt::white, 16,
                                                devicePixelRatioF()));
            m_ghost->show();
            m_kindRows.at(row)->setDown(false);
            QApplication::setOverrideCursor(Qt::ClosedHandCursor);
        }
        m_ghost->move(m_ghost->parentWidget()->mapFromGlobal(global) - QPoint(kGhostSize / 2, kGhostSize / 2));
        m_ghost->raise();
        Q_EMIT kindDragged(kind, global);
        return true;
    case QEvent::MouseButtonRelease:
        if (mouse->button() != Qt::LeftButton || m_dragRow != row) return false;
        m_dragRow = -1;
        if (!m_ghost) return false;
        m_ghost->hide();
        m_ghost->deleteLater();
        QApplication::restoreOverrideCursor();
        m_kindRows.at(row)->setDown(false);
        Q_EMIT kindDropped(kind, global);
        return true;
    default:
        return false;
    }
}

void FormFieldPanel::showField(const std::optional<FormSpec> &spec, int place)
{
    if (!spec || !m_spec || spec->name != m_spec->name) m_tooltipOpen = false;
    m_spec  = spec;
    m_place = place;
    m_filling = true;
    if (spec) {
        m_kind->setCurrentIndex(int(spec->kind));
        m_kind->setEnabled(spec->isNew() && spec->places.size() == 1);
        m_name->setText(spec->name);
        m_tooltip->setText(spec->tooltip);
        m_required->setChecked(spec->flags & FormFlags::Required);
        m_readOnly->setChecked(spec->flags & FormFlags::ReadOnly);
        m_multiline->setChecked(spec->flags & FormFlags::Multiline);
        m_editable->setChecked(spec->flags & FormFlags::Editable);
        m_options->clear();
        for (const QString &option : spec->options) {
            auto *item = new QListWidgetItem(gripIcon(), option, m_options);
            item->setFlags(item->flags() | Qt::ItemIsEditable | Qt::ItemIsDragEnabled);
        }
        m_exportValue->setText(spec->places.value(place).exportValue);
        m_fontSize->setValue(spec->fontSize);

        // Scripts the panel cannot express stay as the file has them.
        const FormScript::Format f = FormScript::formatOf(spec->keystroke, spec->format);
        int choice = None;
        switch (f.kind) {
        case FormScript::Format::Kind::Number:  choice = Number; break;
        case FormScript::Format::Kind::Percent: choice = Percent; break;
        case FormScript::Format::Kind::Date:    choice = Date; break;
        case FormScript::Format::Kind::Time:    choice = Time; break;
        case FormScript::Format::Kind::Special: choice = f.special == 2 ? Phone : Zip; break;
        case FormScript::Format::Kind::None:
            choice = spec->keystroke.isEmpty() && spec->format.isEmpty() ? None : Custom;
            break;
        }
        m_format->setCurrentIndex(choice);
        m_decimals->setValue(f.decimals);
        m_pattern->setText(f.pattern);

        const auto range = FormScript::rangeOf(spec->validate);
        const QLocale locale;
        m_minimum->setText(range && range->min ? locale.toString(*range->min, 'g', 12) : QString());
        m_maximum->setText(range && range->max ? locale.toString(*range->max, 'g', 12) : QString());
        m_minimum->setEnabled(spec->validate.isEmpty() || range);
        m_maximum->setEnabled(spec->validate.isEmpty() || range);

        const auto simple = FormScript::simpleOf(spec->calculate);
        int operation = 0;
        if (simple)
            for (int i = 1; i < int(std::size(kOperations)); ++i)
                if (simple->operation == QLatin1String(kOperations[i])) operation = i;
        const bool customCalculation = !spec->calculate.isEmpty() && !operation;
        m_calculation->setCurrentIndex(customCalculation ? int(std::size(kOperations)) : operation);
        m_sources->setText(simple ? simple->fields.join(QStringLiteral(", ")) : QString());
    }
    m_filling = false;
    fitOptions();
    showRows();
}

void FormFieldPanel::showRows()
{
    const bool picked = m_spec.has_value() && !m_placement;
    const bool more = picked && m_more->isChecked();
    const FormSpec::Kind kind = picked ? m_spec->kind : FormSpec::Kind::Text;
    const bool text = picked && kind == FormSpec::Kind::Text;
    const int format = m_format->currentIndex();

    if (m_placement)
        m_hint->setText(tr("Click or drag on the page to place the %1. Esc cancels.")
                            .arg(m_kind->itemText(int(*m_placement))));
    else
        m_hint->setText(tr("Drag a field onto the page, or draw one on the page. Click a field "
                           "to change it, drag it to move it, drag a corner to resize it, Delete removes it."));
    m_hint->setVisible(!picked);
    m_props->setVisible(picked);
    m_remove->setVisible(picked);

    const auto row = [this](QWidget *w, bool shown) { setRowVisible(w, shown); };
    row(m_kind, picked);
    row(m_name, picked);
    row(m_optionsBox, picked && isChoice(kind));
    row(m_exportValue, picked && isButton(kind));
    row(m_required, picked);
    row(m_multiline, text);
    row(m_format, text);
    row(m_decimals, text && (format == Number || format == Percent));
    row(m_pattern, text && (format == Date || format == Time));
    row(m_calculation, text);
    row(m_sources, text && m_calculation->currentIndex() > 0
                        && m_calculation->currentIndex() < int(std::size(kOperations)));
    const bool tooltip = picked && (m_tooltipOpen || !m_spec->tooltip.isEmpty());
    row(m_tooltip, tooltip);
    m_note->setVisible(text && (format == Custom || m_calculation->currentIndex() == int(std::size(kOperations))));
    m_addTooltip->setVisible(picked && !tooltip);

    row(m_readOnly, more);
    row(m_editable, more && kind == FormSpec::Kind::ComboBox);
    row(m_fontSize, more && !isButton(kind));
    const bool range = more && text && (format == None || format == Number || format == Percent || format == Custom);
    row(m_minimum, range);
    row(m_maximum, range);
}

void FormFieldPanel::addRow(QVBoxLayout *layout, QWidget *field, bool labelled)
{
    auto *row = new QWidget(m_props);
    auto *rowLayout = new QVBoxLayout(row);
    rowLayout->setContentsMargins(0, 0, 0, 0);
    rowLayout->setSpacing(6);
    if (labelled) rowLayout->addWidget(rowLabel(row));
    field->setParent(row);
    rowLayout->addWidget(field);
    layout->addWidget(row);
    m_rows.insert(field, row);
}

void FormFieldPanel::setRowVisible(QWidget *field, bool shown)
{
    if (QWidget *row = m_rows.value(field)) row->setVisible(shown);
}

void FormFieldPanel::setRowLabel(QWidget *field, const QString &text)
{
    if (QWidget *row = m_rows.value(field))
        if (auto *label = row->findChild<QLabel *>(QStringLiteral("FormFieldLabel"), Qt::FindDirectChildrenOnly))
            label->setText(text);
}

void FormFieldPanel::addOption()
{
    if (!m_spec) return;
    QStringList taken;
    for (int i = 0; i < m_options->count(); ++i) taken << m_options->item(i)->text();
    int n = m_options->count() + 1;
    while (taken.contains(tr("Option %1").arg(n))) ++n;
    m_filling = true;
    m_options->addItem(tr("Option %1").arg(n));
    m_filling = false;
    edited();
    const int last = m_options->count() - 1;
    if (last < 0) return;
    m_options->setCurrentRow(last);
    m_options->scrollToBottom();
    m_options->setFocus();
    m_options->editItem(m_options->item(last));
}

void FormFieldPanel::fitOptions()
{
    m_options->ensurePolished();
    const int rowHeight = m_options->count() ? m_options->sizeHintForRow(0) : m_options->fontMetrics().height() + 14;
    const int rows = qBound(1, int(m_options->count()), kMaxOptionRows);
    m_options->setFixedHeight(rows * rowHeight + 2 * m_options->frameWidth());
}

void FormFieldPanel::edited()
{
    if (m_filling || !m_spec) return;
    const FormSpec spec = gathered();
    if (spec != *m_spec) Q_EMIT specEdited(spec);
}

FormSpec FormFieldPanel::gathered() const
{
    FormSpec s = *m_spec;
    s.name    = m_name->text().trimmed();
    s.tooltip = m_tooltip->text().trimmed();
    if (m_kind->isEnabled() && m_kind->currentIndex() != int(s.kind)) {
        // The other rows describe the old kind; a new kind starts from its own defaults.
        s.kind = FormSpec::Kind(m_kind->currentIndex());
        s.flags &= ~(FormFlags::Multiline | FormFlags::Password | FormFlags::Editable | FormFlags::KindBits);
        s.keystroke.clear();
        s.format.clear();
        s.validate.clear();
        s.calculate.clear();
        if (!isChoice(s.kind)) s.options.clear();
        else if (s.options.isEmpty()) s.options = { tr("Option 1"), tr("Option 2"), tr("Option 3") };
        for (FormSpec::Place &p : s.places)
            p.exportValue = s.kind == FormSpec::Kind::CheckBox ? QStringLiteral("Yes")
                          : s.kind == FormSpec::Kind::Radio    ? QStringLiteral("Choice1") : QString();
        return s;
    }
    const auto flag = [&s](int bit, bool on) { s.flags = on ? (s.flags | bit) : (s.flags & ~bit); };
    flag(FormFlags::Required, m_required->isChecked());
    flag(FormFlags::ReadOnly, m_readOnly->isChecked());
    if (s.kind == FormSpec::Kind::Text)     flag(FormFlags::Multiline, m_multiline->isChecked());
    if (s.kind == FormSpec::Kind::ComboBox) flag(FormFlags::Editable, m_editable->isChecked());
    if (isChoice(s.kind)) {
        s.options.clear();
        for (int i = 0; i < m_options->count(); ++i)
            if (const QString o = m_options->item(i)->text().trimmed(); !o.isEmpty()) s.options << o;
    }
    if (isButton(s.kind) && m_place >= 0 && m_place < s.places.size() && !m_exportValue->text().trimmed().isEmpty())
        s.places[m_place].exportValue = m_exportValue->text().trimmed();
    if (!isButton(s.kind)) s.fontSize = m_fontSize->value();
    if (s.kind != FormSpec::Kind::Text) return s;

    const int choice = m_format->currentIndex();
    if (choice != Custom) {
        FormScript::Format f;
        f.decimals   = m_decimals->value();
        f.separators = separators();
        f.pattern    = m_pattern->text().trimmed().isEmpty() ? defaultPattern(choice) : m_pattern->text().trimmed();
        switch (choice) {
        case Number:  f.kind = FormScript::Format::Kind::Number; break;
        case Percent: f.kind = FormScript::Format::Kind::Percent; break;
        case Date:    f.kind = FormScript::Format::Kind::Date; break;
        case Time:    f.kind = FormScript::Format::Kind::Time; break;
        case Zip:     f.kind = FormScript::Format::Kind::Special; f.special = 0; break;
        case Phone:   f.kind = FormScript::Format::Kind::Special; f.special = 2; break;
        default:      break;
        }
        s.keystroke = FormScript::keystrokeScript(f);
        s.format    = FormScript::formatScript(f);
    }
    if (m_minimum->isEnabled()) {
        FormScript::Range range;
        range.min = FormScript::number(m_minimum->text());
        range.max = FormScript::number(m_maximum->text());
        s.validate = FormScript::rangeScript(range);
    }
    const int operation = m_calculation->currentIndex();
    if (operation < int(std::size(kOperations))) {
        QStringList fields;
        for (const QString &f : m_sources->text().split(u','))
            if (!f.trimmed().isEmpty()) fields << f.trimmed();
        s.calculate = FormScript::calculationScript({ QLatin1String(kOperations[operation]), fields });
    }
    return s;
}

void FormFieldPanel::shake(qreal progress)
{
    const qreal shift = progress < 0 ? 0.0 : 3.5 * (1.0 - progress) * qSin(progress * 2 * M_PI * 3);
    // A row's item reports where the row is now, so its place at rest comes from the layout.
    const int rest = m_kindList->layout()->contentsRect().x();
    for (QPushButton *row : std::as_const(m_kindRows)) row->move(rest + qRound(shift), row->y());
}

void FormFieldPanel::stopShaking()
{
    m_shakeAgain->stop();
    m_shake->stop();
    shake(-1);
}

void FormFieldPanel::setHint(bool shake, bool repeat)
{
    m_shakes       = shake;
    m_shakeRepeats = repeat;
    m_shakeShown   = false;
    if (!shake) stopShaking();
}

void FormFieldPanel::setDocumentAvailable(bool available)
{
    // Without a document there is no page to put a field on, so the kinds are greyed out.
    if (available == m_kindList->isEnabled()) return;
    m_kindList->setEnabled(available);
    for (QPushButton *row : std::as_const(m_kindRows)) row->setCursor(available ? Qt::OpenHandCursor : Qt::ArrowCursor);
    if (!available) stopShaking();
    else if (isVisible()) startShaking();
}

void FormFieldPanel::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    startShaking();
}

void FormFieldPanel::startShaking()
{
    // As set under Interactive UI: never, the first time only, or every time the panel opens.
    if (!m_shakes || !m_kindList->isEnabled()) return;
    QTimer::singleShot(400, m_shake, [this] { if (isVisible() && m_shakes) m_shake->start(); });
    m_shakeAgain->start();
    if (!m_shakeRepeats && !m_shakeShown) {
        m_shakeShown = true;
        Q_EMIT kindHintSeen();
    }
}

void FormFieldPanel::hideEvent(QHideEvent *event)
{
    stopShaking();
    if (!m_shakeRepeats && m_shakeShown) m_shakes = false;
    QWidget::hideEvent(event);
}

bool FormFieldPanel::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_kindList && event->type() == QEvent::Enter) stopShaking();
    // Filters see mouse events even on disabled widgets.
    if (const int row = int(m_kindRows.indexOf(qobject_cast<QPushButton *>(watched))); row >= 0 && m_kindList->isEnabled()
            && (event->type() == QEvent::MouseButtonPress || event->type() == QEvent::MouseMove
                || event->type() == QEvent::MouseButtonRelease))
        return kindMouse(row, event);
    // While an option is renamed its keys go to the editor, not to the list.
    if (watched == m_options && event->type() == QEvent::KeyPress) {
        const int key = static_cast<QKeyEvent *>(event)->key();
        if ((key == Qt::Key_Delete || key == Qt::Key_Backspace) && m_options->currentItem()) {
            m_filling = true;
            delete m_options->currentItem();
            m_filling = false;
            QTimer::singleShot(0, this, &FormFieldPanel::edited);
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}

void FormFieldPanel::refreshIcons()
{
    m_close->setIcon(Theme::makeIcon(QStringLiteral("x"), Theme::IconMuted));
    m_addTooltip->setIcon(Theme::makeIcon(QStringLiteral("plus"), linkColor(), linkColor()));
    m_more->setIcon(Theme::makeIcon(m_more->isChecked() ? QStringLiteral("chevron-up") : QStringLiteral("chevron-down"),
                                    linkColor(), linkColor()));
    m_remove->setIcon(Theme::makeIcon(QStringLiteral("trash-2"), dangerColor(), dangerColor()));
    for (int i = 0; i < m_options->count(); ++i) m_options->item(i)->setIcon(gripIcon());
    for (int i = 0; i < m_kindRows.size(); ++i) {
        const QIcon icon = Theme::makeIcon(QString::fromLatin1(kKindIcons[i]));
        m_kindRows.at(i)->setIcon(icon);
        m_grips.at(i)->setPixmap(gripIcon().pixmap(QSize(14, 14), devicePixelRatioF()));
        m_kind->setItemIcon(i, icon);
    }
}

void FormFieldPanel::retranslateUi()
{
    m_title->setText(tr("Form fields"));
    m_close->setToolTip(tr("Close"));
    const QString kinds[] = { tr("Text field"), tr("Check box"), tr("Radio button"),
                              tr("Drop-down"), tr("List box") };
    for (int i = 0; i < int(std::size(kinds)); ++i) {
        m_kindRows.at(i)->setText(kinds[i]);
        m_kindRows.at(i)->setToolTip(tr("Drag onto the page, or click and then click on the page"));
        m_kind->setItemText(i, kinds[i]);
    }

    setRowLabel(m_kind, tr("Type"));
    setRowLabel(m_name, tr("Name"));
    setRowLabel(m_optionsBox, tr("Content"));
    setRowLabel(m_exportValue, tr("Value when on"));
    setRowLabel(m_format, tr("Format"));
    setRowLabel(m_decimals, tr("Decimals"));
    setRowLabel(m_pattern, tr("Pattern"));
    setRowLabel(m_calculation, tr("Calculation"));
    setRowLabel(m_sources, tr("Of the fields"));
    setRowLabel(m_tooltip, tr("Tooltip"));
    setRowLabel(m_fontSize, tr("Font size"));
    setRowLabel(m_minimum, tr("Minimum"));
    setRowLabel(m_maximum, tr("Maximum"));
    m_kind->setToolTip(tr("Only a new field can change its type."));
    m_options->setToolTip(tr("Double-click to rename, drag to sort, Delete removes an option."));
    m_addOption->setText(tr("Add option"));
    m_required->setText(tr("Required"));
    m_multiline->setText(tr("Several lines"));
    m_readOnly->setText(tr("Read-only"));
    m_editable->setText(tr("Own entries allowed"));
    m_fontSize->setSpecialValueText(tr("Automatic"));
    m_sources->setPlaceholderText(tr("Names, separated by commas"));
    m_tooltip->setPlaceholderText(tr("Shown when the pointer rests on the field"));
    m_note->setText(tr("This field keeps its own script from the file."));
    m_addTooltip->setText(tr("Add tooltip"));
    m_more->setText(m_more->isChecked() ? tr("Fewer settings") : tr("More settings"));
    m_remove->setText(tr("Remove field"));

    const int format = m_format->currentIndex(), calculation = m_calculation->currentIndex();
    m_format->clear();
    m_format->addItems({ tr("None"), tr("Number"), tr("Percent"), tr("Date"), tr("Time"),
                         tr("ZIP code"), tr("Phone number"), tr("Script from the file") });
    m_format->setCurrentIndex(qMax(0, format));
    m_calculation->clear();
    m_calculation->addItems({ tr("None"), tr("Sum"), tr("Average"), tr("Product"), tr("Minimum"),
                              tr("Maximum"), tr("Script from the file") });
    m_calculation->setCurrentIndex(qMax(0, calculation));
    if (m_spec) showField(m_spec, m_place);
    else        showRows();
}

void FormFieldPanel::changeEvent(QEvent *event)
{
    if (event->type() == QEvent::LanguageChange) retranslateUi();
    if ((event->type() == QEvent::StyleChange || event->type() == QEvent::PaletteChange) && m_remove) refreshIcons();
    QWidget::changeEvent(event);
}
