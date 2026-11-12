#include "ui/forms/FormLayer.hpp"

#include "ui/DocumentView.hpp"
#include "ui/forms/FormBox.hpp"
#include "ui/forms/FormEditLayout.hpp"
#include "ui/forms/FormFlags.hpp"
#include "ui/theme/Theme.hpp"

#include "engine/forms/FormTextLayout.hpp"

#include <cmath>

#include <QApplication>
#include <QComboBox>
#include <QFocusEvent>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QMenu>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextEdit>
#include <QTimer>
#include <QToolTip>

bool FormLayer::eventFilter(QObject *watched, QEvent *event)
{
#ifdef HAVE_PDF_RENDERING
    if (!m_editor || !(watched == m_editor || m_editor->isAncestorOf(qobject_cast<QWidget *>(watched))))
        return false;
    if (event->type() == QEvent::KeyPress) {
        auto *key = static_cast<QKeyEvent *>(event);
        const bool multiline = qobject_cast<QTextEdit *>(m_editor.data());
        switch (key->key()) {
        case Qt::Key_Escape:
            closeEditor();
            return true;
        case Qt::Key_Return:
        case Qt::Key_Enter:
            if (multiline && !(key->modifiers() & Qt::ControlModifier)) return false;
            commitEditor(true);
            return true;
        case Qt::Key_Tab:
        case Qt::Key_Backtab: {
            // Tab goes on to the next field to type into, as in every form.
            const int from = m_editing;
            const int step = key->key() == Qt::Key_Tab && !(key->modifiers() & Qt::ShiftModifier) ? 1 : -1;
            if (!commitEditor(true)) return true;
            for (int i = 1; i < m_widgets.size(); ++i) {
                const int next = (from + step * i + m_widgets.size()) % m_widgets.size();
                const Field &f = m_fields[m_widgets.at(next).name];
                if (!m_boxes.value(next)) continue;
                if (f.kind == Widget::Kind::Text
                        || (f.kind == Widget::Kind::ComboBox && (f.flags & FormFlags::Editable))) {
                    openEditor(next);
                    break;
                }
            }
            return true;
        }
        default:
            return false;
        }
    }
    if (event->type() == QEvent::FocusOut
            && static_cast<QFocusEvent *>(event)->reason() != Qt::PopupFocusReason) {
        QTimer::singleShot(0, this, [this] {
            QWidget *focus = QApplication::focusWidget();
            if (m_editor && focus != m_editor && !m_editor->isAncestorOf(focus)) commitEditor(false);
        });
    }
#else
    Q_UNUSED(watched) Q_UNUSED(event)
#endif
    return false;
}

#ifdef HAVE_PDF_RENDERING
void FormLayer::openEditor(int index, const QPoint &click)
{
    closeEditor();
    FormBox *box = m_boxes.value(index);
    QWidget *canvas = m_view ? m_view->canvasWidget() : nullptr;
    if (!box || !canvas) return;
    const Widget &w = m_widgets.at(index);
    const Field &f = m_fields[w.name];
    const QString text = current(w.name);

    // The editor is transparent over the box, which paints the field while it is open,
    // so the typed text can stand exactly where PDFium will draw it.
    QWidget *editor = nullptr;
    QWidget *typed = nullptr;
    if (f.kind == Widget::Kind::Text && (f.flags & FormFlags::Multiline)) {
        auto *edit = new QTextEdit(canvas);
        edit->setAcceptRichText(false);
        edit->setFrameShape(QFrame::NoFrame);
        edit->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        edit->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        edit->document()->setDocumentMargin(0);
        edit->document()->setDocumentLayout(new FormEditLayout(edit->document()));
        edit->setPlainText(text);
        connect(edit, &QTextEdit::textChanged, this, &FormLayer::layoutEditor);
        editor = typed = edit;
    } else if (f.kind == Widget::Kind::ComboBox) {
        auto *combo = new QComboBox(canvas);
        combo->setEditable(true);
        combo->addItems(f.options);
        combo->setCurrentText(text);
        connect(combo, &QComboBox::activated, this, [this] { commitEditor(true); });
        editor = combo;
        typed  = combo->lineEdit();
    } else {
        auto *edit = new QLineEdit(canvas);
        edit->setText(text);
        edit->setFrame(false);
        if (f.flags & FormFlags::Password) edit->setEchoMode(QLineEdit::Password);
        connect(edit, &QLineEdit::textChanged, this, &FormLayer::layoutEditor);
        editor = typed = edit;
    }
    if (f.kind == Widget::Kind::ComboBox)
        editor->setStyleSheet(QStringLiteral("background: white; color: black; border: 1px solid %1;")
                                  .arg(Theme::Primary.name()));
    else
        editor->setStyleSheet(QStringLiteral("background: transparent; color: %1; border: none; padding: 0;")
                                  .arg(w.textColor.name()));
    editor->installEventFilter(this);
    if (typed && typed != editor) typed->installEventFilter(this);
    m_editor  = editor;
    m_editing = index;
    layoutEditor();
    box->update();
    editor->show();
    editor->raise();

    // A click puts the caret where it landed; coming in by Tab takes the whole text.
    const QPoint at = click.x() < 0 ? QPoint() : click + box->pos() - editor->pos();
    if (auto *edit = qobject_cast<QLineEdit *>(editor)) {
        if (click.x() < 0) edit->selectAll();
        else edit->setCursorPosition(edit->cursorPositionAt(at));
    } else if (auto *edit = qobject_cast<QTextEdit *>(editor)) {
        if (click.x() < 0) edit->selectAll();
        else edit->setTextCursor(edit->cursorForPosition(edit->viewport()->mapFrom(edit, at)));
    } else if (auto *combo = qobject_cast<QComboBox *>(editor); combo && combo->lineEdit()) {
        combo->lineEdit()->selectAll();
    }
    (typed ? typed : editor)->setFocus(Qt::MouseFocusReason);
}

void FormLayer::layoutEditor()
{
    FormBox *box = m_boxes.value(m_editing);
    if (!m_editor || !box || !m_view) return;
    const Widget &w = m_widgets.at(m_editing);
    const qreal scale = m_view->screenScale();
    const QRect area = box->geometry();
    // The box is the field rounded out to whole pixels; the render starts between them. The plate
    // is the field inside its border, where PDFium sets and cuts the text, counted from the box.
    const QLabel *label = m_view->pageLabel(w.page);
    const QPointF start = label ? w.bounds.topLeft() * scale + QPointF(label->pos()) - QPointF(area.topLeft())
                                : QPointF();
    const QRectF plateF(start + QPointF(FormTextLayout::kInset, FormTextLayout::kInset) * scale,
                        (w.bounds.size() - 2 * QSizeF(FormTextLayout::kInset, FormTextLayout::kInset)) * scale);
    const QRect plate = plateF.toAlignedRect();
    // The font at the field's size, spaced so the text is as wide as PDFium sets it.
    const auto sized = [&](qreal pt, const QString &text) {
        QFont font = FormTextLayout::font();
        font.setPointSizeF(qMax(1.0, pt * scale) * 72.0 / qMax(1, m_editor->logicalDpiY()));
        const QString flat = QString(text).remove(u'\n');
        if (flat.size() > 1) {
            const qreal want = FormTextLayout::textWidth(flat, pt) * scale;
            const qreal have = QFontMetricsF(font).horizontalAdvance(flat);
            font.setLetterSpacing(QFont::AbsoluteSpacing, (want - have) / flat.size());
        }
        return font;
    };

    if (auto *combo = qobject_cast<QComboBox *>(m_editor.data())) {
        combo->setFont(sized(FormTextLayout::fontSize(combo->currentText(), w.bounds.size(), false, w.fontSize), combo->currentText()));
        combo->setGeometry(area);
        return;
    }

    if (auto *edit = qobject_cast<QLineEdit *>(m_editor.data())) {
        const qreal size = FormTextLayout::fontSize(edit->text(), w.bounds.size(), false, w.fontSize);
        const QFont font = sized(size, edit->text());
        if (edit->font() != font) edit->setFont(font);
        // QLineEdit keeps two pixels on each side of the text, centres a line at least as high as
        // the text's bounding box and draws on a whole pixel; PDFium's baseline lands on the nearest
        // one. The editor is as high as that takes and moved so its baseline meets PDFium's.
        constexpr int kSide = 2;
        const QFontMetrics metrics(font);
        const int lineHeight = qMax(metrics.height(), metrics.boundingRect(edit->text()).height());
        const int height = qMax(area.height(), lineHeight + 2);
        const int baseline = int(std::floor((height - lineHeight + 1) / 2 + QFontMetricsF(font).ascent()));
        const int target = qRound(start.y() + FormTextLayout::singleBaseline(w.bounds.height(), size) * scale);
        // Aligned left the editor is as wide as the plate and scrolls by itself. Otherwise it is as
        // wide as its text and stands where PDFium sets it, which may begin before the plate.
        int left = qRound(plateF.left()) - kSide, width = plate.right() + 1 - qRound(plateF.left()) + 2 * kSide;
        QRect seen = plate;
        if (w.alignment) {
            const qreal textWidth = FormTextLayout::textWidth(edit->text(), size) * scale;
            left  = qRound(plateF.left() + (plateF.width() - textWidth) * w.alignment / 2.0) - kSide;
            width = int(std::ceil(QFontMetricsF(font).horizontalAdvance(edit->text()))) + 2 * kSide + 4
                  + qMax(0, -metrics.minRightBearing());
            if (w.alignment == 2) seen.adjust(0, 0, 2, 0);
        }
        const QRect rect(area.left() + left, area.top() + target - baseline, width, height);
        edit->setGeometry(rect);
        // PDFium cuts the text off at the field's border; the caret after right-aligned text stays.
        edit->setMask(seen.translated(area.topLeft() - rect.topLeft()));
        return;
    }

    if (auto *edit = qobject_cast<QTextEdit *>(m_editor.data())) {
        const QString text = edit->toPlainText();
        const qreal size = FormTextLayout::fontSize(text, w.bounds.size(), true, w.fontSize);
        const QFont font = sized(size, text);
        if (edit->font() != font) {
            const QSignalBlocker blocker(edit);
            edit->setFont(font);
            edit->document()->setDefaultFont(font);
        }
        // PDFium cuts the text off at the field's border, so the editor ends there too.
        if (auto *layout = qobject_cast<FormEditLayout *>(edit->document()->documentLayout()))
            layout->setMetrics(size, scale, w.bounds.width() - 2 * FormTextLayout::kInset, w.alignment,
                               QPointF(plate.topLeft()) - start);
        edit->setGeometry(plate.translated(area.topLeft()));
    }
}

void FormLayer::openChoice(int index)
{
    FormBox *box = m_boxes.value(index);
    if (!box) return;
    const QString name = m_widgets.at(index).name;
    const QString selected = current(name);
    QMenu menu(box);
    for (const QString &option : std::as_const(m_fields[name].options)) {
        QAction *action = menu.addAction(option);
        action->setCheckable(true);
        action->setChecked(option == selected);
    }
    if (menu.isEmpty()) return;
    menu.setMinimumWidth(box->width());
    if (QAction *chosen = menu.exec(box->mapToGlobal(QPoint(0, box->height()))))
        setValue(name, chosen->text());
}

bool FormLayer::commitEditor(bool keepOnProblem)
{
    if (!m_editor || m_editing < 0 || m_committing) return true;
    QString text;
    if (auto *edit = qobject_cast<QLineEdit *>(m_editor.data()))            text = edit->text();
    else if (auto *edit = qobject_cast<QTextEdit *>(m_editor.data()))       text = edit->toPlainText();
    else if (auto *combo = qobject_cast<QComboBox *>(m_editor.data()))      text = combo->currentText();

    const Widget &w = m_widgets.at(m_editing);
    const Field &f = m_fields[w.name];
    const FormScript::Checked checked = FormScript::check(text, f.format, f.range);
    if (checked.problem != FormScript::Problem::None) {
        QWidget *anchor = m_editor;
        const QString message = problemText(m_editing, checked.problem);
        if (!keepOnProblem) {
            anchor = m_boxes.value(m_editing);
            closeEditor();
        }
        if (anchor) QToolTip::showText(anchor->mapToGlobal(QPoint(0, anchor->height())), message, anchor);
        return false;
    }
    const QString name = w.name;
    closeEditor();
    setValue(name, checked.value);
    return true;
}

#endif

void FormLayer::closeEditor()
{
    if (!m_editor) {
        m_editing = -1;
        return;
    }
    m_committing = true;
    QWidget *editor = m_editor;
    if (FormBox *box = m_boxes.value(m_editing)) box->update();
    m_editor  = nullptr;
    m_editing = -1;
    editor->hide();
    editor->deleteLater();
    m_committing = false;
}
void FormLayer::activate(int index, const QPoint &click)
{
#ifdef HAVE_PDF_RENDERING
    if (!fillable() || index < 0 || index >= m_widgets.size()) return;
    if (m_editor && m_editing == index) return;
    if (m_editor && !commitEditor(false)) return;
    const Widget &w = m_widgets.at(index);
    const Field &f = m_fields[w.name];
    const QString on = w.exportValue.isEmpty() ? QStringLiteral("Yes") : w.exportValue;
    switch (f.kind) {
    case Widget::Kind::Text:
        openEditor(index, click);
        break;
    case Widget::Kind::CheckBox:
        setValue(w.name, current(w.name) == on ? FormFlags::Off : on);
        break;
    case Widget::Kind::Radio:
        if (current(w.name) != on) setValue(w.name, on);
        break;
    case Widget::Kind::ComboBox:
        if (f.flags & FormFlags::Editable) openEditor(index);
        else                     openChoice(index);
        break;
    case Widget::Kind::ListBox:
        openChoice(index);
        break;
    default:
        break;
    }
#else
    Q_UNUSED(index)
#endif
}

QString FormLayer::problemText(int index, FormScript::Problem problem) const
{
#ifdef HAVE_PDF_RENDERING
    const Field f = m_fields.value(m_widgets.value(index).name);
    const auto shown = [](double v) { return QLocale().toString(v, 'g', 12); };
    switch (problem) {
    case FormScript::Problem::NotNumber:
        return tr("Please enter a number.");
    case FormScript::Problem::NotDate:
        return tr("Please enter a date in the form %1.").arg(f.format.pattern);
    case FormScript::Problem::NotTime:
        return tr("Please enter a time in the form %1.").arg(f.format.pattern);
    case FormScript::Problem::NotSpecial:
        return tr("The entry does not have the required format.");
    case FormScript::Problem::OutOfRange:
        if (f.range && f.range->min && f.range->max)
            return tr("Please enter a value from %1 to %2.")
                .arg(shown(*f.range->min), shown(*f.range->max));
        if (f.range && f.range->min)
            return tr("Please enter a value of at least %1.").arg(shown(*f.range->min));
        if (f.range && f.range->max)
            return tr("Please enter a value of at most %1.").arg(shown(*f.range->max));
        return tr("The value is outside the allowed range.");
    case FormScript::Problem::None:
        break;
    }
#else
    Q_UNUSED(index) Q_UNUSED(problem)
#endif
    return {};
}
