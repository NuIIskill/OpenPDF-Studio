// SPDX-License-Identifier: LicenseRef-OpenPDF-Business
#pragma once

#include "rich-media/engine/MediaAsset.hpp"
#include "rich-media/engine/MediaSpec.hpp"

#include <QList>

/// Media inserted or dropped but not yet saved. Same shape as EditSession in
/// the Core: the list of what the user changed, nothing more. RichMediaWriter
/// knows how it reaches the document.
class MediaSession
{
public:
    /// Returns the index of the new entry.
    int  addInsert(const MediaSpec &spec);
    void updateInsert(int index, const MediaSpec &spec);
    void removeInsert(int index);
    /// Withdraws the insert matching `spec` by page, area and source. By
    /// content and not by index, because indices shift.
    bool dropInsert(const MediaSpec &spec);
    const QList<MediaSpec> &inserts() const { return m_inserts; }

    /// Mark a medium already in the document for removal.
    void addRemoval(const MediaAsset &asset);
    const QList<MediaAsset> &removals() const { return m_removals; }

    bool isEmpty() const { return m_inserts.isEmpty() && m_removals.isEmpty(); }
    void clear();

private:
    QList<MediaSpec>  m_inserts;
    QList<MediaAsset> m_removals;
};
