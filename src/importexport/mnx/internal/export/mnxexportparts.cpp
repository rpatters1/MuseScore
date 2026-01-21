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

#include <optional>

#include "engraving/dom/clef.h"
#include "engraving/dom/instrument.h"
#include "engraving/dom/interval.h"
#include "engraving/dom/measure.h"
#include "engraving/dom/part.h"
#include "engraving/dom/score.h"
#include "engraving/dom/segment.h"
#include "internal/shared/mnxtypesconv.h"
#include "log.h"

using namespace mu::engraving;

namespace mu::iex::mnxio {
namespace {

void appendClefsForMeasure(const Part* part, const Measure* measure, mnx::part::Measure& mnxMeasure)
{
    /// @todo Revisit whether to export a default initial clef if MuseScore has none.
    const bool isFirstMeasure = (measure->prevMeasure() == nullptr);
    const size_t staves = part->nstaves();
    std::optional<mnx::Array<mnx::part::PositionedClef>> mnxClefs;

    for (Segment* segment = measure->first(); segment; segment = segment->next()) {
        const SegmentType segmentType = segment->segmentType();
        if (!(segmentType & SegmentType::ClefType)) {
            continue;
        }
        if (segmentType & SegmentType::CourtesyClefType) {
            continue;
        }
        if ((segmentType == SegmentType::HeaderClef) && !isFirstMeasure) {
            continue;
        }

        const Fraction rTick = segment->rtick();
        for (size_t staffIdx = 0; staffIdx < staves; ++staffIdx) {
            const track_idx_t track = part->startTrack() + VOICES * staffIdx;
            Clef* clef = toClef(segment->element(track));
            if (!clef || clef->isCourtesy()) {
                continue;
            }
            if (clef->generated() && !(rTick.isZero() && isFirstMeasure)) {
                continue;
            }

            const auto required = toMnxClefRequired(clef->clefType());
            if (!required) {
                LOGW() << "Skipping nsupported clef type in MNX export: " << int(clef->clefType());
                continue;
            }

            if (!mnxClefs) {
                mnxClefs = mnxMeasure.ensure_clefs();
            }

            auto mnxClef = mnxClefs->append(required->clefSign,
                                            required->staffPosition,
                                            required->octaveAdjustment);
            if (staves > 1) {
                mnxClef.set_staff(static_cast<int>(staffIdx + 1));
            }
            if (!rTick.isZero()) {
                mnxClef.ensure_position(mnx::FractionValue(rTick.numerator(), rTick.denominator()));
            }
        }
    }
}

} // namespace

void MnxExporter::createParts()
{
    if (!m_score) {
        return;
    }

    /// @todo Export part kit definitions (mnx::part::KitComponent).

    auto mnxParts = m_mnxDocument.parts();

    for (const Part* part : m_score->parts()) {
        auto mnxPart = mnxParts.append();

        const String longName = part->longName();
        if (!longName.isEmpty()) {
            mnxPart.set_name(longName.toStdString());
        }

        const String shortName = part->shortName();
        if (!shortName.isEmpty()) {
            mnxPart.set_shortName(shortName.toStdString());
        }

        mnxPart.set_or_clear_staves(static_cast<int>(part->nstaves()));

        const Instrument* instrument = part->instrument();
        if (instrument) {
            const Interval transpose = instrument->transpose();
            if (!transpose.isZero()) {
                mnxPart.ensure_transposition(mnx::Interval::make(-transpose.diatonic, -transpose.chromatic));
            }
        }

        auto mnxMeasures = mnxPart.measures();
        for (const Measure* measure = m_score->firstMeasure(); measure; measure = measure->nextMeasure()) {
            auto mnxMeasure = mnxMeasures.append();

            /// @todo Export beams (mnx::part::Beam).
            appendClefsForMeasure(part, measure, mnxMeasure);

            /// @todo Export dynamics (mnx::part::Dynamic).
            /// @todo Export ottavas (mnx::part::Ottava).
            createSequences(part, measure, mnxMeasure);
        }
    }
}

} // namespace mu::iex::mnxio
