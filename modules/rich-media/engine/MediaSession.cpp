// SPDX-License-Identifier: LicenseRef-OpenPDF-Business
#include "rich-media/engine/MediaSession.hpp"

#include <QDataStream>
#include <QIODevice>

namespace {

constexpr quint16 kVersion = 1;

QDataStream &operator<<(QDataStream &out, const MediaSpec &spec)
{
    return out << static_cast<qint32>(spec.type) << spec.source << spec.displayName
               << spec.mimeType << spec.poster << spec.activateOnPageOpen
               << spec.autoPlay << spec.muted << spec.loop << spec.showControls
               << spec.floating << static_cast<qint32>(spec.page) << spec.bounds
               << spec.ownPage << spec.pageSizePt;
}

QDataStream &operator>>(QDataStream &in, MediaSpec &spec)
{
    qint32 type = 0, page = -1;
    in >> type >> spec.source >> spec.displayName >> spec.mimeType >> spec.poster
       >> spec.activateOnPageOpen >> spec.autoPlay >> spec.muted >> spec.loop
       >> spec.showControls >> spec.floating >> page >> spec.bounds
       >> spec.ownPage >> spec.pageSizePt;
    spec.type = static_cast<MediaSpec::Type>(type);
    spec.page = page;
    return in;
}

QDataStream &operator<<(QDataStream &out, const MediaAsset &asset)
{
    return out << static_cast<qint32>(asset.page) << asset.bounds << asset.name
               << asset.mimeType << static_cast<qint32>(asset.kind)
               << asset.activateOnPageOpen << asset.muted << asset.loop
               << asset.showControls << asset.floating
               << static_cast<qint32>(asset.streamObject)
               << static_cast<qint32>(asset.streamGeneration)
               << static_cast<qint32>(asset.annotObject)
               << static_cast<qint32>(asset.annotGeneration) << asset.size;
}

QDataStream &operator>>(QDataStream &in, MediaAsset &asset)
{
    qint32 page = -1, kind = 0, stream = 0, streamGen = 0, annot = 0, annotGen = 0;
    in >> page >> asset.bounds >> asset.name >> asset.mimeType >> kind
       >> asset.activateOnPageOpen >> asset.muted >> asset.loop
       >> asset.showControls >> asset.floating >> stream >> streamGen
       >> annot >> annotGen >> asset.size;
    asset.page             = page;
    asset.kind             = static_cast<MediaAsset::Kind>(kind);
    asset.streamObject     = stream;
    asset.streamGeneration = streamGen;
    asset.annotObject      = annot;
    asset.annotGeneration  = annotGen;
    return in;
}

}

int MediaSession::addInsert(const MediaSpec &spec)
{
    m_inserts.append(spec);
    return m_inserts.size() - 1;
}

void MediaSession::updateInsert(int index, const MediaSpec &spec)
{
    if (index >= 0 && index < m_inserts.size())
        m_inserts[index] = spec;
}

void MediaSession::removeInsert(int index)
{
    if (index >= 0 && index < m_inserts.size())
        m_inserts.removeAt(index);
}

bool MediaSession::dropInsert(const MediaSpec &spec)
{
    for (int i = 0; i < m_inserts.size(); ++i) {
        const MediaSpec &candidate = m_inserts.at(i);
        if (candidate.page == spec.page && candidate.source == spec.source
            && candidate.bounds == spec.bounds) {
            m_inserts.removeAt(i);
            return true;
        }
    }
    return false;
}

void MediaSession::addRemoval(const MediaAsset &asset)
{

    if (asset.annotObject <= 0) return;
    for (const MediaAsset &existing : m_removals)
        if (existing.annotObject == asset.annotObject) return;
    m_removals.append(asset);
}

void MediaSession::clear()
{
    m_inserts.clear();
    m_removals.clear();
}

QByteArray MediaSession::toBytes() const
{
    QByteArray bytes;
    QDataStream out(&bytes, QIODevice::WriteOnly);
    out.setVersion(QDataStream::Qt_6_0);
    out << kVersion << static_cast<qint32>(m_inserts.size());
    for (const MediaSpec &spec : m_inserts) out << spec;
    out << static_cast<qint32>(m_removals.size());
    for (const MediaAsset &asset : m_removals) out << asset;
    return bytes;
}

MediaSession MediaSession::fromBytes(const QByteArray &bytes)
{
    MediaSession session;
    QDataStream in(bytes);
    in.setVersion(QDataStream::Qt_6_0);
    quint16 version = 0;
    in >> version;
    if (version != kVersion) return session;

    QList<MediaSpec>  inserts;
    QList<MediaAsset> removals;
    qint32 count = 0;
    in >> count;
    for (qint32 i = 0; i < count && in.status() == QDataStream::Ok; ++i) {
        MediaSpec spec;
        in >> spec;
        inserts.append(std::move(spec));
    }
    in >> count;
    for (qint32 i = 0; i < count && in.status() == QDataStream::Ok; ++i) {
        MediaAsset asset;
        in >> asset;
        removals.append(std::move(asset));
    }
    if (in.status() != QDataStream::Ok) return session;
    session.m_inserts  = std::move(inserts);
    session.m_removals = std::move(removals);
    return session;
}
