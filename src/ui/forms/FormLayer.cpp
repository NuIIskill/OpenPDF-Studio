#include "ui/forms/FormLayer.hpp"

#include "ui/DocumentView.hpp"
#include "ui/forms/FormBox.hpp"
#include "ui/forms/FormDesigner.hpp"
#include "ui/forms/FormFlags.hpp"

#include "engine/edit/EditSession.hpp"
#include "engine/forms/FormFieldWriter.hpp"

#include <QDataStream>
#include <QElapsedTimer>
#include <QLabel>
#include <QSet>
#include <QTimer>

namespace {

const QString kStateKey = QStringLiteral("forms");

constexpr quint32 kStateVersion = 2;
constexpr int     kReadBudgetMs = 30;

}

FormLayer::FormLayer(DocumentView *view, QObject *parent)
    : QObject(parent)
    , m_view(view)
    , m_designer(new FormDesigner(this, view, this))
{}

FormLayer::~FormLayer()
{
    closeEditor();
    for (const QPointer<FormBox> &box : std::as_const(m_boxes)) delete box.data();
}

void FormLayer::setSource(PdfBackend *backend, EditSession *session)
{
    m_backend = backend;
    m_session = session;
}

void FormLayer::setDocument(const QString &path)
{
    closeEditor();
    ++m_generation;
#ifdef HAVE_PDF_RENDERING
    m_fileWidgets.clear();
    m_fileOriginals.clear();
#endif
    m_values.clear();
    m_specs.clear();
    rebuild();
    syncSession();
    m_documentPath = path;
    m_nextPage     = path.isEmpty() || !m_backend ? -1 : 0;
    if (m_nextPage < 0) return;
    // The pages are laid out only after the document is set.
    QTimer::singleShot(0, this, [this, generation = m_generation] {
        if (generation == m_generation) readPages(kReadBudgetMs);
    });
}

void FormLayer::setActiveTool(const QString &toolId)
{
    m_tool = toolId;
    if (!fillable()) closeEditor();
    if (toolId == QLatin1String("form")) readPages(-1);
    m_designer->setActive(toolId == QLatin1String("form") && FormFieldWriter::available());
    relayout();
}

void FormLayer::relayout()
{
#ifdef HAVE_PDF_RENDERING
    const bool shown = fillable() && m_view && m_view->pageCount() > 0
                    && m_view->viewMode() == DocumentView::ViewMode::Single;
    const qreal scale = m_view ? m_view->screenScale() : 1.0;
    for (int i = 0; i < m_boxes.size(); ++i) {
        FormBox *box = m_boxes.at(i);
        if (!box) continue;
        const Widget &w = m_widgets.at(i);
        const QLabel *label = shown ? m_view->pageLabel(w.page) : nullptr;
        if (!label) {
            box->hide();
            continue;
        }
        box->setGeometry(QRectF(w.bounds.topLeft() * scale + QPointF(label->pos()),
                                w.bounds.size() * scale).toAlignedRect());
        box->show();
        box->raise();
    }
    m_designer->relayout();
    if (!m_editor) return;
    FormBox *box = m_boxes.value(m_editing);
    if (!box || !box->isVisible()) {
        closeEditor();
        return;
    }
    layoutEditor();
    m_editor->raise();
#endif
}

QString FormLayer::stateKey() const
{
    return kStateKey;
}

QByteArray FormLayer::state() const
{
    if (m_values.isEmpty() && m_specs.isEmpty()) return {};
    QByteArray bytes;
    QDataStream out(&bytes, QIODevice::WriteOnly);
    out << kStateVersion << m_values << m_specs;
    return bytes;
}

void FormLayer::restoreState(const QByteArray &state)
{
    QMap<QString, QString> values;
    QList<FormSpec> specs;
    if (!state.isEmpty()) {
        QDataStream in(state);
        quint32 version = 0;
        in >> version >> values;
        if (version >= 2) in >> specs;
        if (version < 1 || version > kStateVersion || in.status() != QDataStream::Ok) return;
    }
    if (values == m_values && specs == m_specs) return;
    closeEditor();
    readPages(-1);
    QList<int> pages;
    for (const FormSpec &spec : std::as_const(m_specs)) pages << spec.pages();
    for (const FormSpec &spec : std::as_const(specs)) pages << spec.pages();
    const QMap<QString, QString> before = m_values;
    m_values = values;
    m_specs  = specs;
    rebuild();
    applyChange(before, pages);
}

void FormLayer::readPages(int budgetMs)
{
#ifdef HAVE_PDF_RENDERING
    if (m_nextPage < 0 || !m_backend) return;
    if (m_nextPage == 0 && !m_backend->hasForms()) {
        m_nextPage = -1;
        return;
    }
    QElapsedTimer clock;
    clock.start();
    const int count = m_backend->pageCount();
    while (m_nextPage < count) {
        for (const Widget &w : m_backend->pageForms(m_nextPage)) {
            if (w.name.isEmpty() || w.kind == Widget::Kind::Button || w.kind == Widget::Kind::Signature)
                continue;
            m_fileWidgets.append(w);
            QString &original = m_fileOriginals[w.name];
            switch (w.kind) {
            case Widget::Kind::CheckBox:
            case Widget::Kind::Radio:
                if (w.checked && !w.exportValue.isEmpty()) original = w.exportValue;
                else if (original.isEmpty()) original = FormFlags::Off;
                break;
            case Widget::Kind::ComboBox:
            case Widget::Kind::ListBox:
                original = w.selected >= 0 ? w.options.value(w.selected) : w.value;
                break;
            default:
                original = w.value;
                break;
            }
        }
        ++m_nextPage;
        if (budgetMs >= 0 && clock.elapsed() >= budgetMs && m_nextPage < count) {
            QTimer::singleShot(0, this, [this, generation = m_generation] {
                if (generation == m_generation) readPages(kReadBudgetMs);
            });
            rebuild();
            return;
        }
    }
    m_nextPage = -1;
    rebuild();
#else
    Q_UNUSED(budgetMs)
#endif
}

void FormLayer::rebuild()
{
    closeEditor();
    // A box may be rebuilt from inside its own mouse event, so it goes once that event is done.
    for (const QPointer<FormBox> &box : std::as_const(m_boxes))
        if (box) {
            box->hide();
            box->deleteLater();
        }
    m_boxes.clear();
    m_order.clear();
#ifdef HAVE_PDF_RENDERING
    m_widgets.clear();
    m_fields.clear();
    QSet<QString> designed;
    for (const FormSpec &spec : std::as_const(m_specs))
        if (!spec.isNew()) designed.insert(spec.original);
    for (const Widget &w : std::as_const(m_fileWidgets))
        if (!designed.contains(w.name)) addWidget(w, m_fileOriginals.value(w.name));

    for (const FormSpec &spec : std::as_const(m_specs)) {
        if (spec.removed) continue;
        const bool button = spec.kind == FormSpec::Kind::CheckBox || spec.kind == FormSpec::Kind::Radio;
        const QString original = spec.isNew() ? (button ? FormFlags::Off : QString())
                                              : m_fileOriginals.value(spec.original);
        // What the design tool does not set stays as the file has it.
        Widget inFile;
        for (const Widget &file : std::as_const(m_fileWidgets))
            if (!spec.isNew() && file.name == spec.original) {
                inFile = file;
                break;
            }
        for (const FormSpec::Place &place : spec.places) {
            Widget w;
            w.alignment   = inFile.alignment;
            w.textColor   = inFile.textColor;
            w.name        = spec.name;
            w.page        = place.page;
            w.bounds      = place.bounds;
            w.exportValue = place.exportValue.isEmpty() && button ? QStringLiteral("Yes") : place.exportValue;
            w.flags       = spec.flags;
            w.options     = spec.options;
            w.fontSize    = spec.fontSize;
            w.tooltip     = spec.tooltip;
            w.keystroke   = spec.keystroke;
            w.format      = spec.format;
            w.validate    = spec.validate;
            w.calculate   = spec.calculate;
            switch (spec.kind) {
            case FormSpec::Kind::Text:     w.kind = Widget::Kind::Text; break;
            case FormSpec::Kind::CheckBox: w.kind = Widget::Kind::CheckBox; break;
            case FormSpec::Kind::Radio:    w.kind = Widget::Kind::Radio; break;
            case FormSpec::Kind::ComboBox: w.kind = Widget::Kind::ComboBox; break;
            case FormSpec::Kind::ListBox:  w.kind = Widget::Kind::ListBox; break;
            }
            addWidget(w, original);
        }
    }
#endif
    m_designer->rebuild();
    relayout();
}

#ifdef HAVE_PDF_RENDERING
void FormLayer::addWidget(const Widget &w, const QString &original)
{
    auto field = m_fields.find(w.name);
    if (field == m_fields.end()) {
        Field f;
        f.kind         = w.kind;
        f.flags        = w.flags;
        f.original     = original;
        f.options      = w.options;
        f.tooltip      = w.tooltip;
        f.fontSize     = w.fontSize;
        f.keystroke    = w.keystroke;
        f.formatScript = w.format;
        f.validate     = w.validate;
        f.calculate    = w.calculate;
        f.format       = FormScript::formatOf(w.keystroke, w.format);
        f.range        = FormScript::rangeOf(w.validate);
        field = m_fields.insert(w.name, f);
        m_order.append(w.name);
    }
    if (!field->pages.contains(w.page)) field->pages.append(w.page);

    const int index = int(m_widgets.size());
    m_widgets.append(w);
    QWidget *canvas = m_view ? m_view->canvasWidget() : nullptr;
    m_boxes.append(canvas && !(w.flags & FormFlags::ReadOnly) ? new FormBox(this, index, canvas) : nullptr);
}
#endif

QList<FormLayer::Place> FormLayer::places() const
{
    QList<Place> out;
#ifdef HAVE_PDF_RENDERING
    QHash<QString, int> seen;
    for (const Widget &w : m_widgets) {
        Place p;
        p.field  = w.name;
        p.index  = seen[w.name]++;
        p.page   = w.page;
        p.bounds = w.bounds;
        switch (w.kind) {
        case Widget::Kind::CheckBox: p.kind = FormSpec::Kind::CheckBox; break;
        case Widget::Kind::Radio:    p.kind = FormSpec::Kind::Radio; break;
        case Widget::Kind::ComboBox: p.kind = FormSpec::Kind::ComboBox; break;
        case Widget::Kind::ListBox:  p.kind = FormSpec::Kind::ListBox; break;
        default:                     p.kind = FormSpec::Kind::Text; break;
        }
        out.append(p);
    }
#endif
    return out;
}

FormSpec FormLayer::fileSpec(const QString &field) const
{
    FormSpec spec;
#ifdef HAVE_PDF_RENDERING
    for (const Widget &w : m_fileWidgets) {
        if (w.name != field) continue;
        if (spec.original.isEmpty()) {
            spec.original  = field;
            spec.name      = field;
            spec.flags     = w.flags & ~FormFlags::KindBits;
            spec.tooltip   = w.tooltip;
            spec.options   = w.options;
            spec.fontSize  = w.fontSize;
            spec.keystroke = w.keystroke;
            spec.format    = w.format;
            spec.validate  = w.validate;
            spec.calculate = w.calculate;
            switch (w.kind) {
            case Widget::Kind::CheckBox: spec.kind = FormSpec::Kind::CheckBox; break;
            case Widget::Kind::Radio:    spec.kind = FormSpec::Kind::Radio; break;
            case Widget::Kind::ComboBox: spec.kind = FormSpec::Kind::ComboBox; break;
            case Widget::Kind::ListBox:  spec.kind = FormSpec::Kind::ListBox; break;
            default:                     spec.kind = FormSpec::Kind::Text; break;
            }
        }
        spec.places.append({ w.page, w.bounds, w.exportValue, w.bounds });
        if (!spec.filePages.contains(w.page)) spec.filePages.append(w.page);
    }
#else
    Q_UNUSED(field)
#endif
    return spec;
}

FormSpec FormLayer::specFor(const QString &field) const
{
    for (const FormSpec &spec : m_specs)
        if (spec.name == field && !spec.removed) return spec;
    return fileSpec(field);
}

bool FormLayer::hasField(const QString &field) const
{
#ifdef HAVE_PDF_RENDERING
    return m_fields.contains(field);
#else
    Q_UNUSED(field)
    return false;
#endif
}

bool FormLayer::commitSpec(const QString &field, const FormSpec &spec, const QString &title)
{
    readPages(-1);
    const FormSpec before = specFor(field);
    if (spec == before) return true;
    if (spec.name.trimmed().isEmpty()) return false;
    if (spec.name != field) {
        // Only the last part of a name is the field's own; the rest names the fields above it.
        const QString above = before.isNew() ? QString() : before.original.section(u'.', 0, -2);
        if (spec.name.section(u'.', 0, -2) != above || hasField(spec.name)) return false;
    }

    QList<int> pages = before.pages();
    pages << spec.pages();
    const QMap<QString, QString> values = m_values;
    if (spec.name != field && m_values.contains(field)) m_values.insert(spec.name, m_values.take(field));
    if (spec.kind != before.kind) m_values.remove(spec.name);
    m_specs.removeIf([&](const FormSpec &s) {
        return s.name == field || (!before.isNew() && s.original == before.original);
    });
    const bool pristine = !spec.isNew() && !spec.removed && spec == fileSpec(spec.original);
    if (!pristine && !(spec.isNew() && spec.removed)) m_specs.append(spec);
    if (spec.removed) m_values.remove(spec.name);
    rebuild();
    recalculate();
    applyChange(values, pages);
    reportChange(title, pages.value(0, -1));
    return true;
}

QString FormLayer::addField(FormSpec::Kind kind, int page, const QRectF &bounds, const QString &group)
{
    readPages(-1);
    if (kind == FormSpec::Kind::Radio && !group.isEmpty()) {
        FormSpec radio = specFor(group);
        if (radio.kind == FormSpec::Kind::Radio && !radio.places.isEmpty()) {
            QStringList taken;
            for (const FormSpec::Place &p : std::as_const(radio.places)) taken << p.exportValue;
            int n = int(radio.places.size()) + 1;
            while (taken.contains(QStringLiteral("Choice%1").arg(n))) ++n;
            radio.places.append({ page, bounds, QStringLiteral("Choice%1").arg(n), {} });
            commitSpec(group, radio, tr("Radio button added"));
            return group;
        }
    }

    QString base;
    switch (kind) {
    case FormSpec::Kind::Text:     base = QStringLiteral("Text"); break;
    case FormSpec::Kind::CheckBox: base = QStringLiteral("CheckBox"); break;
    case FormSpec::Kind::Radio:    base = QStringLiteral("RadioGroup"); break;
    case FormSpec::Kind::ComboBox: base = QStringLiteral("Dropdown"); break;
    case FormSpec::Kind::ListBox:  base = QStringLiteral("List"); break;
    }
    int n = 1;
    while (hasField(base + QString::number(n))) ++n;

    FormSpec spec;
    spec.name = base + QString::number(n);
    spec.kind = kind;
    const QString exportValue = kind == FormSpec::Kind::CheckBox ? QStringLiteral("Yes")
                              : kind == FormSpec::Kind::Radio    ? QStringLiteral("Choice1") : QString();
    spec.places.append({ page, bounds, exportValue, {} });
    if (kind == FormSpec::Kind::ComboBox || kind == FormSpec::Kind::ListBox)
        spec.options = { tr("Option 1"), tr("Option 2"), tr("Option 3") };
    return commitSpec(spec.name, spec, tr("Form field added")) ? spec.name : QString();
}

void FormLayer::removeField(const QString &field)
{
    FormSpec spec = specFor(field);
    if (spec.places.isEmpty()) return;
    spec.removed = true;
    commitSpec(field, spec, tr("Form field removed"));
}

void FormLayer::setValue(const QString &field, const QString &value)
{
#ifdef HAVE_PDF_RENDERING
    readPages(-1);
    const QMap<QString, QString> before = m_values;
    put(field, value);
    recalculate();
    if (m_values == before) return;
    applyChange(before, {});
    reportChange(tr("Form field filled in"), m_fields[field].pages.value(0, -1));
#else
    Q_UNUSED(field) Q_UNUSED(value)
#endif
}

void FormLayer::put(const QString &field, const QString &value)
{
#ifdef HAVE_PDF_RENDERING
    if (value == m_fields.value(field).original) m_values.remove(field);
    else                                         m_values.insert(field, value);
#else
    Q_UNUSED(field) Q_UNUSED(value)
#endif
}

void FormLayer::recalculate()
{
#ifdef HAVE_PDF_RENDERING
    const FormScript::Lookup lookup = [this](const QString &name) {
        if (m_fields.contains(name)) return QStringList { current(name) };
        // A name above the fields stands for all of them, as getField() does.
        QStringList values;
        const QString prefix = name + u'.';
        for (const QString &other : std::as_const(m_order))
            if (other.startsWith(prefix)) values << current(other);
        return values;
    };
    // Calculations may build on each other; a few rounds settle any order.
    constexpr int kRounds = 8;
    for (int round = 0; round < kRounds; ++round) {
        bool changed = false;
        for (const QString &name : std::as_const(m_order)) {
            const Field &f = m_fields[name];
            if (!FormScript::calculates(f.calculate)) continue;
            const auto result = FormScript::calculate(f.calculate, lookup);
            if (!result) continue;
            const QString text = FormScript::numberText(*result, f.format);
            if (current(name) == text) continue;
            put(name, text);
            changed = true;
        }
        if (!changed) break;
    }
#endif
}

void FormLayer::applyChange(const QMap<QString, QString> &before, QList<int> pages)
{
#ifdef HAVE_PDF_RENDERING
    const auto touch = [&](const QString &name) {
        for (int page : m_fields.value(name).pages)
            if (!pages.contains(page)) pages.append(page);
    };
    for (auto it = before.cbegin(); it != before.cend(); ++it)
        if (!m_values.contains(it.key()) || m_values.value(it.key()) != it.value()) touch(it.key());
    for (auto it = m_values.cbegin(); it != m_values.cend(); ++it)
        if (!before.contains(it.key())) touch(it.key());
    syncSession();
    QList<int> done;
    for (int page : std::as_const(pages)) {
        if (page < 0 || done.contains(page)) continue;
        done.append(page);
        m_view->rerenderPage(page);
    }
    for (const QPointer<FormBox> &box : std::as_const(m_boxes))
        if (box) box->update();
#else
    Q_UNUSED(before) Q_UNUSED(pages)
#endif
}

void FormLayer::syncSession()
{
#ifdef HAVE_PDF_RENDERING
    if (!m_session) return;
    QList<EditSession::FormValue> values;
    for (auto it = m_values.cbegin(); it != m_values.cend(); ++it)
        values.append({ it.key(), it.value(), m_fields.value(it.key()).pages });
    // A designed field is filled in again, so it gets the appearance of where it is now.
    for (const FormSpec &spec : std::as_const(m_specs))
        if (!spec.removed && !m_values.contains(spec.name))
            values.append({ spec.name, current(spec.name), m_fields.value(spec.name).pages });
    m_session->replaceFormValues(std::move(values));
    m_session->replaceFormDesign(m_specs);
#endif
}

QString FormLayer::current(const QString &field) const
{
#ifdef HAVE_PDF_RENDERING
    const auto it = m_values.constFind(field);
    return it != m_values.cend() ? *it : m_fields.value(field).original;
#else
    return m_values.value(field);
#endif
}

bool FormLayer::fillable() const
{
    return m_tool == QLatin1String("select") || m_tool == QLatin1String("pan")
        || m_tool == QLatin1String("bookmark");
}

bool FormLayer::missing(int index) const
{
#ifdef HAVE_PDF_RENDERING
    if (index < 0 || index >= m_widgets.size()) return false;
    const QString &name = m_widgets.at(index).name;
    if (!(m_fields.value(name).flags & FormFlags::Required)) return false;
    const QString value = current(name);
    return value.isEmpty() || value == FormFlags::Off;
#else
    Q_UNUSED(index)
    return false;
#endif
}
