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
#pragma once

#include <memory>
#include <unordered_map>
#include <map>

#include "engraving/types/types.h"

#include "mnxdom.h"

namespace mu::engraving {
class Instrument;
class Measure;
class Part;
class Score;
class Staff;
} // namespace mu::engraving

namespace mu::iex::mnxio {

class MnxImporter
{
public:
    MnxImporter(engraving::Score* s, mnx::Document&& doc)
        : m_score(s), m_mnxDocument(std::move(doc)) {}
    void importMnx();

    const mnx::Document& mnxDocument() const { return m_mnxDocument; }

    engraving::Score* score() const { return m_score; }

private:
    //parts
    void importParts();
    void createStaff(engraving::Part* part, const mnx::Part& mnxPart, int staffNum);

    // global measures
    void importGlobalMeasures();
    void createKeySig(engraving::Measure* measure, const mnx::KeySignature& mnxKey);
    void createTimeSig(engraving::Measure* measure, const mnx::TimeSignature& timeSig);
    void setBarline(engraving::Measure* measure, const mnx::global::Barline& barline);

    // part measures
    void importPartMeasures();
    void importSequences(const mnx::Part& mnxPart, const mnx::part::Measure& partMeasure,
                         engraving::Measure* measure);
    void createClefs(const mnx::Part& mnxPart, const mnx::Array<mnx::part::PositionedClef>& mnxClefs,
                     engraving::Measure* measure);

    // utility funcs
    engraving::Staff* mnxPartStaffToStaff(const mnx::Part& mnxPart, int staffNum);

    std::unordered_map<size_t, muse::ID> m_mnxPartToPartId;
    // ordered map avoids need for hash on std::pair
    std::map<std::pair<size_t, int>, engraving::staff_idx_t> m_mnxPartStaffToStaff;
    std::unordered_map<engraving::staff_idx_t, size_t> m_StaffToMnxPart;
    std::unordered_map<size_t, engraving::Fraction> m_mnxMeasToTick;

    engraving::Score* m_score{};
    mnx::Document m_mnxDocument;
};

} // namespace mu::iex::mnxio
