#pragma once

#include <QtGlobal>

#include <functional>

QT_BEGIN_NAMESPACE
class QObject;
QT_END_NAMESPACE

/// Asks the system whether OpenPDF Studio opens PDF files and makes it do so.
namespace DefaultPdfApp {

enum class State { Default, NotDefault, Unknown };

void query(QObject *context, std::function<void(State)> done);

bool makeDefault();

bool needsSystemSettings();

}
