#pragma once

#include "engine/edit/EditSession.hpp"

#include <QByteArray>
#include <QList>

/// Writes the pending edits of a session to bytes and reads them back, for the change log.
namespace EditCodec {

QByteArray encode(const QList<EditSession::Edit> &edits);
bool       decode(const QByteArray &data, QList<EditSession::Edit> *edits);

QByteArray encode(const QList<EditSession::DrawStroke> &strokes);
bool       decode(const QByteArray &data, QList<EditSession::DrawStroke> *strokes);

QByteArray encode(const QList<EditSession::ImageChange> &changes);
bool       decode(const QByteArray &data, QList<EditSession::ImageChange> *changes);

}
