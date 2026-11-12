#pragma once

#include "engine/forms/FormSpec.hpp"

#include <QByteArray>
#include <QList>
#include <QString>

#include <string>

/// Builds a document with its form fields as designed, in memory, for the preview and the save to start from.
namespace FormFieldWriter {

bool available();

QByteArray build(const QString &path, const std::string &password, const QList<FormSpec> &specs);

}
