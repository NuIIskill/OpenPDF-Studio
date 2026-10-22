#pragma once

#include <QProxyStyle>

/// Draws check boxes and radio buttons in the accent blue, readable in both themes.
class CheckBoxStyle : public QProxyStyle
{
public:
    int pixelMetric(PixelMetric metric, const QStyleOption *option,
                    const QWidget *widget) const override;
    void drawPrimitive(PrimitiveElement element, const QStyleOption *option, QPainter *p,
                       const QWidget *widget) const override;
};
