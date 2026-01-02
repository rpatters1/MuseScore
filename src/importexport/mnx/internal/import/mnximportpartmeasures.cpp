/*
 * SPDX-License-Identifier: GPL-3.0-only
 * MuseScore-Studio-CLA-applies
 *
 * MuseScore Studio
 * Music Composition & Notation
 *
 * Copyright (C) 2021 MuseScore Limited
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
#include <stack>

#include "mnximporter.h"
#include "internal/shared/mnxtypesconv.h"

#include "engraving/dom/barline.h"
#include "engraving/dom/bracketItem.h"
#include "engraving/dom/factory.h"
#include "engraving/dom/hook.h"
#include "engraving/dom/instrtemplate.h"
#include "engraving/dom/keysig.h"
#include "engraving/dom/note.h"
#include "engraving/dom/noteval.h"
#include "engraving/dom/part.h"
#include "engraving/dom/rest.h"
#include "engraving/dom/score.h"
#include "engraving/dom/staff.h"
#include "engraving/dom/stem.h"
#include "engraving/dom/tremolotwochord.h"
#include "engraving/dom/tuplet.h"
#include "engraving/dom/volta.h"

#include "mnxdom.h"

using namespace mu::engraving;

namespace mu::iex::mnxio {

Tuplet* MnxImporter::createTuplet(const mnx::sequence::Tuplet& mnxTuplet, Measure* measure, track_idx_t curTrackIdx)
{
    TDuration baseLen = toMuseScoreDuration(mnxTuplet.outer().duration());
    mnx::FractionValue ratioDivisor = mnxTuplet.outer() / mnxTuplet.inner().duration();
    if (!baseLen.isValid() || ratioDivisor.remainder() != 0) {
        LOGE() << "Unable to import tuplet at " << mnxTuplet.pointer().to_string();
        LOGE() << mnxTuplet.dump(2);
        return nullptr;
    }
    Fraction tupletRatio = Fraction(mnxTuplet.inner().multiple(), ratioDivisor.quotient());

    Tuplet* t = Factory::createTuplet(measure);
    t->setTrack(curTrackIdx);
    t->setParent(measure);
    t->setRatio(tupletRatio);
    t->setBaseLen(baseLen);
    Fraction f = baseLen.fraction() * tupletRatio.denominator();
    t->setTicks(f.reduced());
    // options
    t->setNumberType(toMuseScoreTupletNumberType(mnxTuplet.showNumber()));
    if (mnxTuplet.showNumber() != mnx::TupletDisplaySetting::NoNumber) {
        t->setBracketType(toMuseScoreTupletBracketType(mnxTuplet.bracket()));
    }
    return t;
}

void MnxImporter::createTremolo(const mnx::sequence::MultiNoteTremolo& mnxTremolo,
                                Measure* measure, track_idx_t curTrackIdx,
                                const mnx::FractionValue& startTick, const mnx::FractionValue& endTick)
{
    const auto startTick2 = startTick + (endTick - startTick) / 2;
    engraving::Chord* c1 = measure->findChord(measure->tick() + mnxFractionValueToFraction(startTick), curTrackIdx);
    engraving::Chord* c2 = measure->findChord(measure->tick() + mnxFractionValueToFraction(startTick2), curTrackIdx);
    IF_ASSERT_FAILED(c1 && c2 && c1->ticks() == c2->ticks()) {
        LOGE() << "Unable to import tremolo at " << mnxTremolo.pointer().to_string();
        LOGE() << mnxTremolo.dump(2);
        return;
    }
    int tremoloBeamsNum = int(TremoloType::C8) - 1 + mnxTremolo.marks();
    tremoloBeamsNum = std::clamp(tremoloBeamsNum, int(TremoloType::C8), int(TremoloType::C64));
    if (tremoloBeamsNum <= c1->durationType().hooks()) {
        return; // no tremolo is possible
    }

    Fraction d = c1->ticks() / 2;
    c1->setDurationType(d.reduced());
    c1->setTicks(c1->actualDurationType().fraction());
    c2->setDurationType(c1->durationType());
    c2->setTicks(c1->ticks());

    if (c1->durationType().hooks() > 0) {
        // mnx does not include these tremolo events in beams, so do it here.
        c1->setBeamMode(BeamMode::BEGIN);
        c2->setBeamMode(BeamMode::END);
    }

    TremoloTwoChord* tremolo = Factory::createTremoloTwoChord(c1);
    tremolo->setTremoloType(TremoloType(tremoloBeamsNum));
    tremolo->setTrack(curTrackIdx);
    tremolo->setVisible(c1->notes().front()->visible());
    tremolo->setParent(c1);
    tremolo->setChords(c1, c2);
    c1->setTremoloTwoChord(tremolo);
}

// return true if a ChordRest was created.
ChordRest* MnxImporter::importEvent(const mnx::sequence::Event& event,
                              track_idx_t curTrackIdx, Measure* measure, const mnx::FractionValue& startTick,
                              const std::stack<Tuplet*>& activeTuplets, TremoloTwoChord* activeTremolo)
{
    auto d = [&]() -> TDuration {
        if (const auto& duration = event.duration()) {
            return toMuseScoreDuration(duration.value());
        } else if (event.measure() && event.rest()) {
            return TDuration(DurationType::V_MEASURE);
        }
        return {};
    }();
    if (!d.isValid()) {
        LOGW() << "Given ChordRest duration not supported in MuseScore";
        return nullptr;
    }

    Segment* segment = measure->getSegmentR(SegmentType::ChordRest, mnxFractionValueToFraction(startTick));
    mnx::Sequence sequence = event.getSequence();

    ChordRest* cr = nullptr;
    const int eventStaff = event.staff_or(sequence.staff());
    const int crossStaffMove = eventStaff - sequence.staff();
    staff_idx_t staffIdx = track2staff(curTrackIdx);
    Staff* baseStaff = m_score->staff(staffIdx);
    Staff* targetStaff = m_score->staff(static_cast<staff_idx_t>(int(staffIdx) + crossStaffMove));
    IF_ASSERT_FAILED(baseStaff && targetStaff) {
        LOGE() << "Event " << event.pointer().to_string() << " has invalid staff " << eventStaff << ".";
        return nullptr;
    }
\
    if (const auto& mnxRest = event.rest()) {
        Rest* rest = Factory::createRest(segment, d);
        /// @todo rest staff position
        cr = toChordRest(rest);
    } else {
        const auto& notes = event.notes();
        const auto& kitNotes = event.kitNotes();
        if ((notes && !notes->empty())) {/// @todo || (kitNotes && !kitNotes->empty()) {
            engraving::Chord* chord = Factory::createChord(segment);
            for (size_t i = 0; i < event.notes()->size(); i++) {
                engraving::Note* note = Factory::createNote(chord);
                note->setParent(chord);
                note->setTrack(curTrackIdx);
                NoteVal nval = toNoteVal(notes->at(i).pitch(), baseStaff->concertKey(segment->tick()));
                /// @todo transposed pitch
                note->setNval(nval);
                /// @todo force acci
                chord->add(note);
            }
            /// @todo kitNotes
            /*
            // We may need this if we have to override defaults, but for now omit it.
            if (chord->shouldHaveStem() || d.hasStem()) {
                Stem* stem = Factory::createStem(chord);
                chord->add(stem);
            }
            if (m_useBeams && d.hooks() > 0 && !mnxDocument().getIdMapping().tryGetBeam(event)) {
                chord->setBeamMode(BeamMode::NONE);
                Hook* hook = new Hook(chord);
                chord->setHook(hook);
                chord->add(hook);
            }
            */
            cr = toChordRest(chord);
        } else {
            LOGW() << "Event " << event.pointer().to_string() << " is neither rest nor chord.";
            return nullptr;
        }
    }
    cr->setDurationType(d);
    cr->setStaffMove(crossStaffMove);
    cr->setTrack(curTrackIdx);
    if (cr->durationType().isMeasure()) {
        cr->setTicks(measure->stretchedLen(baseStaff)); // baseStaff because that's the staff the cr 'belongs to'
    } else {
        cr->setTicks(cr->actualDurationType().fraction());
    }
    if (!event.isGrace()) {
        segment->add(cr);
        if (!activeTuplets.empty()) {
            activeTuplets.top()->add(cr);
        }
        if (activeTremolo) {
            activeTremolo->add(cr);
        }
    }
    m_mnxEventToCR.emplace(event.pointer().to_string(), cr);
    return cr;
}

// return true if any ChordRest was created
bool MnxImporter::importNonGraceEvents(const mnx::Sequence& sequence, Measure* measure,
                                       track_idx_t curTrackIdx, GraceNeighborsMap& graceNeighbors)
{
    bool insertedCR = false;
    std::stack<Tuplet*> activeTuplets;
    TremoloTwoChord* activeTremolo = nullptr;

    ChordRest* lastCR = nullptr;
    std::vector<std::string> pendingNext;

    mnx::util::SequenceWalkHooks hooks;
    hooks.onItem = [&](const mnx::ContentObject& item, mnx::util::SequenceWalkContext&) {
        if (item.type() == mnx::sequence::Grace::ContentTypeValue) {
            /// @todo refactor this if MuseScore allows grace notes to be normal.
            const auto grace = item.get<mnx::sequence::Grace>();
            const std::string key = grace.pointer().to_string();
            graceNeighbors[key] = { lastCR, nullptr }; // store prev neighbor
            pendingNext.push_back(key);
            return mnx::util::SequenceWalkControl::SkipChildren;
        } else if (item.type() == mnx::sequence::Tuplet::ContentTypeValue) {
            const auto mnxTuplet = item.get<mnx::sequence::Tuplet>();
            if (Tuplet* t = createTuplet(mnxTuplet, measure, curTrackIdx)) {
                if (!activeTuplets.empty()) {
                    activeTuplets.top()->add(t); // reparent tuplet
                }
                activeTuplets.push(t);
            }
        } else if (item.type() == mnx::sequence::MultiNoteTremolo::ContentTypeValue) {
            const auto mnxTremolo = item.get<mnx::sequence::MultiNoteTremolo>();
            auto content = mnxTremolo.content();
            if (content.size() != 2) {
                LOGE() << "Tremolo at " << mnxTremolo.pointer().to_string() << " has " << content.size()
                << " events and cannot be imported.";
                LOGE() << mnxTremolo.dump(2);
                return mnx::util::SequenceWalkControl::SkipChildren;
            }
            using MnxEv = mnx::sequence::Event;
            if (content[0].type() != MnxEv::ContentTypeValue || content[0].type() != MnxEv::ContentTypeValue) {
                LOGE() << "Tremolo at " << mnxTremolo.pointer().to_string() << " contains other content than events";
                LOGE() << mnxTremolo.dump(2);
                return mnx::util::SequenceWalkControl::SkipChildren;
            }
        }
        return mnx::util::SequenceWalkControl::Continue;
    };
    hooks.onEvent = [&](const mnx::sequence::Event& event,
                        const mnx::FractionValue& startTick,
                        const mnx::FractionValue&, [[maybe_unused]]mnx::util::SequenceWalkContext& ctx) {
        IF_ASSERT_FAILED(!ctx.inGrace) {
            LOGE() << "Encountered grace when processing non-grace.";
            return true;
        }
        if (ChordRest* cr = importEvent(event, curTrackIdx, measure, startTick, activeTuplets, activeTremolo)) {
            lastCR = cr;
            insertedCR = true;
            for (const auto& key : pendingNext) {
                auto it = graceNeighbors.find(key);
                if (it != graceNeighbors.end()) {
                    it->second.second = cr; // store next neighbor
                }
            }
            pendingNext.clear();
        }
        return true;
    };
    hooks.onAfterItem = [&](const mnx::ContentObject& item, mnx::util::SequenceWalkContext& ctx) {
        if (item.type() == mnx::sequence::Tuplet::ContentTypeValue) {
            activeTuplets.pop();
        } else if (item.type() == mnx::sequence::MultiNoteTremolo::ContentTypeValue) {
            const auto mnxTremolo = item.get<mnx::sequence::MultiNoteTremolo>();
            const auto startTime = ctx.elapsedTime - (mnxTremolo.outer() * ctx.timeRatio);
            createTremolo(mnxTremolo, measure, curTrackIdx, startTime, ctx.elapsedTime);
        }
    };

    mnx::util::walkSequenceContent(sequence, hooks);

    return insertedCR;
}

void MnxImporter::importGraceEvents(const mnx::Sequence& sequence, Measure* measure,
                                    track_idx_t curTrackIdx, const GraceNeighborsMap& graceNeighbors)
{
    mnx::util::SequenceWalkHooks hooks;
    hooks.onEvent = [&](const mnx::sequence::Event& event,
                        const mnx::FractionValue& startTick,
                        const mnx::FractionValue&, mnx::util::SequenceWalkContext& ctx) {
        if (ctx.inGrace) {
            if (event.rest()) {
                LOGW() << "encountered unsupported grace note rest at " << event.pointer().to_string();
                return true;
            }
            auto grace = event.container<mnx::sequence::Grace>();
            auto [leftNeighbor, rightNeighbor] = muse::value(graceNeighbors, grace.pointer().to_string());
            const bool useRight = rightNeighbor && rightNeighbor->isChord();
            const bool useLeft = !useRight && leftNeighbor && leftNeighbor->isChord();
            if (useRight || useLeft) {
                if (ChordRest* cr = importEvent(event, curTrackIdx, measure, startTick, {}, nullptr)) {
                    engraving::Chord* gc = toChord(cr);
                    TDuration d = gc->durationType();
                    if (useRight && grace.slash() && grace.content().size() == 1) {
                        gc->setNoteType(engraving::NoteType::ACCIACCATURA);
                    } else {
                        gc->setNoteType(durationTypeToNoteType(d.type(), useLeft));
                        gc->setShowStemSlash(grace.slash());
                    }
                    if (useRight) {
                        Chord* graceParent = toChord(rightNeighbor);
                        gc->setGraceIndex(graceParent->graceNotesBefore().size());
                        graceParent->add(gc);
                    } else if (useLeft) {
                        Chord* graceParent = toChord(leftNeighbor);
                        gc->setGraceIndex(0);
                        graceParent->add(gc);
                    }
                }
            }
        }
        return true;
    };

    mnx::util::walkSequenceContent(sequence, hooks);
}

void MnxImporter::importSequences(const mnx::Part& mnxPart, const mnx::part::Measure& partMeasure,
                                  Measure* measure)
{
    std::vector<std::vector<track_idx_t>> staffVoiceMaps(mnxPart.staves());

    // pass1: import non-grace-note events to ChordRest
    for (const auto& sequence : partMeasure.sequences()) {
        if (sequence.staff() > mnxPart.staves()) {
            LOGE() << "Sequence " << sequence.pointer().to_string()
                   << " specifies non-existent staff " << sequence.staff()
                   << " for MNX part at " << mnxPart.pointer().to_string() << ".";
            continue;
       }
        const staff_idx_t curStaffIdx = muse::value(m_mnxPartStaffToStaff,
                                              std::make_pair(mnxPart.calcArrayIndex(), sequence.staff()),
                                              muse::nidx);
        IF_ASSERT_FAILED(curStaffIdx != muse::nidx) {
            LOGE() << "Sequence " << sequence.pointer().to_string()
                   << " specifies unmapped staff " << sequence.staff()
                   << " for MNX part at " << mnxPart.pointer().to_string() << ".";
            return;
        }
        auto& staffVoiceMap = staffVoiceMaps[static_cast<size_t>(sequence.staff() - 1)];
        const track_idx_t voiceId = staffVoiceMap.size();
        if (voiceId >= VOICES) {
            LOGW() << "Part measure " << partMeasure.pointer().to_string()
                   << " contains too many voices for staff " << sequence.staff() << ". This sequence is skipped.";
        }
        const track_idx_t curTrackIdx = staff2track(curStaffIdx, voiceId);
        GraceNeighborsMap graceNeighbors;
        if (importNonGraceEvents(sequence, measure, curTrackIdx, graceNeighbors)) {
            importGraceEvents(sequence, measure, curTrackIdx, graceNeighbors); // if MuseScore refactors graces, maybe we don't need this.
            staffVoiceMap.push_back(voiceId);
        }
    }

    // fill in measures as needed with rests.
    for (int staffNum = 1; staffNum <= mnxPart.staves(); staffNum++) {
        staff_idx_t staffIdx = mnxPartStaffToStaffIdx(mnxPart, staffNum);
        measure->checkMeasure(staffIdx);
    }
}

void MnxImporter::importPartMeasures()
{
    /// pass1: create ChordRests and clefs
    for (const mnx::Part& mnxPart : mnxDocument().parts()) {
        if (const auto partMeasures = mnxPart.measures()) {
            for (const mnx::part::Measure& partMeasure : *partMeasures) {
                Measure* measure = mnxMeasureToMeasure(partMeasure.calcArrayIndex());
                importSequences(mnxPart, partMeasure, measure);
                if (const auto mnxClefs = partMeasure.clefs()) {
                    createClefs(mnxPart, mnxClefs.value(), measure);
                }
            }
        }
    }
    /// @todo pass2: add beams, dynamics, ottavas, ties, and slurs
}

}
