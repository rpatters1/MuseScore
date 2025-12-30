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

void MnxImporter::importSequences(const mnx::Part& mnxPart, const mnx::part::Measure& partMeasure,
                                  Measure* measure)
{
    /// @todo actually process sequences from partMeasure, For now just add measure rests.
    for (int staffNum = 1; staffNum <= mnxPart.staves(); staffNum++) {
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

void MnxImporter::importPartMeasures()
{
    for (const mnx::Part& mnxPart : mnxDocument().parts()) {
        if (const auto partMeasures = mnxPart.measures()) {
            for (const mnx::part::Measure& partMeasure : *partMeasures) {
                Measure* measure = mnxMeasureToMeasure(partMeasure.calcArrayIndex());
                importSequences(mnxPart, partMeasure, measure);
                if (const auto mnxClefs = partMeasure.clefs()) {
                    createClefs(mnxPart, mnxClefs.value(), measure);
                }
                /// @todo add beams, dynamics, ottavas
            }
        }
    }
}

}
