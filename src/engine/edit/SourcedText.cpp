#include "engine/edit/SourcedText.hpp"

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMimeData>

namespace {

const QString kFormat = QStringLiteral("application/x-openpdfstudio-sourced-text");

QJsonArray runsOf(const QList<TextLayout::Source> &sources)
{
    QJsonArray runs;
    for (qsizetype i = 0; i < sources.size();) {
        qsizetype end = i + 1;
        while (end < sources.size() && sources.at(end) == sources.at(i)) ++end;
        const TextLayout::Source &s = sources.at(i);
        runs.append(QJsonArray{ int(end - i), s.page, s.at.x(), s.at.y(), s.rise });
        i = end;
    }
    return runs;
}

QList<TextLayout::Source> sourcesOf(const QJsonArray &runs)
{
    QList<TextLayout::Source> out;
    for (const QJsonValue run : runs) {
        const QJsonArray r = run.toArray();
        const TextLayout::Source s{ r.at(1).toInt(-1), QPointF(r.at(2).toDouble(), r.at(3).toDouble()),
                                    r.at(4).toDouble() };
        for (int k = 0; k < r.at(0).toInt(); ++k) out.append(s);
    }
    return out;
}

QJsonObject rootOf(const QMimeData *mime, const QString &document)
{
    if (!mime || document.isEmpty() || !mime->hasFormat(kFormat)) return {};
    const QJsonObject root = QJsonDocument::fromJson(mime->data(kFormat)).object();
    // Pages of another document mean nothing here.
    return root.value(QStringLiteral("document")).toString() == document ? root : QJsonObject();
}

QJsonArray numbers(const QList<double> &values)
{
    QJsonArray out;
    for (const double v : values) out.append(v);
    return out;
}

}

QString SourcedText::documentKey(const void *document)
{
    return QStringLiteral("%1:%2").arg(QCoreApplication::applicationPid()).arg(quintptr(document));
}

void SourcedText::write(QMimeData *mime, const QString &document,
                        const QList<TextLayout::Source> &sources, const Box &box)
{
    if (!mime || document.isEmpty()) return;
    QJsonObject root{ { QStringLiteral("document"), document },
                      { QStringLiteral("runs"), runsOf(sources) } };
    if (!box.size.isEmpty()) {
        QJsonArray lines;
        for (qsizetype i = 0; i < box.lines.size(); ++i) {
            const TextLayout::OriginalLine &l = box.lines.at(i);
            lines.append(QJsonObject{ { QStringLiteral("text"), l.text },
                                      { QStringLiteral("baseline"), l.baseline },
                                      { QStringLiteral("x"), numbers(l.charX) },
                                      { QStringLiteral("end"), l.endX },
                                      { QStringLiteral("top"), l.rect.top() },
                                      { QStringLiteral("bottom"), l.rect.bottom() },
                                      { QStringLiteral("runs"), runsOf(box.sources.value(i)) } });
        }
        root.insert(QStringLiteral("box"), QJsonObject{ { QStringLiteral("width"), box.size.width() },
                                                        { QStringLiteral("height"), box.size.height() },
                                                        { QStringLiteral("size"), box.fontSizePt },
                                                        { QStringLiteral("lines"), lines } });
    }
    mime->setData(kFormat, QJsonDocument(root).toJson(QJsonDocument::Compact));
}

QList<TextLayout::Source> SourcedText::read(const QMimeData *mime, const QString &document)
{
    const QJsonObject root = rootOf(mime, document);
    if (root.isEmpty()) return {};
    const QList<TextLayout::Source> out = sourcesOf(root.value(QStringLiteral("runs")).toArray());
    if (out.size() != mime->text().size()) return {};
    return out;
}

SourcedText::Box SourcedText::boxAround(QList<TextLayout::OriginalLine> lines,
                                        const QList<QList<TextLayout::Source>> &sources, double fontSizePt)
{
    QRectF bounds;
    for (const TextLayout::OriginalLine &line : std::as_const(lines))
        bounds = bounds.isNull() ? line.rect : bounds.united(line.rect);
    for (TextLayout::OriginalLine &line : lines) {
        line.baseline -= bounds.top();
        for (double &x : line.charX) x -= bounds.left();
        line.endX -= bounds.left();
        line.rect.translate(-bounds.topLeft());
    }
    return { bounds.size(), fontSizePt, lines, sources };
}

SourcedText::Box SourcedText::box(const QMimeData *mime, const QString &document)
{
    const QJsonObject b = rootOf(mime, document).value(QStringLiteral("box")).toObject();
    Box out;
    out.size = QSizeF(b.value(QStringLiteral("width")).toDouble(), b.value(QStringLiteral("height")).toDouble());
    out.fontSizePt = b.value(QStringLiteral("size")).toDouble();
    for (const QJsonValue v : b.value(QStringLiteral("lines")).toArray()) {
        const QJsonObject o = v.toObject();
        TextLayout::OriginalLine l;
        l.text     = o.value(QStringLiteral("text")).toString();
        l.baseline = o.value(QStringLiteral("baseline")).toDouble();
        for (const QJsonValue x : o.value(QStringLiteral("x")).toArray()) l.charX.append(x.toDouble());
        l.endX = o.value(QStringLiteral("end")).toDouble();
        l.objectStart = QList<bool>(l.text.size(), false);
        const QList<TextLayout::Source> sources = sourcesOf(o.value(QStringLiteral("runs")).toArray());
        // A line whose positions or styles do not match its characters makes the whole box unusable.
        if (l.text.isEmpty() || l.charX.size() != l.text.size() || sources.size() != l.text.size()) return {};
        l.rect = QRectF(QPointF(l.charX.first(), o.value(QStringLiteral("top")).toDouble()),
                        QPointF(l.endX, o.value(QStringLiteral("bottom")).toDouble()));
        out.lines.append(l);
        out.sources.append(sources);
    }
    return out;
}
