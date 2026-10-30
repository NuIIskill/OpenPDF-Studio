// SPDX-License-Identifier: LicenseRef-OpenPDF-Business
#pragma once

#include "engine/document/WriteGrant.hpp"
#include <QPointF>
#include <QRectF>
#include <QSize>
#include <QSizeF>
#include <QString>
#include <QStringList>

/// Geometry and file handling for videos dropped onto a PDF.
namespace MediaDrop {

const QStringList &videoSuffixes();

bool isVideoFile(const QString &path);

QSizeF pageSizeFor(int videoWidth, int videoHeight);

QRectF placementBoundsFor(const QSizeF &pageSize, const QSize &videoPixels,
                          const QPointF &dropPoint);

QString mimeTypeFor(const QString &path);

QString displayNameFor(const QString &original, const QString &actual);

bool addAsOwnPage(const QString &pdfPath, const QString &source, int afterPage,
                  const WriteGrant &work, const QString &displayName = QString());

bool newDocument(const QString &source, const WriteGrant &outPdf,
                 const QString &displayName = QString());

}
