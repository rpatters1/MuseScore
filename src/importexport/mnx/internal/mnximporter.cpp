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
#include "engraving/dom/volta.h"
#include "mnxtypesconv.h"

#include "engraving/dom/barline.h"
#include "engraving/dom/bracketItem.h"
#include "engraving/dom/factory.h"
#include "engraving/dom/instrtemplate.h"
#include "engraving/dom/keysig.h"
#include "engraving/dom/part.h"
#include "engraving/dom/rest.h"
#include "engraving/dom/score.h"
#include "engraving/dom/sig.h"
#include "engraving/dom/staff.h"
#include "engraving/dom/timesig.h"

#include "mnxdom.h"

using namespace mu::engraving;

namespace mu::iex::mnxio {

static void loadInstrument(Part* part, const mnx::Part& mnxPart, Instrument* instrument)
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
    if (const std::optional<mnx::part::PartTransposition> mnxTransp = mnxPart.transposition()) {
        instrument->setTranspose(Interval(-mnxTransp->interval().staffDistance(), -mnxTransp->interval().halfSteps()));
    }
}

Staff* MnxImporter::mnxPartStaffToStaff(const mnx::Part& mnxPart, int staffNum)
{
    staff_idx_t idx = muse::value(m_mnxPartStaffToStaff,
                                  std::make_pair(mnxPart.calcArrayIndex(), staffNum),
                                  muse::nidx);
    IF_ASSERT_FAILED(idx != muse::nidx) {
        throw std::logic_error("Unmapped staff encountered");
    }
    Staff* staff = m_score->staff(idx);
    IF_ASSERT_FAILED(staff) {
        throw std::logic_error("Invalid mapped staff index " + std::to_string(idx));
    }
    return staff;
}

Staff* MnxImporter::mnxLayoutStaffToStaff(const mnx::layout::Staff& mnxStaff)
{
    const auto sources = mnxStaff.sources();
    for (const auto& source : sources) {
        if (const auto part = mnxDocument().getIdMapping().tryGet<mnx::Part>(source.part())) {
            return mnxPartStaffToStaff(part.value(), source.staff());
        } else {
            LOGE() << "Staff source points to invalid part\"" << source.part() << "\" " << source.pointer().to_string()
                   << "\n" << source.dump(2);
        }
    }
    return nullptr;
}

Measure* MnxImporter::mnxMeasureToMeasure(const size_t mnxMeasIdx)
{
    Fraction measTick = muse::value(m_mnxMeasToTick, mnxMeasIdx, {-1, 1});
    IF_ASSERT_FAILED(measTick >= Fraction(0, 1)) {
        throw std::logic_error("MNX measure index " + std::to_string(mnxMeasIdx)
                               + " is not mapped.");
    }
    Measure* measure = m_score->tick2measure(measTick);
    IF_ASSERT_FAILED(measure) {
        throw std::logic_error("MNX measure index " + std::to_string(mnxMeasIdx)
                               + " has invalid tick " + measTick.toString().toStdString());
    }
    return measure;
}

void MnxImporter::importSettings()
{
    /// @todo add settings as MNX adds them

    // MNX specifies that the barline of the last bar is a finale barline by default.
    // This appears always to be the case for MuseScore as well, so nothing needs to be done for this.
}

void MnxImporter::createStaff(Part* part, const mnx::Part& mnxPart, int staffNum)
{
    Staff* staff = Factory::createStaff(part);
    m_score->appendStaff(staff);
    m_mnxPartStaffToStaff.emplace(std::make_pair(mnxPart.calcArrayIndex(), staffNum), staff->idx());
    m_StaffToMnxPart.emplace(staff->idx(), mnxPart.calcArrayIndex());
}

void MnxImporter::importParts()
{
    size_t partNum = 0;
    for (const mnx::Part& mnxPart : mnxDocument().parts()) {
        partNum++;
        Part * part = new Part(m_score);
        /// @todo a better way to find the instrument, perhaps by part name or else some future mnx enhancement
        const InstrumentTemplate* it = [&]() {
            if (mnxPart.kit()) {
                return searchTemplate(u"drumset");
            }
            return searchTemplate(u"piano");
        }();
        if (it) {
            part->initFromInstrTemplate(it);
        }
        part->setPartName(String::fromStdString(mnxPart.name_or("Part " + mnxPart.id_or(std::to_string((partNum))))));
        part->setLongName(String::fromStdString(mnxPart.name_or("")));
        part->setShortName(String::fromStdString(mnxPart.shortName_or("")));
        loadInstrument(part, mnxPart, part->instrument());
        for (int staffNum = 1; staffNum <= mnxPart.staves(); staffNum++) {
            createStaff(part, mnxPart, staffNum);
        }
        m_score->appendPart(part);
        m_mnxPartToPartId.emplace(mnxPart.calcArrayIndex(), part->id());
    }
}

void MnxImporter::importBrackets()
{
    const auto fullScoreLayout = mnxDocument().findFullScoreLayout();
    if (!fullScoreLayout) {
        LOGW() << "Unable to find full score layout.\n";
        return;
    }
    const auto layoutSpans = mnx::util::buildLayoutSpans(fullScoreLayout.value());
    if (!layoutSpans) {
        LOGE() << "Layout spans for full score layout were invalid.\n";
        return;
    }
    const auto layoutStaves = mnx::util::flattenLayoutStaves(fullScoreLayout.value());
    if (!layoutStaves) {
        LOGE() << "Layout staves for full score layout were invalid.\n";
        return;
    }

    for (const auto& span : layoutSpans.value()) {
        BracketType brt = toMuseScoreBracketType(span.symbol.value_or(mnx::LayoutSymbol::NoSymbol));
        if (brt == BracketType::NO_BRACKET && span.startIndex >= span.endIndex) {
            continue;
        }
        Staff* staff = mnxLayoutStaffToStaff(layoutStaves->at(span.startIndex));
        if (!staff) {
            LOGE() << "Staff not found for span starting at " << span.startIndex
                   << " and ending at " << span.endIndex << ".\n";
            continue;
        }
        BracketItem* bi = Factory::createBracketItem(m_score->dummy());
        bi->setBracketType(brt);
        const int groupSpan = static_cast<int>(span.endIndex - span.startIndex + 1);
        bi->setBracketSpan(groupSpan);
        bi->setColumn(size_t(span.depth));
        const staff_idx_t staffIdx = staff->idx();
        /// @todo as MNX adds barline options to groups, this will become more complicated.
        m_score->staff(staffIdx)->addBracket(bi);
        if (groupSpan > 1) {
            size_t currIndex = m_barlineSpans.size();
            m_barlineSpans.push_back(std::make_pair(staffIdx, staffIdx + static_cast<staff_idx_t>(groupSpan - 1)));
            // Barline defaults (these will be overridden later, but good to have nice defaults)
            for (staff_idx_t idx = staffIdx; idx < staffIdx + static_cast<staff_idx_t>(groupSpan - 1); idx++) {
                m_score->staff(idx)->setBarLineSpan(true);
                m_score->staff(idx)->setBarLineTo(0);
                m_staffToSpan.emplace(idx, currIndex);
            }
        }
    }
}

void MnxImporter::createKeySig(engraving::Measure* measure, const mnx::KeySignature& mnxKey)
{
    const Key concertKey = mnxFifthsToKey(mnxKey.fifths());
    if (concertKey == Key::INVALID) {
        LOGE() << "invalid mnx key fifths " << mnxKey.fifths() << " for measure " << measure->measureIndex();
        return;
    }
    for (staff_idx_t idx = 0; idx < m_score->nstaves(); idx++) {
        Staff* staff = m_score->staff(idx);
        KeySigEvent keySigEvent;
        keySigEvent.setConcertKey(concertKey);
        keySigEvent.setKey(concertKey);
        if (!score()->style().styleB(Sid::concertPitch)) {
            const size_t mnxPartIndex = muse::value(m_StaffToMnxPart, idx, muse::nidx);
            IF_ASSERT_FAILED(mnxPartIndex != muse::nidx) {
                throw std::logic_error("Staff " + std::to_string(idx) + " is not mapped.");
            }
            const mnx::Part mnxPart = mnxDocument().parts()[mnxPartIndex];
            if (const std::optional<mnx::part::PartTransposition>& partTransposition = mnxPart.transposition()) {
                int transpFifths = partTransposition->calcTransposedKeyFifthsFor(mnxKey);
                const Key transpKey = mnxFifthsToKey(transpFifths);
                if (transpKey != Key::INVALID) {
                    keySigEvent.setKey(transpKey);
                } else {
                    // set the document to concert pitch and let MuseScore deal with it.
                    LOGW() << "invalid mnx transposed key fifths " << transpFifths << " for measure " << measure->measureIndex();
                    m_score->style().set(Sid::concertPitch, true);
                }
            }
        }
        Segment* seg = measure->getSegmentR(SegmentType::KeySig, Fraction(0, 1));
        KeySig* ks = Factory::createKeySig(seg);
        ks->setKeySigEvent(keySigEvent);
        ks->setTrack(staff2track(idx));
        seg->add(ks);
        staff->setKey(measure->tick(), ks->keySigEvent());
    }
}

void MnxImporter::createTimeSig(engraving::Measure* measure, const mnx::TimeSignature& timeSig)
{
    /// @todo Eventually, as mnx develops, we may get more sophisticated here than just a Fraction.
    const Fraction sigFraction = mnxFractionValueToFraction(timeSig);
    for (staff_idx_t idx = 0; idx < m_score->staves().size(); idx++) {
        Segment* seg = measure->getSegmentR(SegmentType::TimeSig, Fraction(0, 1));
        TimeSig* ts = Factory::createTimeSig(seg);
        ts->setSig(sigFraction);
        ts->setTrack(staff2track(idx));
        seg->add(ts);
    }
}

void MnxImporter::setBarline(engraving::Measure* measure, const mnx::global::Barline& barline)
{
    const mnx::BarlineType mnxBlt = barline.type();
    BarLineType blt = toMuseScoreBarLineType(mnxBlt);
    Segment* bls = measure->getSegmentR(SegmentType::EndBarLine, measure->ticks());

    for (staff_idx_t idx = 0; idx < m_score->staves().size(); idx++) {
        BarLine* bl = Factory::createBarLine(bls);
        bl->setParent(bls);
        bl->setTrack(staff2track(idx));
        bl->setVisible(mnxBlt != mnx::BarlineType::NoBarline);
        bl->setGenerated(false);
        bl->setBarLineType(blt);
        if (mnxBlt == mnx::BarlineType::Tick) {
            int lines = bl->staff()->lines(bls->tick() - Fraction::eps()) - 1;
            bl->setSpanFrom(BARLINE_SPAN_TICK1_FROM + (lines == 0 ? BARLINE_SPAN_1LINESTAFF_FROM : 0));
            bl->setSpanTo((lines == 0 ? BARLINE_SPAN_1LINESTAFF_FROM : (2 * -lines)) + 1);
        } else if (mnxBlt == mnx::BarlineType::Short) {
            bl->setSpanFrom(BARLINE_SPAN_SHORT1_FROM);
            bl->setSpanTo(BARLINE_SPAN_SHORT1_TO);
        } else {
            bl->setSpanStaff(m_staffToSpan.find(idx) != m_staffToSpan.end());
            bl->setSpanFrom(0);
            bl->setSpanTo(0);
        }
        bls->add(bl);
    }
}

void MnxImporter::createVolta(engraving::Measure* measure, const mnx::global::Ending& ending)
{
    track_idx_t voltaTrackIdx = 0; /// @todo more options as indicated by mnx spec.

    Measure* endMeasure = measure;
    for (int countdown = ending.duration() - 1; countdown > 0; countdown--) {
        Measure* next = endMeasure->nextMeasure();
        if (!next) {
            LOGW() << "Ending at " << ending.pointer().to_string() << " specifies non-existent end measure\n"
                   << ending.dump(2);
        }
        endMeasure = next;
    }

    Volta* volta = Factory::createVolta(m_score->dummy());
    volta->setTrack(voltaTrackIdx);
    volta->setTick(measure->tick());
    volta->setTick2(endMeasure->endTick());
    volta->setVisible(true);
    if (const auto& numbers = ending.numbers()) {
        volta->setEndings(numbers->toStdVector());
        // use default MuseScore ending text format, based on observed defaults in 4.6.x
        String text;
        for (int number : *numbers) {
            if (!text.empty()) {
                text += u", ";
            }
            text += String("%1").arg(number);
        }
        text += u".";
        volta->setText(text);
    }
    volta->setVoltaType(ending.open() ? Volta::Type::OPEN : Volta::Type::CLOSED);
    m_score->addElement(volta);
}

void MnxImporter::importGlobalMeasures()
{
    Fraction currTimeSig(4, 4);
    m_score->sigmap()->clear();
    m_score->sigmap()->add(0, currTimeSig);

    // pass 1 creates the measures as it goes
    for (const mnx::global::Measure& mnxMeasure : mnxDocument().global().measures()) {
        Measure* measure = Factory::createMeasure(m_score->dummy()->system());
        Fraction tick(m_score->last() ? m_score->last()->endTick() : Fraction(0, 1));
        measure->setTick(tick);
        if (const std::optional<mnx::TimeSignature>& mnxTimeSig = mnxMeasure.time()) {
            Fraction thisTimeSig = mnxFractionValueToFraction(mnxTimeSig.value());
            if (!thisTimeSig.identical(currTimeSig)) {
                m_score->sigmap()->add(tick.ticks(), thisTimeSig);
                currTimeSig = thisTimeSig;
            }
            createTimeSig(measure, mnxTimeSig.value());
        }
        if (const std::optional<mnx::KeySignature>& keySig = mnxMeasure.key()) {
            createKeySig(measure, keySig.value());
        }
        if (const std::optional<mnx::global::Barline>& barline = mnxMeasure.barline()) {
            setBarline(measure, barline.value());
        }
        if (mnxMeasure.repeatStart()) {
            measure->setRepeatStart(true);
        }
        if (const std::optional<mnx::global::RepeatEnd>& rpt = mnxMeasure.repeatEnd()) {
            measure->setRepeatEnd(true);
            if (const std::optional<int> nTimes = rpt->times()) {
                measure->setRepeatCount(*nTimes);
            }
        }
        /// @todo fine, jump, measure number, segno, tempos

        measure->setTimesig(currTimeSig);
        measure->setTicks(currTimeSig);
        m_score->measures()->append(measure);
        m_mnxMeasToTick.emplace(mnxMeasure.calcArrayIndex(), tick);
    }

    // pass 2 for items that require all measures to exist already
    for (const mnx::global::Measure& mnxMeasure : mnxDocument().global().measures()) {
        Measure* measure = mnxMeasureToMeasure(mnxMeasure.calcArrayIndex());
        if (const std::optional<mnx::global::Ending>& ending = mnxMeasure.ending()) {
            createVolta(measure, ending.value());
        }
    }
}

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

void MnxImporter::createClefs(const mnx::Part& mnxPart, const mnx::Array<mnx::part::PositionedClef>& mnxClefs,
                              engraving::Measure* measure)
{
    for (const mnx::part::PositionedClef& mnxClef : mnxClefs) {
        Staff* staff = mnxPartStaffToStaff(mnxPart, mnxClef.staff());
        Fraction rTick{};
        if (const std::optional<mnx::RhythmicPosition>& position = mnxClef.position()) {
            rTick = mnxFractionValueToFraction(position->fraction()).reduced();
        }
        ClefType clefType = mnxClefToClefType(mnxClef.clef());
        if (clefType != ClefType::INVALID) {
            const bool isHeader = !measure->prevMeasure() && rTick.isZero();
            Segment* clefSeg = measure->getSegmentR(isHeader ? SegmentType::HeaderClef : SegmentType::Clef, rTick);
            Clef* clef = Factory::createClef(clefSeg);
            clef->setTrack(staff2track(staff->idx()));
            clef->setConcertClef(clefType);
            clef->setTransposingClef(clefType);
            clef->setGenerated(false);
            clef->setIsHeader(isHeader);
            clefSeg->add(clef);
        } else {
            LOGE() << "Unsupported clef encountered at " << mnxClef.pointer().to_string();
        }
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

void MnxImporter::importMnx()
{
    if (!m_mnxDocument.hasIdMapping()) {
        m_mnxDocument.buildIdMapping();
    }
    importSettings();
    importParts();
    importBrackets();
    importGlobalMeasures();
    importPartMeasures();
}

} // namespace mu::iex::mnxio
