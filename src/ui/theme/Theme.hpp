#pragma once

#include <QColor>
#include <QIcon>
#include <QPixmap>
#include <QString>

namespace Theme {

extern bool DarkMode;

extern QColor Primary;
extern QColor IconNormal;
extern QColor IconMuted;
extern QColor IconChecked;
extern QColor IconDisabled;

QString loadStyleSheet();

void apply(const QString &mode);

QPixmap renderSvg(const QString &name, const QColor &color,
                  int size, qreal dpr = 1.0);

QIcon makeIcon(const QString &name,
               const QColor &normal   = IconNormal,
               const QColor &checked  = IconChecked,
               const QColor &disabled = IconDisabled,
               int size = 20);

}
