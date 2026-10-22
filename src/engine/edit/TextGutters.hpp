#pragma once

#include <QList>
#include <QPair>

/// Finds the white channels that run down between text columns and table cells.
namespace TextGutters {

struct Row {
    double baseline { 0.0 };
    double size     { 0.0 };
    QList<QPair<double, double>> ink;
};

bool separates(const QList<Row> &rows, int row, double gapLeft, double gapRight);

}
