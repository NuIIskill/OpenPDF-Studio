#include "engine/export/PdfAAppearances.hpp"

#ifdef HAVE_QPDF

#include <QByteArray>

#include <cmath>
#include <vector>

namespace {

struct Colour {
    bool   set { false };
    double r { 0 }, g { 0 }, b { 0 };
};

std::vector<double> numbers(QPDFObjectHandle array)
{
    std::vector<double> out;
    if (!array.isArray()) return out;
    for (const QPDFObjectHandle &item : array.getArrayAsVector())
        if (item.isNumber()) out.push_back(item.getNumericValue());
    return out;
}

Colour colourOf(QPDFObjectHandle array)
{
    const std::vector<double> c = numbers(array);
    if (c.size() == 1) return { true, c[0], c[0], c[0] };
    if (c.size() == 3) return { true, c[0], c[1], c[2] };
    // CMYK would need a CMYK profile; the appearance is drawn in RGB instead.
    if (c.size() == 4)
        return { true, (1 - c[0]) * (1 - c[3]), (1 - c[1]) * (1 - c[3]), (1 - c[2]) * (1 - c[3]) };
    return {};
}

QByteArray num(double v)
{
    QByteArray out = QByteArray::number(v, 'f', 3);
    while (out.endsWith('0')) out.chop(1);
    if (out.endsWith('.')) out.chop(1);
    return out == "-0" ? QByteArray("0") : out;
}

QByteArray rgb(const Colour &c, const char *op)
{
    return num(c.r) + ' ' + num(c.g) + ' ' + num(c.b) + ' ' + op + '\n';
}

double borderWidth(QPDFObjectHandle annot)
{
    QPDFObjectHandle bs = annot.getKey("/BS");
    if (bs.isDictionary() && bs.getKey("/W").isNumber()) return bs.getKey("/W").getNumericValue();
    const std::vector<double> border = numbers(annot.getKey("/Border"));
    return border.size() >= 3 ? border[2] : 1.0;
}

class Path
{
public:
    Path(double x0, double y0) : m_x0(x0), m_y0(y0) {}

    QByteArray point(double x, double y, const char *op) const
    {
        return num(x - m_x0) + ' ' + num(y - m_y0) + ' ' + op + '\n';
    }

private:
    double m_x0, m_y0;
};

QByteArray ellipse(double x, double y, double w, double h)
{
    const double k = 0.5523, rx = w / 2, ry = h / 2, cx = x + rx, cy = y + ry;
    QByteArray o = num(cx + rx) + ' ' + num(cy) + " m\n";
    const auto curve = [&](double x1, double y1, double x2, double y2, double x3, double y3) {
        o += num(x1) + ' ' + num(y1) + ' ' + num(x2) + ' ' + num(y2) + ' ' + num(x3) + ' '
           + num(y3) + " c\n";
    };
    curve(cx + rx, cy + k * ry, cx + k * rx, cy + ry, cx, cy + ry);
    curve(cx - k * rx, cy + ry, cx - rx, cy + k * ry, cx - rx, cy);
    curve(cx - rx, cy - k * ry, cx - k * rx, cy - ry, cx, cy - ry);
    curve(cx + k * rx, cy - ry, cx + rx, cy - k * ry, cx + rx, cy);
    return o;
}

QByteArray textMarkup(const std::string &subtype, const std::vector<double> &q, const Path &path)
{
    QByteArray o;
    for (size_t i = 0; i + 8 <= q.size(); i += 8) {
        // Corners in reading order: top left, top right, bottom left, bottom right.
        const double tlx = q[i], tly = q[i + 1], trx = q[i + 2], try_ = q[i + 3];
        const double blx = q[i + 4], bly = q[i + 5], brx = q[i + 6], bry = q[i + 7];
        const double height = std::hypot(tlx - blx, tly - bly);
        if (subtype == "/Highlight") {
            o += path.point(tlx, tly, "m") + path.point(trx, try_, "l")
               + path.point(brx, bry, "l") + path.point(blx, bly, "l") + "h f\n";
            continue;
        }
        const double width = qMax(0.5, height / 16);
        o += num(width) + " w\n";
        if (subtype == "/Underline") {
            const double t = 0.08;
            o += path.point(blx + (tlx - blx) * t, bly + (tly - bly) * t, "m")
               + path.point(brx + (trx - brx) * t, bry + (try_ - bry) * t, "l") + "S\n";
        } else if (subtype == "/StrikeOut") {
            o += path.point((tlx + blx) / 2, (tly + bly) / 2, "m")
               + path.point((trx + brx) / 2, (try_ + bry) / 2, "l") + "S\n";
        } else {
            const double length = std::hypot(brx - blx, bry - bly);
            const int steps = qMax(2, int(length / qMax(1.0, height / 4)));
            for (int s = 0; s <= steps; ++s) {
                const double t = double(s) / steps, lift = (s % 2) ? height / 8 : 0;
                const double ux = (tlx - blx) / qMax(1e-6, height), uy = (tly - bly) / qMax(1e-6, height);
                o += path.point(blx + (brx - blx) * t + ux * lift, bly + (bry - bly) * t + uy * lift,
                                s == 0 ? "m" : "l");
            }
            o += "S\n";
        }
    }
    return o;
}

}

bool PdfAAppearances::draw(QPDF &pdf, QPDFObjectHandle annot)
{
    const std::vector<double> rect = numbers(annot.getKey("/Rect"));
    QPDFObjectHandle subtypeObj = annot.getKey("/Subtype");
    if (rect.size() != 4 || !subtypeObj.isName()) return false;
    const double x0 = qMin(rect[0], rect[2]), y0 = qMin(rect[1], rect[3]);
    const double w = std::abs(rect[2] - rect[0]), h = std::abs(rect[3] - rect[1]);
    if (w <= 0 || h <= 0) return false;
    const std::string subtype = subtypeObj.getName();
    const Path path(x0, y0);
    Colour colour = colourOf(annot.getKey("/C"));
    const Colour interior = colourOf(annot.getKey("/IC"));
    const double bw = borderWidth(annot);
    bool multiply = false;
    QByteArray ops;

    if (subtype == "/Widget") {
    } else if (subtype == "/Text") {
        if (!colour.set) colour = { true, 1.0, 0.82, 0.0 };
        ops = "0.3 G 1 w\n" + rgb(colour, "rg") + "0.5 0.5 " + num(w - 1) + ' ' + num(h - 1) + " re B\n";
        for (int i = 1; i <= 3; ++i) {
            const double y = h * (1.0 - 0.25 * i);
            ops += num(w * 0.2) + ' ' + num(y) + " m " + num(w * 0.8) + ' ' + num(y) + " l S\n";
        }
    } else if (subtype == "/Highlight" || subtype == "/Underline" || subtype == "/StrikeOut"
               || subtype == "/Squiggly") {
        const std::vector<double> quads = numbers(annot.getKey("/QuadPoints"));
        if (quads.size() < 8) return false;
        if (!colour.set)
            colour = subtype == "/Highlight" ? Colour { true, 1.0, 1.0, 0.0 } : Colour { true, 0, 0, 0 };
        multiply = subtype == "/Highlight";
        ops = rgb(colour, multiply ? "rg" : "RG") + textMarkup(subtype, quads, path);
    } else if (subtype == "/Square" || subtype == "/Circle") {
        const double in = colour.set ? bw / 2 : 0;
        const char *paint = colour.set && interior.set ? "B" : colour.set ? "S" : interior.set ? "f" : "n";
        ops = num(bw) + " w\n";
        if (colour.set) ops += rgb(colour, "RG");
        if (interior.set) ops += rgb(interior, "rg");
        ops += subtype == "/Square"
             ? num(in) + ' ' + num(in) + ' ' + num(w - 2 * in) + ' ' + num(h - 2 * in) + " re\n"
             : ellipse(in, in, w - 2 * in, h - 2 * in);
        ops += QByteArray(paint) + '\n';
    } else if (subtype == "/Ink" || subtype == "/Line" || subtype == "/PolyLine"
               || subtype == "/Polygon") {
        if (!colour.set) return false;
        std::vector<std::vector<double>> strokes;
        if (subtype == "/Ink") {
            QPDFObjectHandle list = annot.getKey("/InkList");
            if (list.isArray())
                for (const QPDFObjectHandle &stroke : list.getArrayAsVector()) strokes.push_back(numbers(stroke));
        } else {
            strokes.push_back(numbers(annot.getKey(subtype == "/Line" ? "/L" : "/Vertices")));
        }
        ops = "1 J 1 j " + num(bw) + " w\n" + rgb(colour, "RG");
        const bool closed = subtype == "/Polygon";
        if (closed && interior.set) ops += rgb(interior, "rg");
        for (const std::vector<double> &s : strokes) {
            for (size_t i = 0; i + 1 < s.size(); i += 2) ops += path.point(s[i], s[i + 1], i == 0 ? "m" : "l");
            if (s.size() >= 4) ops += closed ? (interior.set ? "b\n" : "s\n") : "S\n";
        }
    } else {
        return false;
    }

    QPDFObjectHandle resources = QPDFObjectHandle::newDictionary();
    if (multiply) {
        QPDFObjectHandle gs = QPDFObjectHandle::newDictionary();
        gs.replaceKey("/Type", QPDFObjectHandle::newName("/ExtGState"));
        gs.replaceKey("/BM", QPDFObjectHandle::newName("/Multiply"));
        QPDFObjectHandle states = QPDFObjectHandle::newDictionary();
        states.replaceKey("/GS0", gs);
        resources.replaceKey("/ExtGState", states);
        ops = "/GS0 gs\n" + ops;
    }
    QPDFObjectHandle bbox = QPDFObjectHandle::newArray();
    for (double v : { 0.0, 0.0, w, h }) bbox.appendItem(QPDFObjectHandle::newReal(v, 3));
    QPDFObjectHandle stream = pdf.newStream(("q\n" + ops + "Q\n").toStdString());
    QPDFObjectHandle dict = stream.getDict();
    dict.replaceKey("/Type", QPDFObjectHandle::newName("/XObject"));
    dict.replaceKey("/Subtype", QPDFObjectHandle::newName("/Form"));
    dict.replaceKey("/BBox", bbox);
    dict.replaceKey("/Resources", resources);
    QPDFObjectHandle ap = QPDFObjectHandle::newDictionary();
    ap.replaceKey("/N", stream);
    annot.replaceKey("/AP", ap);
    return true;
}

#endif
