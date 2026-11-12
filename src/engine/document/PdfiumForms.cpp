#include "engine/document/PdfiumForms.hpp"

#if defined(HAVE_PDF_RENDERING) && defined(HAVE_PDFIUM)

#include "engine/document/PdfiumPageSpace.hpp"

#include "fpdf_annot.h"

#include <QMap>

#include <optional>
#include <string>
#include <vector>

namespace {

template <typename Get>
QString wide(Get get)
{
    const unsigned long bytes = get(nullptr, 0);
    if (bytes <= 2) return {};
    std::vector<FPDF_WCHAR> buffer(bytes / 2 + 1, 0);
    get(buffer.data(), bytes);
    return QString::fromUtf16(reinterpret_cast<const char16_t *>(buffer.data()));
}

QString fieldName(FPDF_FORMHANDLE form, FPDF_ANNOTATION annot)
{
    return wide([&](FPDF_WCHAR *b, unsigned long n) {
        return FPDFAnnot_GetFormFieldName(form, annot, b, n);
    });
}

QString exportValue(FPDF_FORMHANDLE form, FPDF_ANNOTATION annot)
{
    return wide([&](FPDF_WCHAR *b, unsigned long n) {
        return FPDFAnnot_GetFormFieldExportValue(form, annot, b, n);
    });
}

QString script(FPDF_FORMHANDLE form, FPDF_ANNOTATION annot, int event)
{
    return wide([&](FPDF_WCHAR *b, unsigned long n) {
        return FPDFAnnot_GetFormAdditionalActionJavaScript(form, annot, event, b, n);
    });
}

std::optional<PdfBackend::FormWidget::Kind> kindOf(int type)
{
    using Kind = PdfBackend::FormWidget::Kind;
    switch (type) {
    case FPDF_FORMFIELD_TEXTFIELD:   return Kind::Text;
    case FPDF_FORMFIELD_CHECKBOX:    return Kind::CheckBox;
    case FPDF_FORMFIELD_RADIOBUTTON: return Kind::Radio;
    case FPDF_FORMFIELD_COMBOBOX:    return Kind::ComboBox;
    case FPDF_FORMFIELD_LISTBOX:     return Kind::ListBox;
    case FPDF_FORMFIELD_PUSHBUTTON:  return Kind::Button;
    case FPDF_FORMFIELD_SIGNATURE:   return Kind::Signature;
    default:                         return std::nullopt;
    }
}

struct Widget {
    FPDF_PAGE       page  { nullptr };
    FPDF_ANNOTATION annot { nullptr };
};

void typeText(FPDF_FORMHANDLE form, const Widget &w, const QString &text)
{
    if (!FORM_SetFocusedAnnot(form, w.annot)) return;
    FORM_SelectAllText(form, w.page);
    const std::u16string utf16 = text.toStdU16String();
    FORM_ReplaceSelection(form, w.page, reinterpret_cast<FPDF_WIDESTRING>(utf16.c_str()));
    FORM_ForceToKillFocus(form);
}

void click(FPDF_FORMHANDLE form, const Widget &w)
{
    FS_RECTF r {};
    if (!FPDFAnnot_GetRect(w.annot, &r)) return;
    const double x = (r.left + r.right) / 2, y = (r.bottom + r.top) / 2;
    FORM_OnLButtonDown(form, w.page, 0, x, y);
    FORM_OnLButtonUp(form, w.page, 0, x, y);
    FORM_ForceToKillFocus(form);
}

void select(FPDF_FORMHANDLE form, const Widget &w, int index)
{
    if (!FORM_SetFocusedAnnot(form, w.annot)) return;
    FORM_SetIndexSelected(form, w.page, index, true);
    FORM_ForceToKillFocus(form);
}

int optionIndex(FPDF_FORMHANDLE form, FPDF_ANNOTATION annot, const QString &label)
{
    for (int i = 0, n = FPDFAnnot_GetOptionCount(form, annot); i < n; ++i) {
        const QString option = wide([&](FPDF_WCHAR *b, unsigned long len) {
            return FPDFAnnot_GetOptionLabel(form, annot, i, b, len);
        });
        if (option == label) return i;
    }
    return -1;
}

void fill(FPDF_FORMHANDLE form, const QList<Widget> &widgets, const QString &value)
{
    const Widget &first = widgets.first();
    const int type  = FPDFAnnot_GetFormFieldType(form, first.annot);
    const int flags = FPDFAnnot_GetFormFieldFlags(form, first.annot);
    // A calculated field is often read-only; only the user is kept out of it.
    const bool readOnly = flags & FPDF_FORMFLAG_READONLY;
    if (readOnly) FPDFAnnot_SetFormFieldFlags(form, first.annot, flags & ~FPDF_FORMFLAG_READONLY);

    switch (type) {
    case FPDF_FORMFIELD_TEXTFIELD:
        typeText(form, first, value);
        break;
    case FPDF_FORMFIELD_COMBOBOX:
    case FPDF_FORMFIELD_LISTBOX: {
        const int index = optionIndex(form, first.annot, value);
        if (index >= 0) select(form, first, index);
        else if (type == FPDF_FORMFIELD_COMBOBOX && (flags & FPDF_FORMFLAG_CHOICE_EDIT))
            typeText(form, first, value);
        break;
    }
    case FPDF_FORMFIELD_CHECKBOX:
    case FPDF_FORMFIELD_RADIOBUTTON: {
        const Widget *target = nullptr;
        if (value != QLatin1String("Off"))
            for (const Widget &w : widgets)
                if (exportValue(form, w.annot) == value) { target = &w; break; }
        if (!target) {
            for (const Widget &w : widgets)
                if (FPDFAnnot_IsChecked(form, w.annot)) click(form, w);
        } else if (!FPDFAnnot_IsChecked(form, target->annot)) {
            click(form, *target);
        }
        break;
    }
    default:
        break;
    }

    if (readOnly) FPDFAnnot_SetFormFieldFlags(form, first.annot, flags);
}

}

PdfiumForms::PdfiumForms(FPDF_DOCUMENT doc)
    : m_doc(doc)
{
    m_info.version = 1;
    if (m_doc && FPDF_GetFormType(m_doc) != FORMTYPE_NONE)
        m_form = FPDFDOC_InitFormFillEnvironment(m_doc, &m_info);
}

PdfiumForms::~PdfiumForms()
{
    if (m_form) FPDFDOC_ExitFormFillEnvironment(m_form);
}

QList<PdfBackend::FormWidget> PdfiumForms::read(int page) const
{
    QList<PdfBackend::FormWidget> result;
    if (!m_form) return result;
    FPDF_PAGE pg = FPDF_LoadPage(m_doc, page);
    if (!pg) return result;
    FORM_OnAfterLoadPage(pg, m_form);
    const PdfiumPageSpace space(pg);

    for (int i = 0, n = FPDFPage_GetAnnotCount(pg); i < n; ++i) {
        FPDF_ANNOTATION annot = FPDFPage_GetAnnot(pg, i);
        if (!annot) continue;
        const auto kind = FPDFAnnot_GetSubtype(annot) == FPDF_ANNOT_WIDGET
                              ? kindOf(FPDFAnnot_GetFormFieldType(m_form, annot))
                              : std::nullopt;
        const int shown = FPDFAnnot_GetFlags(annot);
        if (!kind || (shown & (FPDF_ANNOT_FLAG_HIDDEN | FPDF_ANNOT_FLAG_NOVIEW))) {
            FPDFPage_CloseAnnot(annot);
            continue;
        }

        PdfBackend::FormWidget w;
        w.kind    = *kind;
        w.page    = page;
        w.name    = fieldName(m_form, annot);
        w.tooltip = wide([&](FPDF_WCHAR *b, unsigned long len) {
            return FPDFAnnot_GetFormFieldAlternateName(m_form, annot, b, len);
        });
        w.value   = wide([&](FPDF_WCHAR *b, unsigned long len) {
            return FPDFAnnot_GetFormFieldValue(m_form, annot, b, len);
        });
        w.value.replace(QLatin1String("\r\n"), QLatin1String("\n")).replace(u'\r', u'\n');
        w.flags   = FPDFAnnot_GetFormFieldFlags(m_form, annot);
        if (w.kind == PdfBackend::FormWidget::Kind::CheckBox
                || w.kind == PdfBackend::FormWidget::Kind::Radio) {
            w.exportValue = exportValue(m_form, annot);
            w.checked     = FPDFAnnot_IsChecked(m_form, annot);
        }
        for (int o = 0, count = FPDFAnnot_GetOptionCount(m_form, annot); o < count; ++o) {
            w.options.append(wide([&](FPDF_WCHAR *b, unsigned long len) {
                return FPDFAnnot_GetOptionLabel(m_form, annot, o, b, len);
            }));
            if (w.selected < 0 && FPDFAnnot_IsOptionSelected(m_form, annot, o)) w.selected = o;
        }
        float size = 0;
        if (FPDFAnnot_GetFontSize(m_form, annot, &size)) w.fontSize = size;
        float quadding = 0;
        if (FPDFAnnot_GetNumberValue(annot, "Q", &quadding)) w.alignment = qBound(0, int(quadding), 2);
        unsigned int red = 0, green = 0, blue = 0;
        if (FPDFAnnot_GetFontColor(m_form, annot, &red, &green, &blue))
            w.textColor = QColor(int(red), int(green), int(blue));
        w.keystroke = script(m_form, annot, FPDF_ANNOT_AACTION_KEY_STROKE);
        w.format    = script(m_form, annot, FPDF_ANNOT_AACTION_FORMAT);
        w.validate  = script(m_form, annot, FPDF_ANNOT_AACTION_VALIDATE);
        w.calculate = script(m_form, annot, FPDF_ANNOT_AACTION_CALCULATE);
        FS_RECTF r {};
        if (FPDFAnnot_GetRect(annot, &r)) w.bounds = space.toView(r.left, r.bottom, r.right, r.top);
        result.append(std::move(w));
        FPDFPage_CloseAnnot(annot);
    }

    FORM_OnBeforeClosePage(pg, m_form);
    FPDF_ClosePage(pg);
    return result;
}

void PdfiumForms::apply(const QList<EditSession::FormValue> &values)
{
    if (!m_form || values.isEmpty()) return;

    // Every page of a field is loaded, so all its widgets take the new appearance.
    QMap<int, FPDF_PAGE> pages;
    for (const EditSession::FormValue &v : values) {
        for (int page : v.pages) {
            if (pages.contains(page)) continue;
            FPDF_PAGE pg = FPDF_LoadPage(m_doc, page);
            if (!pg) continue;
            FORM_OnAfterLoadPage(pg, m_form);
            pages.insert(page, pg);
        }
    }

    for (const EditSession::FormValue &v : values) {
        QList<Widget> widgets;
        for (int page : v.pages) {
            FPDF_PAGE pg = pages.value(page);
            if (!pg) continue;
            for (int i = 0, n = FPDFPage_GetAnnotCount(pg); i < n; ++i) {
                FPDF_ANNOTATION annot = FPDFPage_GetAnnot(pg, i);
                if (!annot) continue;
                if (FPDFAnnot_GetSubtype(annot) == FPDF_ANNOT_WIDGET
                        && fieldName(m_form, annot) == v.field)
                    widgets.append({ pg, annot });
                else
                    FPDFPage_CloseAnnot(annot);
            }
        }
        if (!widgets.isEmpty()) fill(m_form, widgets, v.value);
        for (const Widget &w : std::as_const(widgets)) FPDFPage_CloseAnnot(w.annot);
    }

    for (FPDF_PAGE pg : std::as_const(pages)) {
        FORM_OnBeforeClosePage(pg, m_form);
        FPDF_ClosePage(pg);
    }
}

#endif
