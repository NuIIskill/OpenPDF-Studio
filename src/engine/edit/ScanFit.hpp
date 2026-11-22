#pragma once

#include "engine/edit/ScanFont.hpp"
#include "engine/edit/ScanLine.hpp"

#include <QFont>
#include <QList>
#include <QString>
#include <QStringList>

/// Sets a face over a scanned line and measures how well it covers the scan.
namespace ScanFit {

/// How much better a line fits bold than regular, also word by word, and the size each weight gives it.
struct Weighed {
    double margin { 0.0 };
    double bias { 0.0 };
    double gray[2] { -1.0, -1.0 };
    double size[2] { 0.0, 0.0 };
    QList<double> words;
};

QFont faceOf(const QString &family, bool bold, bool kerned);

ScanFont::Fit fitLine(const ScanLine::Scan &s, const QStringList &words, const QString &text, const QFont &face,
                      qreal scale, bool fine = false, double stretch = 1.0, double sigma = -1.0,
                      double *printed = nullptr, double blur = 0.82, bool styles = false,
                      QList<double> *perWord = nullptr, double *inked = nullptr, double *soft = nullptr);

double blurOf(const QList<ScanLine::Scan> &scans, const QList<QStringList> &words,
              const QList<ScanFont::Line> &lines, const QFont &regular, const QFont &bold, qreal scale,
              bool bySoftness = false);

Weighed weigh(const ScanLine::Scan &s, const QStringList &words, const QString &text, const QFont &regular,
              const QFont &bold, qreal scale, double sigma);

ScanFont::Fit fitStyled(const ScanLine::Scan &s, const QStringList &words, const QString &text,
                        const QFont &regular, const QFont &bold, qreal scale, double sigma, double nearSize,
                        int weight = 0);

double roundedSize(double size);

}
