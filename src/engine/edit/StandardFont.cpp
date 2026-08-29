#include "engine/edit/StandardFont.hpp"

StandardFont::Kind StandardFont::kindOf(const QString &family)
{
    const QString f = family.toLower();
    if (f.contains(QLatin1String("mono")) || f.contains(QLatin1String("courier"))
            || f.contains(QLatin1String("consol")))
        return Kind::Mono;
    const bool serif = (f.contains(QLatin1String("serif"))
                        || f.contains(QLatin1String("times"))
                        || f.contains(QLatin1String("georgia"))
                        || f.contains(QLatin1String("garamond"))
                        || f.contains(QLatin1String("book"))
                        || f.contains(QLatin1String("roman"))
                        || f.contains(QLatin1String("minion"))
                        || f.contains(QLatin1String("cambria")))
                    && !f.contains(QLatin1String("sans"));
    return serif ? Kind::Serif : Kind::Sans;
}

QStringList StandardFont::qtFamilies(Kind kind)
{
    switch (kind) {
    case Kind::Mono:
        return { QStringLiteral("Liberation Mono"), QStringLiteral("Nimbus Mono PS"),
                 QStringLiteral("Courier New"), QStringLiteral("DejaVu Sans Mono"),
                 QStringLiteral("monospace") };
    case Kind::Serif:
        return { QStringLiteral("Liberation Serif"), QStringLiteral("Nimbus Roman"),
                 QStringLiteral("Times New Roman"), QStringLiteral("DejaVu Serif"),
                 QStringLiteral("serif") };
    case Kind::Sans:
        break;
    }
    return { QStringLiteral("Liberation Sans"), QStringLiteral("Nimbus Sans"),
             QStringLiteral("Arial"), QStringLiteral("Helvetica"),
             QStringLiteral("DejaVu Sans"), QStringLiteral("sans-serif") };
}
