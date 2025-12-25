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
#include "mnxtypesconv.h"

#include "engraving/dom/factory.h"
#include "engraving/dom/instrtemplate.h"
#include "engraving/dom/part.h"
#include "engraving/dom/score.h"
#include "engraving/dom/sig.h"
#include "engraving/dom/staff.h"

#include "mnxdom.h"

using namespace mu::engraving;

namespace mu::iex::mnx {

static void loadInstrument(engraving::Part* part, const ::mnx::Part& mnxPart, Instrument* instrument)
{
    // Initialize drumset
    if (mnxPart.kit().has_value()) {
        instrument->setUseDrumset(true);
        /// @todo import kit
        // instrument->setDrumset(createDrumset(percNoteInfoList, musxStaff, instrument));
    } else {
        instrument->setUseDrumset(false);
    }

    // Names
    instrument->setTrackName(part->partName());
    instrument->setLongName(part->longName());
    instrument->setShortName(part->shortName());

    // Transposition
    // MNX transposition has opposite signs.
    if (const std::optional<::mnx::part::PartTransposition> mnxTransp = mnxPart.transposition()) {
        instrument->setTranspose(engraving::Interval(-mnxTransp->interval().staffDistance(), -mnxTransp->interval().halfSteps()));
    }
}

void MnxImporter::createStaff(engraving::Part* part)
{
    Staff* staff = Factory::createStaff(part);
    m_score->appendStaff(staff);
}

void MnxImporter::importParts()
{
    size_t partNum = 0;
    for (const ::mnx::Part& mnxPart : mnxDocument().parts()) {
        partNum++;
        engraving::Part * part = new engraving::Part(m_score);
        /// @todo a better way to find the instrument, perhaps by part name or else some future mnx enhancement
        const InstrumentTemplate* it = searchTemplate(u"piano");
        if (it) {
            part->initFromInstrTemplate(it);
        }
        part->setPartName(String::fromStdString(mnxPart.name_or("Part " + mnxPart.id_or(std::to_string((partNum))))));
        part->setLongName(String::fromStdString(mnxPart.name_or("")));
        part->setShortName(String::fromStdString(mnxPart.shortName_or("")));
        loadInstrument(part, mnxPart, part->instrument());
        for (int x = 0; x < mnxPart.staves(); x++) {
            createStaff(part);
        }
        m_score->appendPart(part);
    }
}

void MnxImporter::importMeasures()
{
    engraving::Fraction currTimeSig(4, 4);
    m_score->sigmap()->clear();
    m_score->sigmap()->add(0, currTimeSig);

    for (const ::mnx::global::Measure& mnxMeasure : mnxDocument().global().measures()) {
        Measure* measure = Factory::createMeasure(m_score->dummy()->system());
        engraving::Fraction tick(m_score->last() ? m_score->last()->endTick() : engraving::Fraction(0, 1));
        measure->setTick(tick);
        if (const std::optional<::mnx::TimeSignature>& mnxTimeSig = mnxMeasure.time()) {
            engraving::Fraction thisTimeSig = mnxFractionValueToFraction(mnxTimeSig.value());
            if (thisTimeSig != currTimeSig) {
                m_score->sigmap()->add(tick.ticks(), thisTimeSig);
                currTimeSig = thisTimeSig;
            }
            measure->setTimesig(thisTimeSig);
            measure->setTicks(thisTimeSig);
            m_score->measures()->append(measure);
        }
    }
}

void MnxImporter::importSequences()
{

}

void MnxImporter::importMnx()
{
    if (!m_mnxDocument.hasIdMapping()) {
        m_mnxDocument.buildIdMapping();
    }
    importParts();
    importMeasures();
    importSequences();
}

} // namespace mu::iex::mnx
