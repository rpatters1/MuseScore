/*
 * SPDX-License-Identifier: GPL-3.0-only
 * MuseScore-Studio-CLA-applies
 *
 * MuseScore Studio
 * Music Composition & Notation
 *
 * Copyright (C) 2026 MuseScore Limited
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */
#include "mnxexporter.h"

#include <stdexcept>

#include "engraving/dom/engravingitem.h"

using namespace mu::engraving;

namespace mu::iex::mnxio {

//---------------------------------------------------------
//   getOrAssignEID
//---------------------------------------------------------

EID MnxExporter::getOrAssignEID(EngravingObject* item)
{
    EID eid = item->eid();
    if (!eid.isValid()) {
        eid = item->assignNewEID();
    }
    return eid;
}

//---------------------------------------------------------
//   mnxEventFromCR
//---------------------------------------------------------

std::optional<mnx::sequence::Event> MnxExporter::mnxEventFromCR(const engraving::ChordRest* cr)
{
    auto pointer = muse::value(m_crToMnxEvent, cr);
    if (!pointer.empty()) {
        return mnx::sequence::Event(mnxDocument().root(), pointer);
    }
    return std::nullopt;
}

//---------------------------------------------------------
//   mnxNoteFromNote
//---------------------------------------------------------

std::optional<mnx::sequence::Note> MnxExporter::mnxNoteFromNote(const engraving::Note* note)
{
    auto pointer = muse::value(m_noteToMnxNote, note);
    if (!pointer.empty()) {
        return mnx::sequence::Note(mnxDocument().root(), pointer);
    }
    return std::nullopt;
}

//---------------------------------------------------------
//   mnxMeasureIndexFromMeasure
//---------------------------------------------------------

size_t MnxExporter::mnxMeasureIndexFromMeasure(const engraving::Measure* measure) const
{
    IF_ASSERT_FAILED(measure) {
        throw std::logic_error("Measure is null while resolving MNX measure index.");
    }
    const auto it = m_measToMnxMeas.find(measure);
    IF_ASSERT_FAILED(it != m_measToMnxMeas.end()) {
        throw std::logic_error("Measure is not mapped to an MNX measure index.");
    }
    return it->second;
}

//---------------------------------------------------------
//   exportMnx
//---------------------------------------------------------

void MnxExporter::exportMnx()
{
    // Header
    mnx::MnxMetaData::Support support = m_mnxDocument.mnx().ensure_support();
    support.set_useBeams(true);

    createGlobal();
    createParts();
}

} // namespace mu::iex::mnxio
