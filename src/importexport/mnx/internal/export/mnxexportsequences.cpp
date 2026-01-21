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

} // namespace

void MnxExporter::createSequences(const Part* part, const Measure* measure, mnx::part::Measure& mnxMeasure)
{
    const size_t staves = part->nstaves();
    auto mnxSequences = mnxMeasure.sequences();

    for (size_t staffIdx = 0; staffIdx < staves; ++staffIdx) {
        for (voice_idx_t voice = 0; voice < VOICES; ++voice) {
            const track_idx_t track = part->startTrack() + VOICES * staffIdx + voice;
            std::vector<const ChordRest*> chordRests;

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

            ExportContext ctx {
                part,
                measure,
                static_cast<staff_idx_t>(staffIdx),
                voice
            };
            appendContent(mnxSequence.content(), ctx, chordRests, ContentContext::Sequence);
        }
    }
}

void MnxExporter::appendContent(mnx::ContentArray content, const ExportContext& ctx,
                               const std::vector<const ChordRest*>& chordRests,
                               ContentContext context)
{
    for (size_t idx = 0; idx < chordRests.size(); ) {
        const ChordRest* chordRest = chordRests[idx];
        IF_ASSERT_FAILED(chordRest) {
            LOGW() << "Skipping null ChordRest while exporting MNX content.";
            ++idx;
            continue;
        }

        const bool inGrace = context == ContentContext::Grace;
        const bool isGrace = chordRest->isGrace();
        IF_ASSERT_FAILED((isGrace && inGrace) || (!isGrace && !inGrace)) {
            LOGW() << "Skipping grace note content with unexpected grace context.";
            ++idx;
            continue;
        }

        if (const Tuplet* tuplet = chordRest->tuplet()) {
            if (!tuplet->elements().empty() && tuplet->elements().front() == chordRest) {
                const size_t nextIdx = appendTuplet(content, ctx, chordRests, idx, chordRest);
                if (nextIdx != idx) {
                    idx = nextIdx;
                    continue;
                }
            }
        }

        if (chordRest->isChord() && toChord(chordRest)->tremoloTwoChord()) {
            const Chord* chord = toChord(chordRest);
            const TremoloTwoChord* tremolo = chord->tremoloTwoChord();
            if (tremolo && tremolo->chord1() == chordRest) {
                const size_t nextIdx = appendTremolo(content, ctx, chordRests, idx, chordRest);
                if (nextIdx != idx) {
                    idx = nextIdx;
                    continue;
                }
            }
        }

        const GraceNotesGroup* graceAfter = nullptr;
        if (chordRest->isChord()) {
            const Chord* chord = toChord(chordRest);
            const GraceNotesGroup& graceBefore = chord->graceNotesBefore();
            const bool isTupletFirst = context == ContentContext::Tuplet && chordRest->tuplet()
                                       && chordRest->tuplet()->elements().front() == chordRest;
            if (context == ContentContext::Tremolo) {
                IF_ASSERT_FAILED(graceBefore.empty() && chord->graceNotesAfter().empty()) {
                    LOGW() << "Skipping grace notes inside a tremolo content container.";
                }
            } else if (!isTupletFirst) {
                appendGrace(content, ctx, graceBefore);
            }

            if (context != ContentContext::Tremolo) {
                graceAfter = &chord->graceNotesAfter();
            }
        }

        const bool eventAppended = appendEvent(content, chordRest);

        bool appendGraceAfter = eventAppended;
        if (context == ContentContext::Tuplet) {
            const Tuplet* tuplet = chordRest->tuplet();
            appendGraceAfter = eventAppended
                               && (!tuplet || tuplet->elements().empty()
                                   || tuplet->elements().back() != chordRest);
        }

        if (appendGraceAfter && graceAfter && !graceAfter->empty()) {
            appendGrace(content, ctx, *graceAfter);
        }

        if (chordRest->isChord()) {
            const Chord* chord = toChord(chordRest);
            if (chord->tremoloSingleChord()) {
                /// @todo Export single-note tremolos.
            }
        }

        /// @todo Export event markings, lyrics, and other annotations.
        ++idx;
    }
}

void MnxExporter::appendGrace(mnx::ContentArray content, const ExportContext& ctx,
                             const GraceNotesGroup& graceNotes)
{
    if (graceNotes.empty()) {
        return;
    }

    auto mnxGrace = content.append<mnx::sequence::Grace>();
    /// @todo Export grace note slash style (mnx::sequence::Grace::slash).
    /// @todo Export grace note playback type (mnx::sequence::Grace::graceType).
    std::vector<const ChordRest*> graceChordRests;
    graceChordRests.reserve(graceNotes.size());
    for (const Chord* graceChord : graceNotes) {
        graceChordRests.push_back(graceChord);
    }
    appendContent(mnxGrace.content(), ctx, graceChordRests, ContentContext::Grace);
}

size_t MnxExporter::appendTuplet(mnx::ContentArray content, const ExportContext& ctx,
                                const std::vector<const ChordRest*>& chordRests, size_t idx,
                                const ChordRest* chordRest)
{
    const Tuplet* tuplet = chordRest->tuplet();
    IF_ASSERT_FAILED(tuplet) {
        return idx;
    }

    if (tuplet->elements().empty() || tuplet->elements().front() != chordRest) {
        return idx;
    }

    std::vector<const ChordRest*> tupletChordRests;
    for (size_t scan = idx; scan < chordRests.size(); ++scan) {
        const ChordRest* scanRest = chordRests[scan];
        if (tuplet->contains(scanRest)) {
            tupletChordRests.push_back(scanRest);
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
        appendGrace(content, ctx, chord->graceNotesBefore());
    }

    auto inner = mnx::NoteValueQuantity::make(static_cast<unsigned>(ratio.numerator()),
                                              *baseNoteValue);
    auto outer = mnx::NoteValueQuantity::make(static_cast<unsigned>(ratio.denominator()),
                                              *baseNoteValue);
    auto mnxTuplet = content.append<mnx::sequence::Tuplet>(inner, outer);
    /// @todo Export tuplet display settings (mnx::sequence::Tuplet).

    appendContent(mnxTuplet.content(), ctx, tupletChordRests, ContentContext::Tuplet);
    const ChordRest* lastTupletRest = tupletChordRests.back();
    if (lastTupletRest && lastTupletRest->isChord()) {
        const GraceNotesGroup& graceAfter = toChord(lastTupletRest)->graceNotesAfter();
        if (!graceAfter.empty()) {
            appendGrace(content, ctx, graceAfter);
        }
    }
    return idx + tupletChordRests.size();
}

size_t MnxExporter::appendTremolo(mnx::ContentArray content, const ExportContext& ctx,
                                 const std::vector<const ChordRest*>& chordRests, size_t idx,
                                 const ChordRest* chordRest)
{
    const Chord* chord = chordRest->isChord() ? toChord(chordRest) : nullptr;
    const TremoloTwoChord* tremolo = chord ? chord->tremoloTwoChord() : nullptr;
    IF_ASSERT_FAILED(tremolo) {
        return idx;
    }

    if (tremolo->chord1() != chordRest) {
        return idx;
    }

    const Chord* chord2 = tremolo->chord2();
    if (!chord2) {
        LOGW() << "Skipping tremolo with missing second chord.";
        return idx;
    }

    IF_ASSERT_FAILED(chord->graceNotesAfter().empty() && chord2->graceNotesBefore().empty()) {
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
    if (!tremoloDuration.isValid()) {
        tremoloDuration = chordRest->durationType();
    }
    const auto tremoloNoteValue = toMnxNoteValue(tremoloDuration);
    if (!tremoloNoteValue) {
        LOGW() << "Skipping tremolo with unsupported MNX duration type.";
        return idx;
    }

    auto outer = mnx::NoteValueQuantity::make(2, *tremoloNoteValue);
    auto mnxTremolo = content.append<mnx::sequence::MultiNoteTremolo>(marks, outer);
    /// @todo Export tremolo individual duration (mnx::sequence::MultiNoteTremolo::individualDuration).

    std::vector<const ChordRest*> tremoloChordRests { chordRest, chord2 };
    appendContent(mnxTremolo.content(), ctx, tremoloChordRests, ContentContext::Tremolo);
    return idx + 2;
}

bool MnxExporter::appendEvent(mnx::ContentArray content, const ChordRest* chordRest)
{
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
    mnxEvent.set_id(chordRest->eid().toStdString());

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
        for (const Note* note : chordNotes) {
            const auto pitch = toMnxPitch(note);
            if (!pitch) {
                LOGW() << "Skipping note with unsupported pitch.";
                continue;
            }
            auto mnxNote = mnxNotes.append(*pitch);
            mnxNote.set_id(note->eid().toStdString());
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
