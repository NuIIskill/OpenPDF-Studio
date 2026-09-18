// SPDX-License-Identifier: LicenseRef-OpenPDF-Business
#pragma once

#include "rich-media/engine/MediaAsset.hpp"
#include "rich-media/engine/MediaSpec.hpp"

#include <QList>

/// Media inserted or dropped but not yet saved.
class MediaSession
{
public:

    int  addInsert(const MediaSpec &spec);
    void updateInsert(int index, const MediaSpec &spec);
    void removeInsert(int index);

    bool dropInsert(const MediaSpec &spec);
    const QList<MediaSpec> &inserts() const { return m_inserts; }

    void addRemoval(const MediaAsset &asset);
    const QList<MediaAsset> &removals() const { return m_removals; }

    bool isEmpty() const { return m_inserts.isEmpty() && m_removals.isEmpty(); }
    void clear();

private:
    QList<MediaSpec>  m_inserts;
    QList<MediaAsset> m_removals;
};
