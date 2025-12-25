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

#include "mnxdom.h"

namespace mu::engraving {
class Instrument;
class Part;
class Score;
} // namespace mu::engraving

namespace mu::iex::mnx {

class MnxImporter
{
public:
    MnxImporter(engraving::Score* s, ::mnx::Document&& doc)
        : m_score(s), m_mnxDocument(std::move(doc)) {}
    void importMnx();

    const ::mnx::Document& mnxDocument() const { return m_mnxDocument; }

    engraving::Score* score() const { return m_score; }

private:
    void importParts();
    void createStaff(engraving::Part* part);

    void importMeasures();
    void importSequences();

    engraving::Score* m_score{};
    ::mnx::Document m_mnxDocument;
};

} // namespace mu::iex::mnx
