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

#include "engraving/dom/barline.h"
#include "engraving/dom/engravingitem.h"
#include "engraving/dom/jump.h"
#include "engraving/dom/key.h"
#include "engraving/dom/layoutbreak.h"
#include "engraving/dom/marker.h"
#include "engraving/dom/measure.h"
#include "engraving/dom/measurebase.h"
#include "engraving/dom/score.h"
#include "engraving/dom/staff.h"
#include "engraving/dom/tempotext.h"
#include "log.h"
#include "internal/shared/mnxtypesconv.h"

using namespace mu::engraving;

namespace mu::iex::mnxio {
namespace {

class MeasureNumberState
{
public:
    MeasureNumberState() { init(); }

    int displayNumber(const Measure* measure)
    {
        updateForMeasure(measure);
        return m_displayNumber;
    }

private:
    void init()
    {
        m_measureNo = 1;
        m_measureNoOffset = 0;
        m_displayNumber = 1;
    }

    void updateForMeasure(const Measure* measure)
    {
        const MeasureBase* previousMB = measure->prev();
        if (previousMB) {
            previousMB = previousMB->findPotentialSectionBreak();
        }
        if (previousMB) {
            const LayoutBreak* layoutBreak = previousMB->sectionBreakElement();
            if (layoutBreak && layoutBreak->startWithMeasureOne()) {
                init();
            }
        }

        m_measureNoOffset = measure->noOffset();
        m_measureNo += m_measureNoOffset;

        if (measure->isAnacrusis()) {
            m_displayNumber = 0;
            return;
        }

        m_displayNumber = m_measureNo++;
    }

    int m_measureNo = 1;
    int m_measureNoOffset = 0;
    int m_displayNumber = 1;
};

} // namespace

//---------------------------------------------------------
//   assignTimeSignature
//---------------------------------------------------------

static void assignTimeSignature(mnx::global::Measure& mnxMeasure, const Measure* measure,
                         std::optional<Fraction>& prevTimeSig)
{
    const Fraction timeSig = measure->timesig();
    if (prevTimeSig && timeSig.identical(*prevTimeSig)) {
        return;
    }

    const auto unit = toMnxTimeSignatureUnit(timeSig.denominator());
    if (!unit) {
        LOGW() << "Skipping time signature with unsupported MNX time signature unit: " << timeSig.denominator();
        return;
    }

    mnxMeasure.ensure_time(timeSig.numerator(), *unit);
    prevTimeSig = timeSig;
}

//---------------------------------------------------------
//   assignKeySignature
//---------------------------------------------------------

static void assignKeySignature(mnx::global::Measure& mnxMeasure, const Score* score, const Measure* measure,
                        std::optional<int>& prevKeyFifths)
{
    if (score->staves().empty()) {
        return;
    }

    const Staff* staff = score->staff(0);
    const KeySigEvent keySigEvent = staff->keySigEvent(measure->tick());
    if (!keySigEvent.isValid()) {
        return;
    }

    const int keyFifths = static_cast<int>(keySigEvent.concertKey());
    if (keyFifths != prevKeyFifths) {
        mnxMeasure.ensure_key(keyFifths);
        prevKeyFifths = keyFifths;
    }

    prevKeyFifths = keyFifths;
}

//---------------------------------------------------------
//   assignBarline
//---------------------------------------------------------

static void assignBarline(mnx::global::Measure& mnxMeasure, const Measure* measure)
{
    if (!measure->endBarLineVisible()) {
        mnxMeasure.ensure_barline(mnx::BarlineType::NoBarline);
        return;
    }

    const mnx::BarlineType barlineType = toMnxBarLineType(measure->endBarLineType());

    if (barlineType != mnx::BarlineType::Regular) {
        mnxMeasure.ensure_barline(barlineType);
    }
}

//---------------------------------------------------------
//   assignRepeats
//---------------------------------------------------------

static void assignRepeats(mnx::global::Measure& mnxMeasure, const Measure* measure)
{
    if (measure->repeatStart()) {
        mnxMeasure.ensure_repeatStart();
    }

    if (measure->repeatEnd()) {
        auto repeatEnd = mnxMeasure.ensure_repeatEnd();
        const int repeatCount = measure->repeatCount();
        if (repeatCount > 0 && repeatCount != 2) {
            repeatEnd.set_times(repeatCount);
        }
    }
}

//---------------------------------------------------------
//   exportMeasureElements
//   export measure-level elements (jumps, markers, tempos)
//---------------------------------------------------------

static void exportMeasureElements(mnx::global::Measure& mnxMeasure, const Measure* measure)
{
    for (EngravingItem* item : measure->el()) {
        if (!item) {
            continue;
        }
        switch (item->type()) {
        case ElementType::JUMP:
            /// @todo Export jump (mnx::global::Jump).
            break;
        case ElementType::MARKER: {
            const Marker* marker = toMarker(item);
            if (marker && marker->markerType() == MarkerType::FINE) {
                /// @todo Export fine (mnx::global::Fine).
            } else if (marker && marker->isSegno()) {
                /// @todo Export segno (mnx::global::Segno).
            }
            break;
        }
        case ElementType::TEMPO_TEXT:
            /// @todo Export tempos (mnx::global::Tempo).
            break;
        default:
            break;
        }
    }
}

//---------------------------------------------------------
//   createGlobal
//---------------------------------------------------------

void MnxExporter::createGlobal()
{
    /// @todo Export global lyrics metadata (mnx::global::LyricsGlobal).
    /// @todo Export global sounds dictionary (mnx::global::Sound).

    if (!m_score) {
        return;
    }

    auto mnxMeasures = m_mnxDocument.global().measures();
    MeasureNumberState measureNumberState;
    std::optional<Fraction> prevTimeSig;
    std::optional<int> prevKeyFifths;
    size_t measureIndex = 0;

    for (const Measure* measure = m_score->firstMeasure(); measure; measure = measure->nextMeasure()) {
        auto mnxMeasure = mnxMeasures.append();
        m_measToMnxMeas.emplace(measure, measureIndex);

        assignBarline(mnxMeasure, measure);
        assignRepeats(mnxMeasure, measure);
        assignTimeSignature(mnxMeasure, measure, prevTimeSig);
        assignKeySignature(mnxMeasure, m_score, measure, prevKeyFifths);
        exportMeasureElements(mnxMeasure, measure);

        const int displayNumber = measureNumberState.displayNumber(measure);
        if (displayNumber == 0) {
            /// @todo MNX does not support pickup measures; export as measure 0.
            mnxMeasure.set_number(displayNumber);
        } else if (displayNumber != static_cast<int>(measureIndex + 1)) {
            mnxMeasure.set_number(displayNumber);
        }

        ++measureIndex;
    }
}

} // namespace mu::iex::mnxio
