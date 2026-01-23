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

#include <unordered_set>
#include <vector>

#include "mnxdom.h"

#include "engraving/types/types.h"

namespace mu::engraving {
class ChordRest;
class EID;
class EngravingItem;
class GraceNotesGroup;
class Measure;
class Part;
class Score;
class TremoloTwoChord;
class Tuplet;
} // namespace mu::engraving

namespace mu::iex::mnxio {

class MnxExporter
{
public:
    MnxExporter(engraving::Score* s) : m_score(s) {}
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

    engraving::EID getOrAssignEID(engraving::EngravingItem* item);

    struct ExportContext {
        ExportContext(const engraving::Part* partIn, const engraving::Measure* measureIn,
                      engraving::staff_idx_t staffIdxIn, engraving::voice_idx_t voiceIn)
            : part(partIn),
              measure(measureIn),
              staffIdx(staffIdxIn),
              voice(voiceIn)
        {
        }

        const engraving::Part* part{};
        const engraving::Measure* measure{};
        engraving::staff_idx_t staffIdx{};
        engraving::voice_idx_t voice{};
        std::vector<const engraving::Tuplet*> tupletStack;
        std::unordered_set<const engraving::ChordRest*> graceBeforeEmitted;
        std::unordered_set<const engraving::ChordRest*> graceAfterEmitted;
    };

    void createGlobal();
    void createParts();
    void createSequences(const engraving::Part* part, const engraving::Measure* measure,
                         mnx::part::Measure& mnxMeasure);
    // Walks a list of chord/rest events, routing output to the provided MNX content container.
    void appendContent(mnx::ContentArray content, ExportContext& ctx,
                       const std::vector<engraving::ChordRest*>& chordRests,
                       ContentContext context);
    // Emits a grace container and recurses into its content.
    void appendGrace(mnx::ContentArray content, ExportContext& ctx,
                     engraving::GraceNotesGroup& graceNotes);
    // Starts a tuplet container and recurses into its content; returns last processed index.
    size_t appendTuplet(mnx::ContentArray content, ExportContext& ctx,
                        const std::vector<engraving::ChordRest*>& chordRests, size_t idx,
                        engraving::ChordRest* chordRest, const engraving::Tuplet* tuplet);
    // Starts a tremolo container and recurses into its content; returns last processed index.
    size_t appendTremolo(mnx::ContentArray content, ExportContext& ctx,
                         const std::vector<engraving::ChordRest*>& chordRests, size_t idx,
                         engraving::ChordRest* chordRest);
    // Emits a single MNX event (duration + rest/notes); returns true when appended.
    bool appendEvent(mnx::ContentArray content, engraving::ChordRest* chordRest);
    // Finds the highest tuplet in the chain that is not already on the stack.
    const engraving::Tuplet* findTopTuplet(engraving::ChordRest* chordRest, const ExportContext& ctx) const;

    engraving::Score* m_score{};
    mnx::Document m_mnxDocument;
};

} // namespace mu::iex::mnxio
