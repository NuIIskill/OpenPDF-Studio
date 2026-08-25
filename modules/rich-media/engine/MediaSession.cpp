// SPDX-License-Identifier: LicenseRef-OpenPDF-Business
#include "rich-media/engine/MediaSession.hpp"

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
    // A medium that is not its own object in the document cannot be removed.
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
