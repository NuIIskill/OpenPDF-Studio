// SPDX-License-Identifier: LicenseRef-OpenPDF-Business
#pragma once

#include <QPointF>
#include <QRectF>
#include <QSize>
#include <QSizeF>
#include <QString>

/// Geometry and file handling for videos dropped onto a PDF.
namespace MediaDrop {

bool isVideoFile(const QString &path);

QSizeF pageSizeFor(int videoWidth, int videoHeight);

QRectF placementBoundsFor(const QSizeF &pageSize, const QSize &videoPixels,
                          const QPointF &dropPoint);

QString mimeTypeFor(const QString &path);

QString displayNameFor(const QString &original, const QString &actual);

QString addAsOwnPage(const QString &pdfPath, const QString &source, int afterPage,
                     const QString &displayName = QString());

}
