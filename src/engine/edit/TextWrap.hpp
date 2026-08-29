#pragma once

#include <QString>
#include <QStringList>

#include <functional>

namespace TextWrap {

QStringList lines(const QString &text, double maxWidth,
                  const std::function<double(QChar)> &advance,
                  double charSpacing = 0.0);

}
