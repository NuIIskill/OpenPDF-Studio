#include "engine/forms/FormSpec.hpp"

QList<int> FormSpec::pages() const
{
    QList<int> out = filePages;
    for (const Place &place : places)
        if (!out.contains(place.page)) out.append(place.page);
    return out;
}

QDataStream &operator<<(QDataStream &out, const FormSpec &spec)
{
    out << spec.original << spec.name << qint32(spec.kind) << qint32(spec.places.size());
    for (const FormSpec::Place &p : spec.places)
        out << qint32(p.page) << p.bounds << p.exportValue << p.from;
    out << spec.filePages << spec.tooltip << qint32(spec.flags) << spec.options << spec.fontSize
        << spec.keystroke << spec.format << spec.validate << spec.calculate << spec.removed;
    return out;
}

QDataStream &operator>>(QDataStream &in, FormSpec &spec)
{
    qint32 kind = 0, count = 0, flags = 0;
    in >> spec.original >> spec.name >> kind >> count;
    spec.kind = FormSpec::Kind(qBound(0, int(kind), int(FormSpec::Kind::ListBox)));
    spec.places.clear();
    for (qint32 i = 0; i < count && in.status() == QDataStream::Ok; ++i) {
        FormSpec::Place p;
        qint32 page = -1;
        in >> page >> p.bounds >> p.exportValue >> p.from;
        p.page = page;
        spec.places.append(p);
    }
    in >> spec.filePages >> spec.tooltip >> flags >> spec.options >> spec.fontSize
       >> spec.keystroke >> spec.format >> spec.validate >> spec.calculate >> spec.removed;
    spec.flags = flags;
    return in;
}
