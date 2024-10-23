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

#include <gtest/gtest.h>

#include "dom/rest.h"
#include "dom/staff.h"

#include "utils/scorerw.h"

#include "log.h"

using namespace mu;
using namespace mu::engraving;

static const String REST_DATA_DIR(u"rest_data/");
static const int TICKS_PER_4_2_MEASURE = 8 * 480; // 4/2 time per measure tick (8 quarters)
static const int TICKS_PER_4_4_MEASURE = 4 * 480; // 4/4 time per measure tick (4 quarters)

class Engraving_RestTests : public ::testing::Test
{
protected:
    void SetUp() override
    {
        //! NOTE: allows to read test files using their version readers
        //! instead of using 302 (see mscloader.cpp, makeReader)
        MScore::useRead302InTestMode = false;
    }

    void TearDown() override
    {
        MScore::useRead302InTestMode = true;
    }

    Rest* findRest(const MasterScore* score, int tick, track_idx_t track = 0) const
    {
        IF_ASSERT_FAILED(score) {
            return nullptr;
        }

        ChordRest* cr = score->findCR(Fraction::fromTicks(tick), track);
        if (cr->isRest()) {
            return toRest(cr);
        }

        return nullptr;
    }

public:
};

TEST_F(Engraving_RestTests, BreveRests_TestFullmeasureLines)
{
    MasterScore* score = ScoreRW::readScore(REST_DATA_DIR + u"rest03.mscz");
    ASSERT_TRUE(score);

    std::vector<int> expectedLines;
    std::vector<track_idx_t> restTracks = { 0, 0, 1, 0, 0, 1 };

    auto calcTick = [](int measureNum) -> int {
        const int measureIdx = measureNum - 1;
        // 3 bars of 4/2 followed by 3 bars of 4/4
        return (std::max(0, std::min(3, measureIdx)) * TICKS_PER_4_2_MEASURE)
               + (std::max(0, measureIdx - 3) * TICKS_PER_4_4_MEASURE);
    };

    auto testBars = [&]() {
        // [GIVEN] fullmeasure rests in each bar
        for (int measureNum = 1; measureNum <= static_cast<int>(restTracks.size()); measureNum++) {
            Rest* rest = findRest(score, calcTick(measureNum), restTracks[measureNum - 1]);
            ASSERT_TRUE(rest);
            // [THEN] ledger numbers match on all bars
            int visibleLine = std::floor(rest->pos().y() / (rest->staff()->lineDistance(rest->tick()) * rest->spatium()));
            EXPECT_EQ(visibleLine, expectedLines[measureNum - 1]);
        }
    };

    // [GIVEN] Style setting for multiVoice 2 space is true
    score->style().set(Sid::multiVoiceRestTwoSpaceOffset, true);
    score->doLayout();
    expectedLines = { 2, 0, 4, 1, -1, 4 };
    testBars();

    // [GIVEN] Style setting for multiVoice 2 space is false
    score->style().set(Sid::multiVoiceRestTwoSpaceOffset, false);
    score->doLayout();
    expectedLines = { 2, 1, 3, 1, 0, 3 };
    testBars();

    delete score;
}
