#include "engine/export/PdfA.hpp"

#ifdef HAVE_QPDF

#include "engine/export/PdfAAppearances.hpp"
#include "engine/export/PdfAContent.hpp"
#include "engine/export/PdfAFonts.hpp"
#include "engine/export/PdfAGlyphs.hpp"

#include <qpdf/QPDF.hh>
#include <qpdf/QPDFAcroFormDocumentHelper.hh>
#include <qpdf/QPDFAnnotationObjectHelper.hh>
#include <qpdf/QPDFFormFieldObjectHelper.hh>
#include <qpdf/QPDFPageDocumentHelper.hh>
#include <qpdf/QPDFPageObjectHelper.hh>

#include <QColorSpace>
#include <QCoreApplication>
#include <QDateTime>
#include <QDebug>
#include <QRegularExpression>
#include <QTimeZone>

#include <functional>
#include <set>
#include <vector>

namespace {

bool nameIs(QPDFObjectHandle object, const char *name)
{
    return object.isName() && object.getName() == name;
}

void note(QStringList &list, const QString &text)
{
    if (!list.contains(text)) list << text;
}

void visit(QPDFObjectHandle object, const std::function<void(QPDFObjectHandle)> &fn, int depth)
{
    if (depth > 32) return;
    QPDFObjectHandle dict = object.isStream() ? object.getDict() : object;
    if (dict.isDictionary()) {
        fn(object);
        for (const std::string &key : dict.getKeys()) {
            QPDFObjectHandle child = dict.getKey(key);
            if (!child.isIndirect()) visit(child, fn, depth + 1);
        }
    } else if (object.isArray()) {
        for (const QPDFObjectHandle &item : object.getArrayAsVector())
            if (!item.isIndirect()) visit(item, fn, depth + 1);
    }
}

// getAllObjects() leaves out direct objects, such as an ExtGState inside a page's resources.
void forEachDictionary(QPDF &pdf, const std::function<void(QPDFObjectHandle)> &fn)
{
    for (QPDFObjectHandle object : pdf.getAllObjects()) visit(object, fn, 0);
}

void dropSignatures(QPDF &pdf, PdfA::Report &report)
{
    bool dropped = false;
    forEachDictionary(pdf, [&](QPDFObjectHandle object) {
        if (!object.isDictionary() || !nameIs(object.getKey("/FT"), "/Sig") || !object.hasKey("/V"))
            return;
        object.removeKey("/V");
        dropped = true;
    });
    QPDFObjectHandle root = pdf.getRoot();
    if (root.hasKey("/Perms")) {
        root.removeKey("/Perms");
        dropped = true;
    }
    if (dropped)
        note(report.changes, QCoreApplication::translate("PdfA", "Digital signatures were removed, because "
                                                                 "rewriting the file breaks them. Their "
                                                                 "visible stamps stay."));
}

void fixForms(QPDF &pdf, PdfA::Report &report)
{
    QPDFObjectHandle form = pdf.getRoot().getKey("/AcroForm");
    if (!form.isDictionary()) return;
    if (form.hasKey("/XFA")) {
        form.removeKey("/XFA");
        note(report.changes, QCoreApplication::translate("PdfA", "The XFA form data was removed. The form "
                                                                 "fields stay."));
    }
    QPDFAcroFormDocumentHelper(pdf).generateAppearancesIfNeeded();
    if (form.hasKey("/NeedAppearances")) {
        form.removeKey("/NeedAppearances");
        ++report.repairs;
    }
}

bool forbiddenAction(QPDFObjectHandle action)
{
    static const std::set<std::string> allowed { "/GoTo", "/GoToR", "/GoToE", "/Thread", "/URI",
                                                 "/SubmitForm", "/Named" };
    static const std::set<std::string> named { "/NextPage", "/PrevPage", "/FirstPage", "/LastPage" };
    if (!action.isDictionary() || !action.getKey("/S").isName()) return false;
    const std::string kind = action.getKey("/S").getName();
    if (!allowed.count(kind)) return true;
    return kind == "/Named" && !(action.getKey("/N").isName() && named.count(action.getKey("/N").getName()));
}

void dropActions(QPDF &pdf, PdfA::Report &report)
{
    bool dropped = false;
    forEachDictionary(pdf, [&](QPDFObjectHandle object) {
        QPDFObjectHandle dict = object.isStream() ? object.getDict() : object;
        if (dict.hasKey("/AA")) {
            dict.removeKey("/AA");
            dropped = true;
        }
        for (const char *key : { "/A", "/OpenAction", "/Next" }) {
            QPDFObjectHandle value = dict.getKey(key);
            if (forbiddenAction(value)) {
                dict.removeKey(key);
                dropped = true;
            } else if (value.isArray()) {
                QPDFObjectHandle kept = QPDFObjectHandle::newArray();
                for (const QPDFObjectHandle &item : value.getArrayAsVector())
                    if (!forbiddenAction(item)) kept.appendItem(item);
                if (kept.getArrayNItems() != value.getArrayNItems()) {
                    dict.replaceKey(key, kept);
                    dropped = true;
                }
            }
        }
    });
    QPDFObjectHandle names = pdf.getRoot().getKey("/Names");
    for (const char *key : { "/JavaScript", "/AlternatePresentations" })
        if (names.isDictionary() && names.hasKey(key)) {
            names.removeKey(key);
            dropped = true;
        }
    if (dropped)
        note(report.changes, QCoreApplication::translate("PdfA", "Scripts and actions that PDF/A does not "
                                                                 "allow were removed."));
}

enum Flag { Invisible = 1, Hidden = 2, Print = 4, NoView = 32, ToggleNoView = 256 };

// PDF/A keeps only the normal appearance.
bool appearanceFits(QPDFObjectHandle annot, bool button, PdfA::Report &report)
{
    QPDFObjectHandle ap = annot.getKey("/AP");
    if (!ap.isDictionary()) return false;
    for (const char *key : { "/R", "/D" })
        if (ap.hasKey(key)) {
            ap.removeKey(key);
            ++report.repairs;
        }
    QPDFObjectHandle normal = ap.getKey("/N");
    return button ? normal.isDictionary() : normal.isStream();
}

std::vector<double> numbersOf(QPDFObjectHandle array)
{
    std::vector<double> out;
    if (array.isArray())
        for (const QPDFObjectHandle &v : array.getArrayAsVector())
            out.push_back(v.isNumber() ? v.getNumericValue() : 0);
    return out;
}

void ensureAppearance(QPDF &pdf, QPDFAcroFormDocumentHelper &forms, QPDFObjectHandle annot,
                      const std::string &subtype, int page, PdfA::Report &report)
{
    const std::vector<double> rect = numbersOf(annot.getKey("/Rect"));
    const bool empty = rect.size() == 4 && rect[0] == rect[2] && rect[1] == rect[3];
    if (subtype == "/Popup" || subtype == "/Link" || empty) {
        if (annot.hasKey("/AP")) appearanceFits(annot, false, report);
        return;
    }
    QPDFAnnotationObjectHelper helper(annot);
    const bool widget = subtype == "/Widget";
    QPDFFormFieldObjectHelper field = widget ? forms.getFieldForAnnotation(helper)
                                             : QPDFFormFieldObjectHelper();
    const bool inForm = widget && field.getObjectHandle().isDictionary();
    const bool button = inForm && field.getFieldType() == "/Btn";
    if (appearanceFits(annot, button, report)) return;
    if (!annot.hasKey("/AP")) {
        try {
            if (inForm && (field.isText() || field.isChoice())) field.generateAppearance(helper);
            // An unsigned signature field shows nothing, which an empty appearance keeps.
            else if (!widget || (inForm && field.getFieldType() == "/Sig")) PdfAAppearances::draw(pdf, annot);
        } catch (const std::exception &ex) {
            qWarning() << "[PDF/A] cannot draw an annotation:" << ex.what();
        }
        if (appearanceFits(annot, button, report)) {
            ++report.repairs;
            return;
        }
    }
    note(report.blockers, QCoreApplication::translate("PdfA", "Page %1 has an annotation without the "
                                                              "appearance PDF/A requires.").arg(page));
}

// A field's appearance that leaves out its resources reads them from the form, which PDF/A does not allow.
void lendFormResources(QPDF &pdf, QPDFObjectHandle annot, PdfA::Report &report)
{
    QPDFObjectHandle form = pdf.getRoot().getKey("/AcroForm");
    QPDFObjectHandle resources = form.isDictionary() ? form.getKey("/DR") : QPDFObjectHandle::newNull();
    QPDFObjectHandle ap = annot.getKey("/AP");
    if (!resources.isDictionary() || !ap.isDictionary()) return;
    QPDFObjectHandle normal = ap.getKey("/N");
    std::vector<QPDFObjectHandle> streams;
    if (normal.isStream()) streams.push_back(normal);
    else if (normal.isDictionary())
        for (const std::string &state : normal.getKeys()) streams.push_back(normal.getKey(state));
    for (QPDFObjectHandle stream : streams)
        if (stream.isStream() && !stream.getDict().getKey("/Resources").isDictionary()) {
            stream.getDict().replaceKey("/Resources", resources);
            ++report.repairs;
        }
}

bool keepAnnotation(QPDF &pdf, QPDFAcroFormDocumentHelper &forms, QPDFObjectHandle annot, int page,
                    PdfA::Report &report)
{
    static const std::set<std::string> media { "/3D", "/Sound", "/Screen", "/Movie", "/RichMedia" };
    const std::string subtype = annot.getKey("/Subtype").isName() ? annot.getKey("/Subtype").getName() : "";
    QPDFObjectHandle flagsObj = annot.getKey("/F");
    const int flags = flagsObj.isInteger() ? int(flagsObj.getIntValue()) : 0;
    if (media.count(subtype)) {
        note(report.changes, QCoreApplication::translate("PdfA", "Media, sound and 3D content was removed. "
                                                                 "PDF/A cannot hold it."));
        return false;
    }
    if (subtype == "/FileAttachment") {
        note(report.blockers, QCoreApplication::translate("PdfA", "The document has file attachments, "
                                                                  "which PDF/A-2 does not allow."));
        return true;
    }
    if (subtype != "/Popup" && (flags & (Hidden | NoView))) {
        note(report.changes, QCoreApplication::translate("PdfA", "Hidden annotations were removed. PDF/A "
                                                                 "does not allow hidden content."));
        return false;
    }
    const int wanted = (flags | Print) & ~(Invisible | Hidden | NoView | ToggleNoView);
    if ((subtype != "/Popup" || flagsObj.isInteger()) && (!flagsObj.isInteger() || wanted != flags)) {
        annot.replaceKey("/F", QPDFObjectHandle::newInteger(wanted));
        ++report.repairs;
    }
    if (subtype == "/Widget") lendFormResources(pdf, annot, report);
    ensureAppearance(pdf, forms, annot, subtype, page, report);
    return true;
}

void removeAnnotations(QPDFObjectHandle page, const std::set<QPDFObjGen> &dropped,
                       std::set<QPDFObjGen> &droppedFields)
{
    QPDFObjectHandle annots = page.getKey("/Annots");
    QPDFObjectHandle kept = QPDFObjectHandle::newArray();
    for (QPDFObjectHandle annot : annots.getArrayAsVector()) {
        const bool gone = dropped.count(annot.getObjGen())
            || (annot.isDictionary() && dropped.count(annot.getKey("/Parent").getObjGen())
                && nameIs(annot.getKey("/Subtype"), "/Popup"));
        if (!gone) {
            kept.appendItem(annot);
            continue;
        }
        if (!nameIs(annot.getKey("/Subtype"), "/Widget")) continue;
        QPDFObjectHandle parent = annot.getKey("/Parent");
        if (annot.hasKey("/FT") || annot.hasKey("/T") || !parent.isDictionary()) {
            droppedFields.insert(annot.getObjGen());
        } else if (parent.getKey("/Kids").isArray()) {
            QPDFObjectHandle kids = QPDFObjectHandle::newArray();
            for (const QPDFObjectHandle &kid : parent.getKey("/Kids").getArrayAsVector())
                if (kid.getObjGen() != annot.getObjGen()) kids.appendItem(kid);
            parent.replaceKey("/Kids", kids);
        }
    }
    if (kept.getArrayNItems() == 0) page.removeKey("/Annots");
    else page.replaceKey("/Annots", kept);
}

void fixAnnotations(QPDF &pdf, PdfA::Report &report)
{
    QPDFAcroFormDocumentHelper forms(pdf);
    std::set<QPDFObjGen> droppedFields;
    std::vector<QPDFPageObjectHelper> pages = QPDFPageDocumentHelper(pdf).getAllPages();
    for (size_t p = 0; p < pages.size(); ++p) {
        QPDFObjectHandle page = pages[p].getObjectHandle();
        QPDFObjectHandle annots = page.getKey("/Annots");
        if (!annots.isArray()) continue;
        // Annotations are told apart by object, so a direct one gets an object of its own.
        for (int i = 0; i < annots.getArrayNItems(); ++i)
            if (annots.getArrayItem(i).isDictionary() && !annots.getArrayItem(i).isIndirect())
                annots.setArrayItem(i, pdf.makeIndirectObject(annots.getArrayItem(i)));
        std::set<QPDFObjGen> dropped;
        for (QPDFObjectHandle annot : annots.getArrayAsVector())
            if (annot.isDictionary() && !keepAnnotation(pdf, forms, annot, int(p) + 1, report))
                dropped.insert(annot.getObjGen());
        if (!dropped.empty()) removeAnnotations(page, dropped, droppedFields);
    }
    if (!droppedFields.empty()) forms.removeFormFields(droppedFields);
}

void flatten(QPDFObjectHandle order, std::set<QPDFObjGen> &into, int depth = 0)
{
    if (!order.isArray() || depth > 16) return;
    for (const QPDFObjectHandle &item : order.getArrayAsVector()) {
        if (item.isArray()) flatten(item, into, depth + 1);
        else if (item.isDictionary()) into.insert(item.getObjGen());
    }
}

void fixOptionalContent(QPDF &pdf, PdfA::Report &report)
{
    QPDFObjectHandle properties = pdf.getRoot().getKey("/OCProperties");
    if (!properties.isDictionary()) return;
    QPDFObjectHandle groups = properties.getKey("/OCGs");
    std::vector<QPDFObjectHandle> configs { properties.getKey("/D") };
    if (properties.getKey("/Configs").isArray())
        for (const QPDFObjectHandle &c : properties.getKey("/Configs").getArrayAsVector()) configs.push_back(c);

    std::set<std::string> names;
    for (size_t i = 0; i < configs.size(); ++i) {
        QPDFObjectHandle config = configs[i];
        if (!config.isDictionary()) continue;
        QPDFObjectHandle name = config.getKey("/Name");
        if (!name.isString() || !names.insert(name.getUTF8Value()).second) {
            const std::string fresh = i == 0 ? "Default" : "Configuration " + std::to_string(i);
            config.replaceKey("/Name", QPDFObjectHandle::newUnicodeString(fresh));
            names.insert(fresh);
            ++report.repairs;
        }
        if (config.hasKey("/AS")) {
            config.removeKey("/AS");
            ++report.repairs;
        }
        QPDFObjectHandle order = config.getKey("/Order");
        if (!order.isArray() || !groups.isArray()) continue;
        std::set<QPDFObjGen> listed;
        flatten(order, listed);
        for (const QPDFObjectHandle &group : groups.getArrayAsVector())
            if (group.isDictionary() && !listed.count(group.getObjGen())) {
                order.appendItem(group);
                ++report.repairs;
            }
    }
}

void fixObjects(QPDF &pdf, PdfA::Report &report)
{
    forEachDictionary(pdf, [&](QPDFObjectHandle object) {
        const bool stream = object.isStream();
        QPDFObjectHandle dict = stream ? object.getDict() : object;
        const auto drop = [&](const char *key) {
            if (!dict.hasKey(key)) return;
            dict.removeKey(key);
            ++report.repairs;
        };
        if (stream) {
            if (dict.hasKey("/F") || dict.hasKey("/FFilter") || dict.hasKey("/FDecodeParms"))
                note(report.blockers, QCoreApplication::translate("PdfA", "The document draws content from "
                                                                          "other files, which PDF/A does "
                                                                          "not allow."));
            QPDFObjectHandle subtype = dict.getKey("/Subtype");
            if (nameIs(subtype, "/Image")) {
                drop("/Alternates");
                drop("/OPI");
                QPDFObjectHandle interpolate = dict.getKey("/Interpolate");
                if (interpolate.isBool() && interpolate.getBoolValue()) drop("/Interpolate");
            } else if (nameIs(subtype, "/Form")) {
                drop("/OPI");
                drop("/Ref");
                if (nameIs(dict.getKey("/Subtype2"), "/PS"))
                    note(report.blockers, QCoreApplication::translate("PdfA", "The document contains "
                                                                              "PostScript, which PDF/A "
                                                                              "does not allow."));
            } else if (nameIs(subtype, "/PS")) {
                note(report.blockers, QCoreApplication::translate("PdfA", "The document contains "
                                                                          "PostScript, which PDF/A does "
                                                                          "not allow."));
            }
            return;
        }
        drop("/TR");
        QPDFObjectHandle tr2 = dict.getKey("/TR2");
        if (!tr2.isNull() && !nameIs(tr2, "/Default")) {
            dict.replaceKey("/TR2", QPDFObjectHandle::newName("/Default"));
            ++report.repairs;
        }
    });
}

QDateTime parseDate(const QString &text)
{
    static const QRegularExpression pattern(QStringLiteral(
        "^(?:D:)?(\\d{4})(\\d{2})?(\\d{2})?(\\d{2})?(\\d{2})?(\\d{2})?(?:([Zz+-])(\\d{2})?'?(\\d{2})?'?)?"));
    const QRegularExpressionMatch m = pattern.match(text.trimmed());
    if (!m.hasMatch()) return {};
    const auto part = [&m](int i, int fallback) {
        return m.captured(i).isEmpty() ? fallback : m.captured(i).toInt();
    };
    const QDate date(part(1, 1), part(2, 1), part(3, 1));
    const QTime time(part(4, 0), part(5, 0), part(6, 0));
    int offset = (part(8, 0) * 60 + part(9, 0)) * 60;
    if (m.captured(7) == QLatin1String("-")) offset = -offset;
    const QDateTime out(date, time, QTimeZone::fromSecondsAheadOfUtc(offset));
    return out.isValid() ? out : QDateTime();
}

QString zone(const QDateTime &when, const QString &separator, const QString &end)
{
    const int offset = when.offsetFromUtc() / 60;
    return QStringLiteral("%1%2%3%4%5").arg(offset < 0 ? u'-' : u'+')
        .arg(qAbs(offset) / 60, 2, 10, QChar(u'0')).arg(separator)
        .arg(qAbs(offset) % 60, 2, 10, QChar(u'0')).arg(end);
}

QString pdfDate(const QDateTime &when)
{
    return QStringLiteral("D:") + when.toString(QStringLiteral("yyyyMMddHHmmss"))
         + zone(when, QStringLiteral("'"), QStringLiteral("'"));
}

QString xmpDate(const QDateTime &when)
{
    return when.toString(QStringLiteral("yyyy-MM-ddTHH:mm:ss")) + zone(when, QStringLiteral(":"), {});
}

QString infoText(QPDFObjectHandle info, const char *key)
{
    QPDFObjectHandle value = info.isDictionary() ? info.getKey(key) : QPDFObjectHandle::newNull();
    return value.isString() ? QString::fromStdString(value.getUTF8Value()) : QString();
}

QByteArray xmpFor(QPDFObjectHandle info)
{
    const auto esc = [](const QString &s) { return s.toHtmlEscaped(); };
    QString body;
    const auto alt = [&](const char *tag, const QString &value) {
        if (!value.isEmpty())
            body += QStringLiteral("   <%1><rdf:Alt><rdf:li xml:lang=\"x-default\">%2</rdf:li></rdf:Alt></%1>\n")
                        .arg(QLatin1String(tag), esc(value));
    };
    const auto plain = [&](const char *tag, const QString &value) {
        if (!value.isEmpty()) body += QStringLiteral("   <%1>%2</%1>\n").arg(QLatin1String(tag), esc(value));
    };
    alt("dc:title", infoText(info, "/Title"));
    if (const QString author = infoText(info, "/Author"); !author.isEmpty())
        body += QStringLiteral("   <dc:creator><rdf:Seq><rdf:li>%1</rdf:li></rdf:Seq></dc:creator>\n")
                    .arg(esc(author));
    alt("dc:description", infoText(info, "/Subject"));
    plain("pdf:Keywords", infoText(info, "/Keywords"));
    plain("xmp:CreatorTool", infoText(info, "/Creator"));
    plain("pdf:Producer", infoText(info, "/Producer"));
    for (const auto &[key, tag] : { std::pair { "/CreationDate", "xmp:CreateDate" },
                                    std::pair { "/ModDate", "xmp:ModifyDate" } }) {
        const QDateTime when = parseDate(infoText(info, key));
        if (when.isValid()) plain(tag, xmpDate(when));
    }

    const QString xmp = QStringLiteral(
        "<?xpacket begin=\"\xEF\xBB\xBF\" id=\"W5M0MpCehiHzreSzNTczkc9d\"?>\n"
        "<x:xmpmeta xmlns:x=\"adobe:ns:meta/\">\n"
        " <rdf:RDF xmlns:rdf=\"http://www.w3.org/1999/02/22-rdf-syntax-ns#\">\n"
        "  <rdf:Description rdf:about=\"\"\n"
        "    xmlns:pdfaid=\"http://www.aiim.org/pdfa/ns/id/\"\n"
        "    xmlns:dc=\"http://purl.org/dc/elements/1.1/\"\n"
        "    xmlns:xmp=\"http://ns.adobe.com/xap/1.0/\"\n"
        "    xmlns:pdf=\"http://ns.adobe.com/pdf/1.3/\">\n"
        "   <pdfaid:part>2</pdfaid:part>\n"
        "   <pdfaid:conformance>B</pdfaid:conformance>\n"
        "%1"
        "  </rdf:Description>\n"
        " </rdf:RDF>\n"
        "</x:xmpmeta>\n"
        "<?xpacket end=\"w\"?>").arg(body);
    return xmp.toUtf8();
}

// The document information and the XMP packet have to say the same.
void tidyInfo(QPDFObjectHandle info)
{
    if (!info.isDictionary()) return;
    for (const std::string &key : info.getKeys()) {
        QPDFObjectHandle value = info.getKey(key);
        if (!value.isString() || value.getUTF8Value().empty()) info.removeKey(key);
    }
    for (const char *key : { "/CreationDate", "/ModDate" }) {
        const QDateTime when = parseDate(infoText(info, key));
        if (when.isValid()) info.replaceKey(key, QPDFObjectHandle::newString(pdfDate(when).toStdString()));
        else info.removeKey(key);
    }
    info.replaceKey("/ModDate",
                    QPDFObjectHandle::newString(pdfDate(QDateTime::currentDateTime()).toStdString()));
}

void writeMetadata(QPDF &pdf, PdfA::Report &report)
{
    QPDFObjectHandle root = pdf.getRoot();
    QPDFObjectHandle info = pdf.getTrailer().getKey("/Info");
    QPDFObjectHandle current = root.getKey("/Metadata");
    if (current.isStream() && !current.getDict().hasKey("/Filter")) {
        const std::shared_ptr<Buffer> data = current.getRawStreamData();
        const QByteArray wanted = xmpFor(info);
        if (data && QByteArray(reinterpret_cast<const char *>(data->getBuffer()),
                               qsizetype(data->getSize())) == wanted)
            return;
    }
    tidyInfo(info);
    const QByteArray xmp = xmpFor(info);
    QPDFObjectHandle stream = pdf.newStream(std::string(xmp.constData(), size_t(xmp.size())));
    stream.getDict().replaceKey("/Type", QPDFObjectHandle::newName("/Metadata"));
    stream.getDict().replaceKey("/Subtype", QPDFObjectHandle::newName("/XML"));
    root.replaceKey("/Metadata", stream);
    ++report.repairs;
}

void writeOutputIntent(QPDF &pdf, PdfA::Report &report)
{
    QPDFObjectHandle root = pdf.getRoot();
    QPDFObjectHandle intents = root.getKey("/OutputIntents");
    if (intents.isArray() && intents.getArrayNItems() == 1) {
        QPDFObjectHandle intent = intents.getArrayItem(0);
        QPDFObjectHandle profile = intent.isDictionary() ? intent.getKey("/DestOutputProfile")
                                                         : QPDFObjectHandle::newNull();
        if (nameIs(intent.getKey("/S"), "/GTS_PDFA1") && profile.isStream()
                && profile.getDict().getKey("/N").isInteger() && profile.getDict().getKey("/N").getIntValue() == 3)
            return;
    }
    const QByteArray icc = QColorSpace(QColorSpace::SRgb).iccProfile();
    if (icc.isEmpty()) {
        note(report.blockers, QCoreApplication::translate("PdfA", "No colour profile is available to write "
                                                                  "PDF/A."));
        return;
    }
    QPDFObjectHandle profile = pdf.newStream(std::string(icc.constData(), size_t(icc.size())));
    profile.getDict().replaceKey("/N", QPDFObjectHandle::newInteger(3));
    const std::string name = "sRGB IEC61966-2.1";
    QPDFObjectHandle intent = QPDFObjectHandle::newDictionary();
    intent.replaceKey("/Type", QPDFObjectHandle::newName("/OutputIntent"));
    intent.replaceKey("/S", QPDFObjectHandle::newName("/GTS_PDFA1"));
    intent.replaceKey("/OutputConditionIdentifier", QPDFObjectHandle::newString(name));
    intent.replaceKey("/Info", QPDFObjectHandle::newString(name));
    intent.replaceKey("/RegistryName", QPDFObjectHandle::newString("http://www.color.org"));
    intent.replaceKey("/DestOutputProfile", profile);
    QPDFObjectHandle list = QPDFObjectHandle::newArray();
    list.appendItem(pdf.makeIndirectObject(intent));
    root.replaceKey("/OutputIntents", list);
    ++report.repairs;
}

QString pageList(const std::set<int> &pages)
{
    QStringList shown;
    for (int page : pages) {
        if (shown.size() == 8) {
            shown << QStringLiteral("…");
            break;
        }
        shown << QString::number(page);
    }
    return shown.join(QStringLiteral(", "));
}

}

void PdfA::convert(QPDF &pdf, Report &report)
{
    dropSignatures(pdf, report);
    fixForms(pdf, report);
    dropActions(pdf, report);
    fixAnnotations(pdf, report);
    fixOptionalContent(pdf, report);
    fixObjects(pdf, report);

    QPDFObjectHandle names = pdf.getRoot().getKey("/Names");
    if (names.isDictionary() && names.hasKey("/EmbeddedFiles"))
        note(report.blockers, QCoreApplication::translate("PdfA", "The document has file attachments, "
                                                                  "which PDF/A-2 does not allow."));

    // Fonts last: the appearances drawn above show text too.
    const PdfAContent::Use use = PdfAContent::scan(pdf);
    PdfAFonts::embed(pdf, use, report);
    PdfAGlyphs::check(pdf, use, report);
    if (!use.cmykPages.empty())
        note(report.blockers, QCoreApplication::translate("PdfA", "CMYK colours are used (page %1). PDF/A "
                                                                  "would need a CMYK colour profile for "
                                                                  "them, which OpenPDF Studio does not "
                                                                  "include.")
                                  .arg(pageList(use.cmykPages)));

    writeMetadata(pdf, report);
    writeOutputIntent(pdf, report);
}

#endif
