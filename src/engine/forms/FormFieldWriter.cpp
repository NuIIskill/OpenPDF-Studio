#include "engine/forms/FormFieldWriter.hpp"

#ifdef HAVE_QPDF

#include <qpdf/Buffer.hh>
#include <qpdf/QPDF.hh>
#include <qpdf/QPDFAcroFormDocumentHelper.hh>
#include <qpdf/QPDFAnnotationObjectHelper.hh>
#include <qpdf/QPDFFormFieldObjectHelper.hh>
#include <qpdf/QPDFObjectHandle.hh>
#include <qpdf/QPDFPageDocumentHelper.hh>
#include <qpdf/QPDFPageObjectHelper.hh>
#include <qpdf/QPDFWriter.hh>

#include <QDebug>
#include <QFile>
#include <QRegularExpression>
#include <QTransform>

#include <algorithm>
#include <cmath>
#include <map>

namespace {

// The field flags of the PDF specification for each kind.
constexpr int kNoToggleToOff = 1 << 14;
constexpr int kRadio         = 1 << 15;
constexpr int kCombo         = 1 << 17;
constexpr int kAppearanceFlags = (1 << 12) | (1 << 13) | (1 << 24);

using Kind = FormSpec::Kind;

QPDFObjectHandle text(const QString &s)  { return QPDFObjectHandle::newUnicodeString(s.toStdString()); }
QPDFObjectHandle name(const QString &s)  { return QPDFObjectHandle::newName("/" + s.toStdString()); }
QPDFObjectHandle real(double v)          { return QPDFObjectHandle::newReal(v, 3); }

QString textOf(QPDFObjectHandle h)
{
    return h.isString() ? QString::fromStdString(h.getUTF8Value()) : QString();
}

// The page as shown, as PdfiumPageSpace has it: the crop box within the media box, turned.
QTransform viewOf(QPDFPageObjectHelper &page)
{
    const auto media = page.getMediaBox().getArrayAsRectangle();
    const auto crop  = page.getCropBox(false, true).getArrayAsRectangle();
    const double left   = std::max(std::min(media.llx, media.urx), std::min(crop.llx, crop.urx));
    const double right  = std::min(std::max(media.llx, media.urx), std::max(crop.llx, crop.urx));
    const double bottom = std::max(std::min(media.lly, media.ury), std::min(crop.lly, crop.ury));
    const double top    = std::min(std::max(media.lly, media.ury), std::max(crop.lly, crop.ury));
    const double width = right - left, height = top - bottom;
    QPDFObjectHandle rotate = page.getAttribute("/Rotate", false);
    const int degrees = rotate.isInteger() ? int(rotate.getIntValue()) : 0;
    const int turns = ((degrees / 90) % 4 + 4) % 4;
    const QTransform unturned(1, 0, 0, -1, -left, top);
    switch (turns) {
    case 1:  return unturned * QTransform(0, 1, -1, 0, height, 0);
    case 2:  return unturned * QTransform(-1, 0, 0, -1, width, height);
    case 3:  return unturned * QTransform(0, -1, 1, 0, 0, width);
    default: return unturned;
    }
}

QRectF viewRect(QPDFObjectHandle::Rectangle r, const QTransform &view)
{
    return view.mapRect(QRectF(QPointF(r.llx, r.lly), QPointF(r.urx, r.ury)).normalized());
}

QPDFObjectHandle pdfRect(const QRectF &bounds, const QTransform &view)
{
    const QRectF r = view.inverted().mapRect(bounds);
    return QPDFObjectHandle::newArray(
        std::vector<QPDFObjectHandle> { real(r.left()), real(r.top()), real(r.right()), real(r.bottom()) });
}

bool near(const QRectF &a, const QRectF &b)
{
    return std::abs(a.left() - b.left()) < 0.5 && std::abs(a.top() - b.top()) < 0.5
        && std::abs(a.right() - b.right()) < 0.5 && std::abs(a.bottom() - b.bottom()) < 0.5;
}

QPDFObjectHandle font(QPDF &pdf, const char *base)
{
    QPDFObjectHandle f = QPDFObjectHandle::newDictionary();
    f.replaceKey("/Type", QPDFObjectHandle::newName("/Font"));
    f.replaceKey("/Subtype", QPDFObjectHandle::newName("/Type1"));
    f.replaceKey("/BaseFont", QPDFObjectHandle::newName(base));
    if (std::string(base) != "/ZapfDingbats")
        f.replaceKey("/Encoding", QPDFObjectHandle::newName("/WinAnsiEncoding"));
    return pdf.makeIndirectObject(f);
}

QPDFObjectHandle child(QPDFObjectHandle parent, const char *key, bool array = false)
{
    QPDFObjectHandle h = parent.getKey(key);
    if (array ? !h.isArray() : !h.isDictionary()) {
        h = array ? QPDFObjectHandle::newArray() : QPDFObjectHandle::newDictionary();
        parent.replaceKey(key, h);
    }
    return h;
}

void removeFrom(QPDFObjectHandle holder, const char *key, QPDFObjGen gone)
{
    QPDFObjectHandle list = holder.getKey(key);
    if (!list.isArray()) return;
    QPDFObjectHandle kept = QPDFObjectHandle::newArray();
    for (int i = 0; i < list.getArrayNItems(); ++i)
        if (list.getArrayItem(i).getObjGen() != gone) kept.appendItem(list.getArrayItem(i));
    holder.replaceKey(key, kept);
}

QString circle(double cx, double cy, double r)
{
    const double k = r * 0.5523;
    return QStringLiteral("%1 %2 m %3 %4 %5 %6 %7 %8 c %9 %10 %11 %12 %13 %14 c ")
               .arg(cx + r).arg(cy).arg(cx + r).arg(cy + k).arg(cx + k).arg(cy + r).arg(cx).arg(cy + r)
               .arg(cx - k).arg(cy + r).arg(cx - r).arg(cy + k).arg(cx - r).arg(cy)
         + QStringLiteral("%1 %2 %3 %4 %5 %6 c %7 %8 %9 %10 %11 %12 c h")
               .arg(cx - r).arg(cy - k).arg(cx - k).arg(cy - r).arg(cx).arg(cy - r)
               .arg(cx + k).arg(cy - r).arg(cx + r).arg(cy - k).arg(cx + r).arg(cy);
}

QPDFObjectHandle form(QPDF &pdf, double w, double h, const QString &content, QPDFObjectHandle zadb)
{
    QPDFObjectHandle stream = QPDFObjectHandle::newStream(&pdf, content.toStdString());
    QPDFObjectHandle dict = stream.getDict();
    dict.replaceKey("/Type", QPDFObjectHandle::newName("/XObject"));
    dict.replaceKey("/Subtype", QPDFObjectHandle::newName("/Form"));
    dict.replaceKey("/BBox", QPDFObjectHandle::newArray(
        std::vector<QPDFObjectHandle> { real(0), real(0), real(w), real(h) }));
    QPDFObjectHandle fonts = QPDFObjectHandle::newDictionary();
    fonts.replaceKey("/ZaDb", zadb);
    QPDFObjectHandle resources = QPDFObjectHandle::newDictionary();
    resources.replaceKey("/Font", fonts);
    dict.replaceKey("/Resources", resources);
    return stream;
}

// Check boxes and radio buttons carry their looks for on and off; PDFium only picks one.
void buttonLooks(QPDF &pdf, QPDFObjectHandle widget, Kind kind, const QString &on, QPDFObjectHandle zadb)
{
    const auto r = widget.getKey("/Rect").getArrayAsRectangle();
    const double w = std::abs(r.urx - r.llx), h = std::abs(r.ury - r.lly);
    const double s = std::min(w, h);
    QString off, mark;
    if (kind == Kind::Radio) {
        off  = QStringLiteral("q 1 g %1 f Q q 0.6 G 1 w %2 S Q ")
                   .arg(circle(w / 2, h / 2, s / 2 - 0.5), circle(w / 2, h / 2, s / 2 - 0.5));
        mark = QStringLiteral("q 0 g %1 f Q").arg(circle(w / 2, h / 2, s * 0.22));
    } else {
        off  = QStringLiteral("q 1 g 0 0 %1 %2 re f Q q 0.6 G 1 w 0.5 0.5 %3 %4 re S Q ")
                   .arg(w).arg(h).arg(w - 1).arg(h - 1);
        const double size = s * 0.8;
        mark = QStringLiteral("BT /ZaDb %1 Tf 0 g %2 %3 Td (4) Tj ET")
                   .arg(size, 0, 'f', 2).arg((w - size * 0.846) / 2, 0, 'f', 2)
                   .arg((h - size * 0.7) / 2, 0, 'f', 2);
    }
    QPDFObjectHandle normal = QPDFObjectHandle::newDictionary();
    normal.replaceKey("/" + on.toStdString(), form(pdf, w, h, off + mark, zadb));
    normal.replaceKey("/Off", form(pdf, w, h, off, zadb));
    QPDFObjectHandle ap = QPDFObjectHandle::newDictionary();
    ap.replaceKey("/N", normal);
    widget.replaceKey("/AP", ap);
    QPDFObjectHandle as = widget.getKey("/AS");
    if (!as.isName()) widget.replaceKey("/AS", QPDFObjectHandle::newName("/Off"));
}

QPDFObjectHandle newWidget(QPDF &pdf, QPDFPageObjectHelper &page, const QRectF &bounds)
{
    QPDFObjectHandle w = QPDFObjectHandle::newDictionary();
    w.replaceKey("/Type", QPDFObjectHandle::newName("/Annot"));
    w.replaceKey("/Subtype", QPDFObjectHandle::newName("/Widget"));
    w.replaceKey("/Rect", pdfRect(bounds, viewOf(page)));
    w.replaceKey("/P", page.getObjectHandle());
    w.replaceKey("/F", QPDFObjectHandle::newInteger(4));
    QPDFObjectHandle mk = QPDFObjectHandle::newDictionary();
    mk.replaceKey("/BC", QPDFObjectHandle::newArray(std::vector<QPDFObjectHandle> { real(0.6), real(0.6), real(0.6) }));
    mk.replaceKey("/BG", QPDFObjectHandle::newArray(std::vector<QPDFObjectHandle> { real(1), real(1), real(1) }));
    w.replaceKey("/MK", mk);
    w = pdf.makeIndirectObject(w);
    child(page.getObjectHandle(), "/Annots", true).appendItem(w);
    return w;
}

QString fontDA(double size) { return QStringLiteral("/Helv %1 Tf 0 g").arg(size, 0, 'g', 4); }

QStringList optionLabels(QPDFObjectHandle opt)
{
    QStringList labels;
    if (!opt.isArray()) return labels;
    for (int i = 0; i < opt.getArrayNItems(); ++i) {
        QPDFObjectHandle o = opt.getArrayItem(i);
        labels << (o.isArray() && o.getArrayNItems() > 1 ? textOf(o.getArrayItem(1)) : textOf(o));
    }
    return labels;
}

void setScripts(QPDFObjectHandle field, const FormSpec &spec)
{
    QPDFObjectHandle aa = field.getKey("/AA");
    if (!aa.isDictionary()) aa = QPDFObjectHandle::newDictionary();
    const std::pair<const char *, const QString *> scripts[] = {
        { "/K", &spec.keystroke }, { "/F", &spec.format }, { "/V", &spec.validate }, { "/C", &spec.calculate },
    };
    for (const auto &[key, code] : scripts) {
        if (code->isEmpty()) { aa.removeKey(key); continue; }
        QPDFObjectHandle action = QPDFObjectHandle::newDictionary();
        action.replaceKey("/S", QPDFObjectHandle::newName("/JavaScript"));
        action.replaceKey("/JS", text(*code));
        aa.replaceKey(key, action);
    }
    if (aa.getKeys().empty()) field.removeKey("/AA");
    else                      field.replaceKey("/AA", aa);
}

// Returns whether the field looks different, so its old appearances are dropped.
bool setProperties(QPDFObjectHandle field, const FormSpec &spec)
{
    bool looks = false;
    const QString partial = spec.name.section(u'.', -1);
    field.replaceKey("/T", text(field.hasKey("/Parent") ? partial : spec.name));
    if (spec.tooltip.isEmpty()) field.removeKey("/TU");
    else                        field.replaceKey("/TU", text(spec.tooltip));

    int kindFlags = 0;
    if (spec.kind == Kind::Radio)    kindFlags = kRadio | kNoToggleToOff;
    if (spec.kind == Kind::ComboBox) kindFlags = kCombo;
    const int flags = spec.flags | kindFlags;
    const int before = field.getKey("/Ff").isInteger() ? int(field.getKey("/Ff").getIntValue()) : 0;
    looks |= (before & kAppearanceFlags) != (flags & kAppearanceFlags);
    field.replaceKey("/Ff", QPDFObjectHandle::newInteger(flags));

    if (spec.kind == Kind::ComboBox || spec.kind == Kind::ListBox) {
        if (optionLabels(field.getKey("/Opt")) != spec.options) {
            QPDFObjectHandle opt = QPDFObjectHandle::newArray();
            for (const QString &o : spec.options) opt.appendItem(text(o));
            field.replaceKey("/Opt", opt);
            looks = true;
        }
    }
    if (spec.kind == Kind::Text || spec.kind == Kind::ComboBox || spec.kind == Kind::ListBox) {
        QString da = textOf(field.getKey("/DA"));
        static const QRegularExpression size(QStringLiteral(R"((/\S+)\s+[\d.]+\s+Tf)"));
        const QString wanted = da.contains(size)
            ? QString(da).replace(size, QStringLiteral("\\1 %1 Tf").arg(spec.fontSize, 0, 'g', 4))
            : fontDA(spec.fontSize);
        if (wanted != da) {
            field.replaceKey("/DA", text(wanted));
            looks = true;
        }
    }
    setScripts(field, spec);
    return looks;
}

void keepInOrder(QPDFObjectHandle acro, QPDFObjectHandle field, bool calculated)
{
    removeFrom(acro, "/CO", field.getObjGen());
    if (calculated) child(acro, "/CO", true).appendItem(field);
}

void addField(QPDF &pdf, QPDFObjectHandle acro, std::vector<QPDFPageObjectHelper> &pages,
              const FormSpec &spec, QPDFObjectHandle zadb)
{
    QList<QPDFObjectHandle> widgets;
    for (const FormSpec::Place &place : spec.places)
        if (place.page >= 0 && place.page < int(pages.size()))
            widgets << newWidget(pdf, pages[size_t(place.page)], place.bounds);
    if (widgets.isEmpty()) return;

    // A radio group is a field above its buttons; the other kinds are one widget that is the field.
    QPDFObjectHandle field = spec.kind == Kind::Radio
        ? pdf.makeIndirectObject(QPDFObjectHandle::newDictionary()) : widgets.first();
    switch (spec.kind) {
    case Kind::Text:     field.replaceKey("/FT", QPDFObjectHandle::newName("/Tx")); break;
    case Kind::CheckBox:
    case Kind::Radio:    field.replaceKey("/FT", QPDFObjectHandle::newName("/Btn")); break;
    case Kind::ComboBox:
    case Kind::ListBox:  field.replaceKey("/FT", QPDFObjectHandle::newName("/Ch")); break;
    }
    field.replaceKey("/DA", text(spec.kind == Kind::CheckBox || spec.kind == Kind::Radio
                                     ? QStringLiteral("/ZaDb 0 Tf 0 g") : fontDA(spec.fontSize)));
    setProperties(field, spec);
    if (spec.kind == Kind::Radio) {
        QPDFObjectHandle kids = QPDFObjectHandle::newArray();
        for (int i = 0; i < widgets.size(); ++i) {
            widgets[i].replaceKey("/Parent", field);
            kids.appendItem(widgets[i]);
        }
        field.replaceKey("/Kids", kids);
    }
    if (spec.kind == Kind::CheckBox || spec.kind == Kind::Radio) {
        field.replaceKey("/V", QPDFObjectHandle::newName("/Off"));
        for (int i = 0; i < widgets.size(); ++i) {
            const QString on = spec.places.value(i).exportValue;
            buttonLooks(pdf, widgets[i], spec.kind, on.isEmpty() ? QStringLiteral("Yes") : on, zadb);
            QPDFObjectHandle mk = widgets[i].getKey("/MK");
            mk.replaceKey("/CA", text(spec.kind == Kind::Radio ? QStringLiteral("l") : QStringLiteral("4")));
        }
    }
    child(acro, "/Fields", true).appendItem(field);
    keepInOrder(acro, field, !spec.calculate.isEmpty());
}

void changeField(QPDF &pdf, QPDFObjectHandle acro, std::vector<QPDFPageObjectHelper> &pages,
                 QPDFAcroFormDocumentHelper &forms, const std::map<QPDFObjGen, int> &pageOf,
                 const FormSpec &spec, QPDFObjectHandle zadb)
{
    QPDFFormFieldObjectHelper field;
    for (QPDFFormFieldObjectHelper &f : forms.getFormFields())
        if (QString::fromStdString(f.getFullyQualifiedName()) == spec.original) { field = f; break; }
    if (field.getObjectHandle().isNull()) return;
    QPDFObjectHandle dict = field.getObjectHandle();

    for (QPDFAnnotationObjectHelper &annot : forms.getAnnotationsForField(field)) {
        QPDFObjectHandle widget = annot.getObjectHandle();
        const auto found = pageOf.find(widget.getObjGen());
        const int page = found != pageOf.end() ? found->second : -1;
        if (page < 0) continue;
        const QTransform view = viewOf(pages[size_t(page)]);
        const QRectF at = viewRect(annot.getRect(), view);
        const auto place = std::find_if(spec.places.cbegin(), spec.places.cend(),
                                        [&](const FormSpec::Place &p) {
            return p.page == page && !p.from.isNull() && near(p.from, at);
        });
        if (spec.removed || place == spec.places.cend()) {
            removeFrom(pages[size_t(page)].getObjectHandle(), "/Annots", widget.getObjGen());
            if (widget.getObjGen() != dict.getObjGen()) removeFrom(dict, "/Kids", widget.getObjGen());
            continue;
        }
        if (!near(place->bounds, at)) {
            widget.replaceKey("/Rect", pdfRect(place->bounds, view));
            if (spec.kind == Kind::CheckBox || spec.kind == Kind::Radio) {
                QString on = place->exportValue;
                QPDFObjectHandle normal = widget.getKey("/AP").getKey("/N");
                if (normal.isDictionary())
                    for (const std::string &key : normal.getKeys())
                        if (key != "/Off") on = QString::fromStdString(key.substr(1));
                buttonLooks(pdf, widget, spec.kind, on.isEmpty() ? QStringLiteral("Yes") : on, zadb);
            } else {
                widget.removeKey("/AP");
            }
        }
    }
    if (spec.removed) {
        forms.removeFormFields({ dict.getObjGen() });
        removeFrom(acro, "/CO", dict.getObjGen());
        return;
    }

    if (setProperties(dict, spec) && spec.kind != Kind::CheckBox && spec.kind != Kind::Radio)
        for (QPDFAnnotationObjectHelper &annot : forms.getAnnotationsForField(field))
            annot.getObjectHandle().removeKey("/AP");
    keepInOrder(acro, dict, !spec.calculate.isEmpty());

    // New buttons of a radio group the file already has.
    if (spec.kind != Kind::Radio) return;
    for (const FormSpec::Place &place : spec.places) {
        if (!place.from.isNull() || place.page < 0 || place.page >= int(pages.size())) continue;
        QPDFObjectHandle widget = newWidget(pdf, pages[size_t(place.page)], place.bounds);
        widget.replaceKey("/Parent", dict);
        child(dict, "/Kids", true).appendItem(widget);
        buttonLooks(pdf, widget, Kind::Radio,
                    place.exportValue.isEmpty() ? QStringLiteral("Yes") : place.exportValue, zadb);
    }
}

}

namespace FormFieldWriter {

bool available() { return true; }

QByteArray build(const QString &path, const std::string &password, const QList<FormSpec> &specs)
{
    try {
        QPDF pdf;
        pdf.processFile(QFile::encodeName(path).constData(),
                        password.empty() ? nullptr : password.c_str());
        QPDFObjectHandle root = pdf.getRoot();
        QPDFObjectHandle acro = root.getKey("/AcroForm");
        if (!acro.isDictionary()) {
            acro = pdf.makeIndirectObject(QPDFObjectHandle::newDictionary());
            root.replaceKey("/AcroForm", acro);
        }
        child(acro, "/Fields", true);
        QPDFObjectHandle fonts = child(child(acro, "/DR"), "/Font");
        if (!fonts.hasKey("/Helv")) fonts.replaceKey("/Helv", font(pdf, "/Helvetica"));
        if (!fonts.hasKey("/ZaDb")) fonts.replaceKey("/ZaDb", font(pdf, "/ZapfDingbats"));
        if (!acro.hasKey("/DA")) acro.replaceKey("/DA", text(QStringLiteral("/Helv 0 Tf 0 g")));
        const QPDFObjectHandle zadb = fonts.getKey("/ZaDb");

        std::vector<QPDFPageObjectHelper> pages = QPDFPageDocumentHelper(pdf).getAllPages();
        std::map<QPDFObjGen, int> pageOf;
        for (size_t i = 0; i < pages.size(); ++i) {
            QPDFObjectHandle annots = pages[i].getObjectHandle().getKey("/Annots");
            if (!annots.isArray()) continue;
            for (int a = 0; a < annots.getArrayNItems(); ++a)
                pageOf.emplace(annots.getArrayItem(a).getObjGen(), int(i));
        }

        QPDFAcroFormDocumentHelper forms(pdf);
        for (const FormSpec &spec : specs)
            if (!spec.isNew()) changeField(pdf, acro, pages, forms, pageOf, spec, zadb);
        for (const FormSpec &spec : specs)
            if (spec.isNew() && !spec.removed) addField(pdf, acro, pages, spec, zadb);

        QPDFWriter writer(pdf);
        writer.setOutputMemory();
        writer.write();
        const std::shared_ptr<Buffer> buffer = writer.getBufferSharedPointer();
        return QByteArray(reinterpret_cast<const char *>(buffer->getBuffer()), qsizetype(buffer->getSize()));
    } catch (const std::exception &e) {
        qWarning() << "[FormFieldWriter]" << path << e.what();
        return {};
    }
}

}

#else

namespace FormFieldWriter {

bool available() { return false; }

QByteArray build(const QString &, const std::string &, const QList<FormSpec> &) { return {}; }

}

#endif
