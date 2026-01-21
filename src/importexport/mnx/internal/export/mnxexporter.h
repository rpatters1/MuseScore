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
#pragma once

#include <vector>

#include "mnxdom.h"

#include "engraving/types/types.h"

namespace mu::engraving {
class ChordRest;
class GraceNotesGroup;
class Measure;
class Part;
class Score;
} // namespace mu::engraving

namespace mu::iex::mnxio {

class MnxExporter
{
public:
    MnxExporter(const engraving::Score* s) : m_score(s) {}
    void exportMnx();

    const mnx::Document& mnxDocument() const
    { return m_mnxDocument; }

private:
    enum class ContentContext {
        Sequence,
        Grace,
        Tuplet,
        Tremolo
    };

    struct ExportContext {
        const engraving::Part* part{};
        const engraving::Measure* measure{};
        engraving::staff_idx_t staffIdx{};
        engraving::voice_idx_t voice{};
    };

    void createGlobal();
    void createParts();
    void createSequences(const engraving::Part* part, const engraving::Measure* measure,
                         mnx::part::Measure& mnxMeasure);
    // Walks a list of chord/rest events, routing output to the provided MNX content container.
    void appendContent(mnx::ContentArray content, const ExportContext& ctx,
                       const std::vector<const engraving::ChordRest*>& chordRests,
                       ContentContext context);
    // Emits a grace container and recurses into its content.
    void appendGrace(mnx::ContentArray content, const ExportContext& ctx,
                     const engraving::GraceNotesGroup& graceNotes);
    // Starts a tuplet container and recurses into its content; returns next parent-loop index.
    size_t appendTuplet(mnx::ContentArray content, const ExportContext& ctx,
                        const std::vector<const engraving::ChordRest*>& chordRests, size_t idx,
                        const engraving::ChordRest* chordRest);
    // Starts a tremolo container and recurses into its content; returns next parent-loop index.
    size_t appendTremolo(mnx::ContentArray content, const ExportContext& ctx,
                         const std::vector<const engraving::ChordRest*>& chordRests, size_t idx,
                         const engraving::ChordRest* chordRest);
    // Emits a single MNX event (duration + rest/notes); returns true when appended.
    bool appendEvent(mnx::ContentArray content, const engraving::ChordRest* chordRest);

    const engraving::Score* m_score{};
    mnx::Document m_mnxDocument;
};

} // namespace mu::iex::mnxio
