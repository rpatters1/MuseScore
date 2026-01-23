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

#include <algorithm>
#include <utility>
#include <vector>

#include "engraving/dom/engravingitem.h"
#include "engraving/dom/chord.h"
#include "engraving/dom/chordrest.h"
#include "engraving/dom/measure.h"
#include "engraving/dom/instrument.h"
#include "engraving/dom/note.h"
#include "engraving/dom/part.h"
#include "engraving/dom/pitchspelling.h"
#include "engraving/dom/rest.h"
#include "engraving/dom/mscore.h"
#include "engraving/dom/segment.h"
#include "engraving/dom/staff.h"
#include "engraving/dom/tremolotwochord.h"
#include "engraving/dom/tuplet.h"
#include "internal/shared/mnxtypesconv.h"
#include "log.h"

using namespace mu::engraving;

namespace mu::iex::mnxio {
namespace {

std::optional<mnx::sequence::Pitch::Required> toMnxPitch(const Note* note)
{
    if (!note) {
        return std::nullopt;
    }

    const Staff* staff = note->staff();
    const Instrument* instrument = staff ? staff->part()->instrument(note->tick()) : nullptr;
    int pitch = note->pitch();
    if (instrument && !note->concertPitch()) {
        pitch -= instrument->transpose().chromatic;
    }

    const int tpc = note->tpc1();
    const int step = tpc2step(tpc);
    const int alter = static_cast<int>(tpc2alter(tpc));
    const int octave = playingOctave(pitch, tpc);

    return mnx::sequence::Pitch::make(static_cast<mnx::NoteStep>(step), octave, alter);
}

bool tupletContainsChordRest(const Tuplet* tuplet, const ChordRest* chordRest)
{
    IF_ASSERT_FAILED(tuplet && chordRest) {
        return false;
    }

    for (const DurationElement* element : tuplet->elements()) {
        if (!element) {
            continue;
        }
        if (element->isChordRest()) {
            if (toChordRest(element) == chordRest) {
                return true;
            }
            continue;
        }
        if (element->isTuplet()) {
            if (tupletContainsChordRest(toTuplet(element), chordRest)) {
                return true;
            }
        }
    }
    return false;
}

ChordRest* firstTupletChordRest(const Tuplet* tuplet)
{
    IF_ASSERT_FAILED(tuplet) {
        return nullptr;
    }

    for (DurationElement* element : tuplet->elements()) {
        if (!element) {
            continue;
        }
        if (element->isChordRest()) {
            return toChordRest(element);
        }
        if (element->isTuplet()) {
            if (ChordRest* nested = firstTupletChordRest(toTuplet(element))) {
                return nested;
            }
        }
    }
    return nullptr;
}

} // namespace

void MnxExporter::createSequences(const Part* part, const Measure* measure, mnx::part::Measure& mnxMeasure)
{
    const size_t staves = part->nstaves();
    auto mnxSequences = mnxMeasure.sequences();

    for (size_t staffIdx = 0; staffIdx < staves; ++staffIdx) {
        for (voice_idx_t voice = 0; voice < VOICES; ++voice) {
            const track_idx_t track = part->startTrack() + VOICES * staffIdx + voice;
            std::vector<ChordRest*> chordRests;

            for (Segment* segment = measure->first(); segment; segment = segment->next()) {
                if (segment->segmentType() != SegmentType::ChordRest) {
                    continue;
                }

                EngravingItem* item = segment->element(track);
                if (!item || !item->isChordRest()) {
                    continue;
                }

                chordRests.push_back(toChordRest(item));
            }

            if (chordRests.empty()) {
                continue;
            }

            auto mnxSequence = mnxSequences.append();
            if (staves > 1) {
                mnxSequence.set_staff(static_cast<int>(staffIdx + 1));
            }
            /// @todo Export sequence voice labels (mnx::Sequence::voice).

            ExportContext ctx(part, measure, static_cast<staff_idx_t>(staffIdx), voice);
            appendContent(mnxSequence.content(), ctx, chordRests, ContentContext::Sequence);
        }
    }
}

void MnxExporter::appendContent(mnx::ContentArray content, ExportContext& ctx,
                               const std::vector<ChordRest*>& chordRests,
                               ContentContext context)
{
    for (size_t idx = 0; idx < chordRests.size(); ++idx) {
        ChordRest* chordRest = chordRests[idx];
        IF_ASSERT_FAILED(chordRest) {
            LOGW() << "Skipping null ChordRest while exporting MNX content.";
            continue;
        }

        const bool inGrace = context == ContentContext::Grace;
        const bool isGrace = chordRest->isGrace();
        IF_ASSERT_FAILED((isGrace && inGrace) || (!isGrace && !inGrace)) {
            LOGW() << "Skipping grace note content with unexpected grace context.";
            continue;
        }

        const bool inTremolo = context == ContentContext::Tremolo;

        if (!inGrace) {
            if (chordRest->tuplet()) {
                const Tuplet* topMost = findTopTuplet(chordRest, ctx);
                if (topMost && firstTupletChordRest(topMost) == chordRest) {
                    const size_t lastIdx = appendTuplet(content, ctx, chordRests, idx,
                                                        chordRest, topMost);
                    if (lastIdx >= idx) {
                        idx = lastIdx;
                        continue;
                    }
                    IF_ASSERT_FAILED(lastIdx < idx) {
                        LOGW() << "Invalid index returned by appendTuplet.";
                    }
                }
            }
            if (chordRest->isChord() && toChord(chordRest)->tremoloTwoChord()) {
                if (!inTremolo) {
                    const Chord* chord = toChord(chordRest);
                    const TremoloTwoChord* tremolo = chord->tremoloTwoChord();
                    if (tremolo && tremolo->chord1() == chordRest) {
                        const size_t lastIdx = appendTremolo(content, ctx, chordRests, idx, chordRest);
                        if (lastIdx >= idx) {
                            idx = lastIdx;
                            continue;
                        }
                        IF_ASSERT_FAILED(lastIdx < idx) {
                            LOGW() << "Invalid index returned by appendTremolo.";
                        }
                    }
                }
            }
        }

        // Tremolos manage their own grace content. Grace notes cannot have grace notes.
        // Tuplet boundaries are handled via graceBeforeEmitted/graceAfterEmitted in appendTuplet.
        if (!inTremolo && !inGrace && chordRest->isChord()) {
            if (ctx.graceBeforeEmitted.insert(chordRest).second) {
                appendGrace(content, ctx, toChord(chordRest)->graceNotesBefore());
            }
        }

        const bool eventAppended = appendEvent(content, chordRest);

        if (eventAppended && !inTremolo && !inGrace && chordRest->isChord()) {
            if (ctx.graceAfterEmitted.insert(chordRest).second) {
                appendGrace(content, ctx, toChord(chordRest)->graceNotesAfter());
            }
        }
    }
}

const Tuplet* MnxExporter::findTopTuplet(ChordRest* chordRest, const ExportContext& ctx) const
{
    const Tuplet* tuplet = chordRest ? chordRest->tuplet() : nullptr;
    const Tuplet* topMost = nullptr;
    for (const Tuplet* cursor = tuplet; cursor; cursor = cursor->tuplet()) {
        const bool activeTuplet = std::find(ctx.tupletStack.begin(),
                                            ctx.tupletStack.end(), cursor)
                                  != ctx.tupletStack.end();
        if (!activeTuplet) {
            topMost = cursor;
        }
    }
    return topMost;
}

void MnxExporter::appendGrace(mnx::ContentArray content, ExportContext& ctx,
                              GraceNotesGroup& graceNotes)
{
    if (graceNotes.empty()) {
        return;
    }

    auto mnxGrace = content.append<mnx::sequence::Grace>();
    mnxGrace.set_slash(graceNotes[0]->showStemSlash());
    /// @todo Export grace note playback type (mnx::sequence::Grace::graceType).

    std::vector<ChordRest*> graceChordRests;
    graceChordRests.reserve(graceNotes.size());
    for (Chord* graceChord : graceNotes) {
        graceChordRests.push_back(graceChord);
    }

    appendContent(mnxGrace.content(), ctx, graceChordRests, ContentContext::Grace);
}

size_t MnxExporter::appendTuplet(mnx::ContentArray content, ExportContext& ctx,
                                const std::vector<ChordRest*>& chordRests, size_t idx,
                                ChordRest* chordRest, const Tuplet* tuplet)
{
    IF_ASSERT_FAILED(tuplet) {
        return idx;
    }

    std::vector<ChordRest*> tupletChordRests;
    size_t lastTupletIdx = idx;
    for (size_t scan = idx; scan < chordRests.size(); ++scan) {
        ChordRest* scanCR = chordRests[scan];
        if (tupletContainsChordRest(tuplet, scanCR)) {
            tupletChordRests.push_back(scanCR);
            lastTupletIdx = scan;
        } else if (!tupletChordRests.empty()) {
            break;
        }
    }

    const auto baseNoteValue = toMnxNoteValue(tuplet->baseLen());
    const Fraction ratio = tuplet->ratio();
    const bool ratioValid = ratio.numerator() > 0 && ratio.denominator() > 0;
    if (tupletChordRests.empty() || !baseNoteValue || !ratioValid) {
        if (!tupletChordRests.empty()) {
            LOGW() << "Skipping tuplet with unsupported MNX base note value or ratio.";
        }
        return idx;
    }

    if (chordRest->isChord()) {
        const Chord* chord = toChord(chordRest);
        if (ctx.graceBeforeEmitted.insert(chordRest).second) {
            appendGrace(content, ctx, chord->graceNotesBefore());
        }
    }

    auto inner = mnx::NoteValueQuantity::make(static_cast<unsigned>(ratio.numerator()),
                                              *baseNoteValue);
    auto outer = mnx::NoteValueQuantity::make(static_cast<unsigned>(ratio.denominator()),
                                              *baseNoteValue);
    auto mnxTuplet = content.append<mnx::sequence::Tuplet>(inner, outer);
    /// @todo Export tuplet display settings (mnx::sequence::Tuplet).

    ctx.tupletStack.push_back(tuplet);
    appendContent(mnxTuplet.content(), ctx, tupletChordRests, ContentContext::Tuplet);
    ctx.tupletStack.pop_back();
    const ChordRest* lastTupletCR = tupletChordRests.back();
    if (lastTupletCR && lastTupletCR->isChord()) {
        if (ctx.graceAfterEmitted.insert(lastTupletCR).second) {
            appendGrace(content, ctx, toChord(lastTupletCR)->graceNotesAfter());
        }
    }
    return lastTupletIdx; // shift index to last idx in tuplet
}

size_t MnxExporter::appendTremolo(mnx::ContentArray content, ExportContext& ctx,
                                 const std::vector<ChordRest*>& chordRests, size_t idx,
                                 ChordRest* chordRest)
{
    Chord* chord = chordRest->isChord() ? toChord(chordRest) : nullptr;
    const TremoloTwoChord* tremolo = chord ? chord->tremoloTwoChord() : nullptr;
    IF_ASSERT_FAILED(chord && tremolo) {
        LOGW() << "Skipping ChordRest that is not part of a tremolo.";
        return idx;
    }

    IF_ASSERT_FAILED(tremolo->chord1() == chordRest) {
        LOGW() << "Skipping tremolo with unexpected first chord.";
        return idx;
    }

    Chord* chord2 = tremolo->chord2();
    IF_ASSERT_FAILED(chord2) {
        LOGW() << "Skipping tremolo with missing second chord.";
        return idx;
    }

    if (!chord->graceNotesAfter().empty() || !chord2->graceNotesBefore().empty()) {
        LOGW() << "Skipping tremolo with grace notes inside tremolo content.";
        return idx;
    }

    if (idx + 1 >= chordRests.size() || chordRests[idx + 1] != chord2) {
        LOGW() << "Skipping tremolo with non-adjacent chord events.";
        return idx;
    }

    const TremoloType tremoloType = tremolo->tremoloType();
    const int marks = static_cast<int>(tremoloType) - static_cast<int>(TremoloType::C8) + 1;
    if (marks <= 0) {
        LOGW() << "Skipping tremolo with unsupported tremolo type.";
        return idx;
    }

    TDuration tremoloDuration = tremolo->durationType();
    if (tremoloDuration.isValid()) {
        tremoloDuration = tremoloDuration.shift(1); // +1 divides the duration by 2.
    }
    if (!tremoloDuration.isValid()) {
        LOGW() << "Skipping 2-note tremolo with invalide duration typew.";
        return idx;
    }
    const auto tremoloNoteValue = toMnxNoteValue(tremoloDuration);
    if (!tremoloNoteValue) {
        LOGW() << "Skipping tremolo with unsupported MNX duration type.";
        return idx;
    }

    auto outer = mnx::NoteValueQuantity::make(2, *tremoloNoteValue);
    auto mnxTremolo = content.append<mnx::sequence::MultiNoteTremolo>(marks, outer);
    /// @todo Perhaps export tremolo individual duration if MNX provides clarity about it.

    std::vector<ChordRest*> tremoloChordRests { chordRest, chord2 };
    if (ctx.graceBeforeEmitted.insert(chordRest).second) {
        appendGrace(content, ctx, chord->graceNotesBefore());
    }
    appendContent(mnxTremolo.content(), ctx, tremoloChordRests, ContentContext::Tremolo);
    if (ctx.graceAfterEmitted.insert(chord2).second) {
        appendGrace(content, ctx, chord2->graceNotesAfter());
    }
    return idx + 1; // shift index to last idx in tremolo
}

bool MnxExporter::appendEvent(mnx::ContentArray content, ChordRest* chordRest)
{
    /// @todo Export event markings, lyrics, and other annotations.
    /// @todo Export single-note tremolos.

    const TDuration duration = chordRest->durationType();
    const bool isMeasure = duration.isMeasure()
                           || (chordRest->isRest() && toRest(chordRest)->isFullMeasureRest());
    const auto noteValue = isMeasure ? std::nullopt : toMnxNoteValue(duration);
    if (!isMeasure && !noteValue) {
        LOGW() << "Skipping event with unsupported MNX duration type: "
               << static_cast<int>(duration.type());
        return false;
    }

    if (chordRest->isRest() && (toRest(chordRest)->isGap() || !chordRest->visible())) {
        /// @todo Revisit doing this for `!visible()` if MNX adds support for explicit rest visibility.
        const Fraction gapTicks = chordRest->ticks();
        const mnx::FractionValue gapDuration(
            static_cast<mnx::FractionValue::NumType>(gapTicks.numerator()),
            static_cast<mnx::FractionValue::NumType>(gapTicks.denominator()));
        content.append<mnx::sequence::Space>(gapDuration);
        return true;
    }

    auto mnxEvent = content.append<mnx::sequence::Event>();
    mnxEvent.set_id(getOrAssignEID(chordRest).toStdString());

    if (isMeasure) {
        mnxEvent.set_measure(true);
    } else {
        mnxEvent.ensure_duration(noteValue->base, noteValue->dots);
    }

    if (chordRest->isRest()) {
        mnxEvent.ensure_rest();
        /// @todo Export rest staff position (mnx::sequence::Rest::staffPosition).
    } else if (chordRest->isChord()) {
        const Chord* chord = toChord(chordRest);
        const std::vector<Note*>& chordNotes = chord->notes();
        if (chordNotes.empty()) {
            LOGW() << "Skipping chord event with no notes.";
            return false;
        }

        auto mnxNotes = mnxEvent.ensure_notes();
        bool hasNote = false;
        for (Note* note : chordNotes) {
            const auto pitch = toMnxPitch(note);
            if (!pitch) {
                LOGW() << "Skipping note with unsupported pitch.";
                continue;
            }
            auto mnxNote = mnxNotes.append(*pitch);
            mnxNote.set_id(getOrAssignEID(note).toStdString());
            /// @todo Export accidentals, ties, articulations, and other note fields.
            hasNote = true;
        }
        if (!hasNote) {
            LOGW() << "Skipping chord event with no convertible notes.";
            return false;
        }
    }

    return true;
}

} // namespace mu::iex::mnxio
