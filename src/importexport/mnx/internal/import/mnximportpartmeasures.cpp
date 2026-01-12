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
#include <stack>

#include "mnximporter.h"
#include "internal/shared/mnxtypesconv.h"

#include "engraving/dom/barline.h"
#include "engraving/dom/bracketItem.h"
#include "engraving/dom/dynamic.h"
#include "engraving/dom/factory.h"
#include "engraving/dom/hook.h"
#include "engraving/dom/instrtemplate.h"
#include "engraving/dom/keysig.h"
#include "engraving/dom/laissezvib.h"
#include "engraving/dom/lyrics.h"
#include "engraving/dom/note.h"
#include "engraving/dom/noteval.h"
#include "engraving/dom/ottava.h"
#include "engraving/dom/part.h"
#include "engraving/dom/rest.h"
#include "engraving/dom/score.h"
#include "engraving/dom/slur.h"
#include "engraving/dom/staff.h"
#include "engraving/dom/stem.h"
#include "engraving/dom/tremolotwochord.h"
#include "engraving/dom/tie.h"
#include "engraving/dom/tuplet.h"
#include "engraving/dom/volta.h"

#include "mnxdom.h"

using namespace mu::engraving;

namespace mu::iex::mnxio {

void MnxImporter::createSlur(const mnx::sequence::Slur& mnxSlur, engraving::ChordRest* startCR)
{
    ChordRest* targetCR = mnxEventIdToCR(mnxSlur.target());
    if (!targetCR) {
        LOGW() << "slur target was event with eventId " << mnxSlur.target() << " that was not mapped.";
        LOGW() << mnxSlur.dump(2);
        return;
    }
    Slur* slur = toSlur(Factory::createItem(ElementType::SLUR, m_score->dummy()));
    slur->setScore(m_score);
    slur->setAnchor(Spanner::Anchor::CHORD);
    slur->setTrack(startCR->track());
    slur->setTrack2(targetCR->track());
    slur->setStartElement(startCR);
    slur->setEndElement(targetCR);
    slur->setTick(startCR->tick());
    slur->setTick2(targetCR->tick());
    slur->setAutoplace(true);
    m_score->addElement(slur);

    if (const auto lineType = mnxSlur.lineType()) {
        setAndStyleProperty(slur, Pid::SLUR_STYLE_TYPE, toMuseScoreSlurStyleType(lineType.value()));
    }
    if (const auto side = mnxSlur.side()) {
        DirectionV slurDir = side.value() == mnx::SlurTieSide::Up ? DirectionV::UP : DirectionV::DOWN;
        setAndStyleProperty(slur, Pid::SLUR_DIRECTION, slurDir);
    } else if (const auto sideEnd = mnxSlur.sideEnd()) {
        DirectionV slurDir = sideEnd.value() == mnx::SlurTieSide::Up ? DirectionV::UP : DirectionV::DOWN;
        setAndStyleProperty(slur, Pid::SLUR_DIRECTION, slurDir);
    }
    /// @todo implement side and sideEnd in opposite directions, if/when MuseScore supports it.
    /// @todo endNote and startNote are not supported by MuseScore (yet?)
}

void MnxImporter::createLyrics(const mnx::sequence::Event& mnxEvent, engraving::ChordRest* cr)
{
    /// @todo import lyric line metadata (i.e., language code) somehow?
    if (const auto lyrics = mnxEvent.lyrics()) {
        if (const auto lines = lyrics->lines()) {
            const auto& mnxLineOrder = mnxDocument().getEntityMap().getLyricLineOrder();
            for (size_t verse = 0; verse < mnxLineOrder.size(); verse++) {
                const auto it = lines->find(mnxLineOrder[verse]);
                if (it == lines->end() || it->second.text().empty()) {
                    continue;
                }
                Lyrics* lyric = Factory::createLyrics(cr);
                lyric->setTrack(cr->track());
                lyric->setParent(cr);
                lyric->setVerse(static_cast<int>(verse));
                lyric->setXmlText(String::fromStdString(it->second.text()));
                lyric->setSyllabic(toMuseScoreLyricsSyllabic(it->second.type()));
                /// @todo word extension span, if mnx ever provides it
                cr->add(lyric);
            }
        }
    }
}

void MnxImporter::createTie(const mnx::sequence::Tie& mnxTie, engraving::Note* startNote)
{
    const auto target = mnxTie.target();
    Note* targetNote = target ? mnxNoteIdToNote(target.value()) : nullptr;
    if (!targetNote) {
        if (target) {
            LOGW() << "tie target was note with noteId " << target.value() << " that was not mapped.";
            LOGW() << mnxTie.dump(2);
        }
        return;
    }

    const bool isLv = mnxTie.lv() || !mnxTie.target();
    Tie* tie = isLv ? Factory::createLaissezVib(startNote) : Factory::createTie(startNote);
    tie->setStartNote(startNote);
    tie->setTick(startNote->tick());
    tie->setTrack(startNote->track());
    tie->setParent(startNote);
    startNote->setTieFor(tie);
    DirectionV tieDir = DirectionV::AUTO;
    if (const auto side = mnxTie.side()) {
        tieDir = side.value() == mnx::SlurTieSide::Up ? DirectionV::UP : DirectionV::DOWN;
    }
    setAndStyleProperty(tie, Pid::SLUR_DIRECTION, tieDir);
    if (!isLv) {
        tie->setEndNote(targetNote);
        tie->setTick2(targetNote->tick());
        tie->setTrack2(targetNote->track());
        targetNote->setTieBack(tie);
    }
}

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
    engraving::Chord* c1 = measure->findChord(measure->tick() + toMuseScoreFraction(startTick), curTrackIdx);
    engraving::Chord* c2 = measure->findChord(measure->tick() + toMuseScoreFraction(startTick2), curTrackIdx);
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

    const engraving::Fraction eventTick = toMuseScoreFraction(startTick);
    Segment* segment = measure->getSegmentR(SegmentType::ChordRest, eventTick);
    mnx::Sequence sequence = event.getSequence();

    ChordRest* cr = nullptr;
    const int eventStaff = event.staff_or(sequence.staff());
    int crossStaffMove = eventStaff - sequence.staff();
    staff_idx_t staffIdx = track2staff(curTrackIdx);
    staff_idx_t targetStaffidx = static_cast<staff_idx_t>(int(staffIdx) + crossStaffMove);
    Staff* baseStaff = m_score->staff(staffIdx);
    Staff* targetStaff = m_score->staff(targetStaffidx);
    if (!(targetStaff && targetStaff->visible() && targetStaff->isLinked() == baseStaff->isLinked()
          && staff2track(staffIdx) >= baseStaff->part()->startTrack()
          && staff2track(targetStaffidx) < baseStaff->part()->endTrack()
          && targetStaff->staffType(eventTick)->group() == baseStaff->staffType(eventTick)->group())) {
        crossStaffMove = 0;
        targetStaff = baseStaff;
        targetStaffidx = staffIdx;
    }
    IF_ASSERT_FAILED(baseStaff && targetStaff) {
        LOGE() << "Event " << event.pointer().to_string() << " has invalid staff " << eventStaff << ".";
        return nullptr;
    }

    if (const auto& mnxRest = event.rest()) {
        Rest* rest = Factory::createRest(segment, d);
        /// @todo rest staff position
        cr = toChordRest(rest);
    } else {
        const auto& notes = event.notes();
        const auto& kitNotes = event.kitNotes();
        const int ottavaDisplacement = mnxDocument().getEntityMap().getOttavaShift(event);
        if ((notes && !notes->empty())) {/// @todo || (kitNotes && !kitNotes->empty()) {
            engraving::Chord* chord = Factory::createChord(segment);
            for (size_t i = 0; i < event.notes()->size(); i++) {
                engraving::Note* note = Factory::createNote(chord);
                note->setParent(chord);
                note->setTrack(curTrackIdx);
                auto pitch = notes->at(i).pitch();
                NoteVal nval = toNoteVal(pitch, baseStaff->concertKey(segment->tick()), ottavaDisplacement);
                NoteVal nvalTransposed = toNoteVal(pitch.calcTransposed(), baseStaff->key(segment->tick()), ottavaDisplacement);
                nval.tpc2 = nvalTransposed.tpc2;
                note->setNval(nval);
                /// @todo force acci
                chord->add(note);
                m_mnxNoteToNote.emplace(event.notes()->at(i).pointer().to_string(), note);
            }
            /// @todo kitNotes
            if (const auto stemDir = event.stemDirection()) {
                chord->setStemDirection(stemDir.value() == mnx::StemDirection::Up ? DirectionV::UP : DirectionV::DOWN);
            }
            if (m_useBeams && d.hooks() > 0 && !mnxDocument().getEntityMap().tryGetBeam(event)) {
                chord->setBeamMode(BeamMode::NONE);
            }
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
    importMarkings(event, cr);
    if (!event.isGrace()) {
        createLyrics(event, cr); /// @todo remove from isGrace conditional if possible.
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
                        gc->setNoteType(duraTypeToGraceNoteType(d.type(), useLeft));
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

void MnxImporter::createDynamics(const mnx::part::Measure& mnxMeasure, engraving::Measure* measure)
{
    const auto part = mnxMeasure.getEnclosingElement<mnx::Part>();
    if (const auto mnxDynamics = mnxMeasure.dynamics()) {
        for (const auto& mnxDynamic : mnxDynamics.value()) {
            /// @todo Process all dynamics, including those without glyphs, once the meaning of value()
            /// has been clarified and once mnx has text formatting, which seems to be imminent.
            if (!mnxDynamic.glyph()) {
                continue;
            }
            /// @todo Honor mnx requirement that dynamics apply to all staves when staff() member
            /// is missing (after clarification).
            staff_idx_t staffIdx = muse::value(m_mnxPartStaffToStaff,
                                               std::make_pair(part->calcArrayIndex(), mnxDynamic.staff_or(1)),
                                               muse::nidx);
            IF_ASSERT_FAILED(staffIdx != muse::nidx) {
                LOGE() << "staff idx not found for part " << part->pointer().to_string();
                continue;
            }
            track_idx_t curTrackIdx = staff2track(staffIdx);

            Fraction rTick = toMuseScoreFraction(mnxDynamic.position().fraction());
            Segment* s = measure->getChordRestOrTimeTickSegment(measure->tick() + rTick);
            Dynamic* dyn = Factory::createDynamic(s);
            dyn->setParent(s);
            dyn->setTrack(curTrackIdx);
            /// @todo: smarter approach to creating xmlText.
            String xmlText = u"<sym>" + String::fromStdString(mnxDynamic.glyph().value()) + u"</sym>";
            dyn->setXmlText(xmlText);
            dyn->setDynamicType(toMuseScoreDynamicType(xmlText));
            /// @todo: voice assignment based on voice()
            dyn->setVoiceAssignment(mnxDynamic.staff()
                                    ? VoiceAssignment::ALL_VOICE_IN_STAFF
                                    : VoiceAssignment::ALL_VOICE_IN_INSTRUMENT);

            s->add(dyn);
        }
    }
}

void MnxImporter::createOttavas(const mnx::part::Measure& mnxMeasure, engraving::Measure* measure)
{
    const auto part = mnxMeasure.getEnclosingElement<mnx::Part>();
    if (const auto mnxOttavas = mnxMeasure.ottavas()) {
        for (const auto& mnxOttava : mnxOttavas.value()) {
            staff_idx_t staffIdx = muse::value(m_mnxPartStaffToStaff,
                                               std::make_pair(part->calcArrayIndex(), mnxOttava.staff()),
                                               muse::nidx);
            IF_ASSERT_FAILED(staffIdx != muse::nidx) {
                LOGE() << "staff idx not found for part " << part->pointer().to_string();
                continue;
            }
            const auto mnxEndMeasure = mnxDocument().getEntityMap().get<mnx::global::Measure>(mnxOttava.end().measure());
            Measure* endMeasure = mnxMeasureToMeasure(mnxEndMeasure.calcArrayIndex());
            const Fraction endPos = toMuseScoreFraction(mnxOttava.end().position().fraction());
            const Fraction endTick = endMeasure->tick() + endPos;
            bool endsOnBarline = false;
            if (!endsOnBarline) {
                if (Measure* endPlus1 = endMeasure->nextMeasure()) {
                    endsOnBarline = endPlus1->tick() == endTick;
                }
            }
            /// @todo map ottava.voice() to a relative track other than 0, if MuseScore ever allows it.
            track_idx_t curTrackIdx = staff2track(staffIdx);

            Ottava* ottava = toOttava(Factory::createItem(ElementType::OTTAVA, m_score->dummy()));
            ottava->setScore(m_score);
            ottava->setAnchor(Spanner::Anchor::SEGMENT);
            ottava->setTrack(curTrackIdx);
            ottava->setTrack2(curTrackIdx);
            ottava->setTick(measure->tick() + toMuseScoreRTick(mnxOttava.position()));
            ottava->setTick2(endTick);
            ottava->setAutoplace(true);
            const OttavaType ottavaType = toMuseScoreOttavaType(mnxOttava.value());
            setAndStyleProperty(ottava, Pid::OTTAVA_TYPE, int(ottavaType));
            if (!endsOnBarline) {
                // ottavas in MNX include any event that starts on the endTick
                ChordRest* endCr = nullptr;
                for (track_idx_t voiceIdx = 0; voiceIdx < VOICES; voiceIdx++) {
                    ChordRest* cr = m_score->findCR(endTick, curTrackIdx + voiceIdx);
                    if (!cr) continue;
                    if (!endCr) {
                        endCr = cr;
                    } else if (endCr->endTick() > cr->endTick()) {
                        endCr = cr;
                    }
                }
                if (endCr){
                    ottava->setEndElement(endCr);
                    ottava->setTick2(endCr->endTick());
                }
            }
            m_score->addElement(ottava);
        }
    }
}

void MnxImporter::createBeams(const mnx::part::Measure& mnxMeasure)
{
    if (const auto beams = mnxMeasure.beams()) {
        for (const auto& beam : beams.value()) {
            const auto events = beam.events();
            for (size_t x = 0; x < events.size(); x++) {
                const auto& eventId = events[x];
                ChordRest* cr = mnxEventIdToCR(eventId);
                IF_ASSERT_FAILED(cr) {
                    LOGE() << "encountered unmapped event " << eventId << " in beam " << beam.pointer().to_string();
                    LOGE() << beam.dump(2);
                    continue;
                }
                if (events.size() == 1) {
                    cr->setBeamMode(BeamMode::NONE); // MuseScore does not have singleton beams
                } else if (x == 0) {
                    cr->setBeamMode(BeamMode::BEGIN);
                } else if (x == events.size() - 1) {
                    cr->setBeamMode(BeamMode::END);
                } else {
                    const auto mode = toMuseScoreBeamMode(mnxDocument().getEntityMap().getBeamStartLevel(eventId));
                    cr->setBeamMode(mode);
                }
            }
        }
    }
}

void MnxImporter::processSequencePass2(const mnx::Sequence& sequence)
{
    mnx::util::SequenceWalkHooks hooks;
    hooks.onEvent = [&](const mnx::sequence::Event& event,
                        const mnx::FractionValue&,
                        const mnx::FractionValue&, mnx::util::SequenceWalkContext&) {
        ChordRest* cr = muse::value(m_mnxEventToCR, event.pointer().to_string());
        IF_ASSERT_FAILED(cr) {
            LOGE() << "event is not mapped.";
            LOGE() << event.dump(2);
            return true;
        }
        if (const auto slurs = event.slurs()) {
            for (const auto& slur : slurs.value()) {
                createSlur(slur, cr);
            }
        }
        if (const auto notes = event.notes()) {
            for (const auto& note : notes.value()) {
                Note* startNote = muse::value(m_mnxNoteToNote, note.pointer().to_string());
                IF_ASSERT_FAILED(startNote) {
                    LOGE() << "note has ties but is not mapped.";
                    LOGE() << note.dump(2);
                    continue;
                }
                if (const auto ties = note.ties()) {
                    for (const auto& tie : ties.value()) {
                        createTie(tie, startNote);
                        break; /// @todo support more than one tie if MNX provides hints about how to handle them
                    }
                }
            }
        }
        return true;
    };

    mnx::util::walkSequenceContent(sequence, hooks);
}

void MnxImporter::importPartMeasures()
{
    /// pass1: create ChordRests and clefs
    for (const auto& mnxPart : mnxDocument().parts()) {
        for (const auto& partMeasure : mnxPart.measures()) {
            Measure* measure = mnxMeasureToMeasure(partMeasure.calcArrayIndex());
            importSequences(mnxPart, partMeasure, measure);
            if (const auto mnxClefs = partMeasure.clefs()) {
                createClefs(mnxPart, mnxClefs.value(), measure);
            }
        }
    }
    /// pass2: add beams, dynamics, ottavas, ties, and slurs
    for (const auto& mnxPart : mnxDocument().parts()) {
        for (const auto& partMeasure : mnxPart.measures()) {
            Measure* measure = mnxMeasureToMeasure(partMeasure.calcArrayIndex());
            createDynamics(partMeasure, measure);
            createOttavas(partMeasure, measure);
            createBeams(partMeasure);
            for (const auto& sequence : partMeasure.sequences()) {
                processSequencePass2(sequence);
            }
        }
    }
}
} // namespace mu::iex::mnxio
