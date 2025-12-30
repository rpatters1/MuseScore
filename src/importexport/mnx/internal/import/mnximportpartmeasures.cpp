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
#include "mnximporter.h"
#include "internal/shared/mnxtypesconv.h"

#include "engraving/dom/barline.h"
#include "engraving/dom/bracketItem.h"
#include "engraving/dom/factory.h"
#include "engraving/dom/instrtemplate.h"
#include "engraving/dom/keysig.h"
#include "engraving/dom/part.h"
#include "engraving/dom/rest.h"
#include "engraving/dom/score.h"
#include "engraving/dom/staff.h"
#include "engraving/dom/volta.h"

#include "mnxdom.h"

using namespace mu::engraving;

namespace mu::iex::mnxio {

// return true if a ChordRest was created.
bool MnxImporter::importEvent(const mnx::sequence::Event& event, track_idx_t curTrackIdx, Measure* measure,
                              const mnx::FractionValue& startTick, const mnx::FractionValue& actualDur)
{
    return false;
}

// return true if any ChordRest was created
bool MnxImporter::importNonGraceEvents(const mnx::Sequence& sequence, Measure* measure, track_idx_t curTrackIdx)
{
    bool insertedCR = false;

    mnx::util::SequenceWalkHooks hooks;
    hooks.onItem = [&](const mnx::ContentObject& /*item*/, mnx::util::SequenceWalkContext&) {
        return mnx::util::SequenceWalkControl::SkipChildren; /// @todo implement children.
    };
    hooks.onEvent = [&](const mnx::sequence::Event& event,
                        const mnx::FractionValue& startTick,
                        const mnx::FractionValue& actualDur, [[maybe_unused]]mnx::util::SequenceWalkContext& ctx) {
        IF_ASSERT_FAILED(!ctx.inGrace) {
            LOGE() << "Encountered grace when processing non-grace.";
            return true;
        }
        if (importEvent(event, curTrackIdx, measure, startTick, actualDur)) {
            insertedCR = true;
        }
        return true;
    };

    mnx::util::walkSequenceContent(sequence, hooks);

    return insertedCR;
}

void MnxImporter::importGraceEvents(const mnx::Sequence& sequence, Measure* measure, track_idx_t curTrackIdx)
{
    mnx::util::SequenceWalkHooks hooks;
    /// @todo Remove this hook when we implement tuplets and other children
    hooks.onItem = [&](const mnx::ContentObject& item, mnx::util::SequenceWalkContext&) {
        if (item.type() != mnx::sequence::Grace::ContentTypeValue) {
            return mnx::util::SequenceWalkControl::SkipChildren;
        }
        return mnx::util::SequenceWalkControl::Continue;
    };
    hooks.onEvent = [&](const mnx::sequence::Event& event,
                        const mnx::FractionValue& startTick,
                        const mnx::FractionValue& actualDur, mnx::util::SequenceWalkContext& ctx) {
        if (ctx.inGrace) {
            importEvent(event, curTrackIdx, measure, startTick, actualDur);
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
        if (importNonGraceEvents(sequence, measure, curTrackIdx)) {
            importGraceEvents(sequence, measure, curTrackIdx); // if MuseScore refactors graces, maybe we don't need this.
            staffVoiceMap.push_back(voiceId);
        }
    }

    // add full measure rest to any staff with no sequence.
    for (int staffNum = 1; staffNum <= mnxPart.staves(); staffNum++) {
        if (staffVoiceMaps[static_cast<size_t>(staffNum - 1)].empty()) {
            Staff* staff = mnxPartStaffToStaff(mnxPart, staffNum);
            track_idx_t staffTrackIdx = staff2track(staff->idx());
            Segment* segment = measure->getSegmentR(SegmentType::ChordRest, Fraction(0, 1));
            Rest* rest = Factory::createRest(segment, TDuration(DurationType::V_MEASURE));
            rest->setScore(m_score);
            rest->setTicks(measure->timesig());
            rest->setTrack(staffTrackIdx);
            segment->add(rest);
        }
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
