#pragma once

#include <QDataStream>
#include <QList>
#include <QRectF>
#include <QString>
#include <QStringList>

/// A form field as designed: a new one, or one of the file changed or removed.
struct FormSpec {
    enum class Kind : qint32 { Text, CheckBox, Radio, ComboBox, ListBox };

    /// One widget of the field; from is where the file has it, empty for a new one.
    struct Place {
        int     page { -1 };
        QRectF  bounds;
        QString exportValue;
        QRectF  from;
        bool operator==(const Place &) const = default;
    };

    QString      original;
    QString      name;
    Kind         kind { Kind::Text };
    QList<Place> places;
    QList<int>   filePages;
    QString      tooltip;
    int          flags { 0 };
    QStringList  options;
    double       fontSize { 0.0 };
    QString      keystroke;
    QString      format;
    QString      validate;
    QString      calculate;
    bool         removed { false };

    QList<int> pages() const;
    bool isNew() const { return original.isEmpty(); }

    bool operator==(const FormSpec &) const = default;
};

QDataStream &operator<<(QDataStream &out, const FormSpec &spec);
QDataStream &operator>>(QDataStream &in, FormSpec &spec);
