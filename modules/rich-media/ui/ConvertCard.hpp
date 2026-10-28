// SPDX-License-Identifier: LicenseRef-OpenPDF-Business
#pragma once

#include "ui/settings/DownloadControl.hpp"

#include <QCoreApplication>

QT_BEGIN_NAMESPACE
class QVBoxLayout;
QT_END_NAMESPACE

/// The ffmpeg card on the settings page for add-on packages.
class ConvertCard
{
    Q_DECLARE_TR_FUNCTIONS(ConvertCard)

public:
    static void build(QVBoxLayout *card);

private:
    static DownloadControl::Target download();
};
