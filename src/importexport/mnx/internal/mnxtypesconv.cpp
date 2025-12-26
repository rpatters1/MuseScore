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
#include <algorithm>

#include "mnxtypesconv.h"

using namespace mu::engraving;
using namespace ::mnx;

namespace mu::iex::mnx {

engraving::Fraction mnxFractionValueToFraction(const ::mnx::FractionValue& fraction)
{
    return engraving::Fraction(fraction.numerator(), fraction.denominator());
}

ClefType mnxClefToClefType(const ::mnx::part::Clef& mnxClef)
{
    const auto snapToLine = [](int staffPositionHalfSpaces) -> int {
        // clefTable only supports clefs centered on lines (even half-spaces).
        if (staffPositionHalfSpaces & 1) {
            staffPositionHalfSpaces += (staffPositionHalfSpaces > 0 ? -1 : +1); // snap toward 0
        }
        return staffPositionHalfSpaces;
    };

    const int sp = snapToLine(mnxClef.staffPosition());
    const auto sign = mnxClef.sign();
    const auto octave = mnxClef.octave();

    switch (sign) {
    case ClefSign::GClef:
        // G clef: standard at sp == -2, also supports G_1 at sp == -4
        if (sp == -4) {
            return ClefType::G_1; // best-effort: ignore octave for this placement
        }

        switch (octave) {
        case OttavaAmountOrZero::NoTransposition: return ClefType::G;
        case OttavaAmountOrZero::TwoOctavesDown:  return ClefType::G15_MB;
        case OttavaAmountOrZero::OctaveDown:      return ClefType::G8_VB;
        case OttavaAmountOrZero::OctaveUp:        return ClefType::G8_VA;
        case OttavaAmountOrZero::TwoOctavesUp:    return ClefType::G15_MA;
        default:                                  return ClefType::INVALID;
        }

    case ClefSign::FClef:
        // F clef: supports sp == 0 (F_B line 3), sp == +2 (F line 4), sp == +4 (F_C line 5)
        if (sp == 0) {
            return ClefType::F_B; // best-effort: ignore octave for this placement
        }
        if (sp == +4) {
            return ClefType::F_C; // best-effort: ignore octave for this placement
        }

        switch (octave) {
        case OttavaAmountOrZero::NoTransposition: return ClefType::F;
        case OttavaAmountOrZero::TwoOctavesDown:  return ClefType::F15_MB;
        case OttavaAmountOrZero::OctaveDown:      return ClefType::F8_VB;
        case OttavaAmountOrZero::OctaveUp:        return ClefType::F_8VA;
        case OttavaAmountOrZero::TwoOctavesUp:    return ClefType::F_15MA;
        default:                                  return ClefType::INVALID;
        }

    case ClefSign::CClef:
        // Special-case explicit octave variant in your clefTable
        if (sp == +2 && octave == OttavaAmountOrZero::OctaveDown) {
            return ClefType::C4_8VB;
        }

        switch (sp) {
        case -4: return ClefType::C1;
        case -2: return ClefType::C2;
        case  0: return ClefType::C3;
        case +2: return ClefType::C4;
        case +4: return ClefType::C5;
        default:
            // If MNX gives something outside these, pick a sane fallback:
            return ClefType::C3;
        }

    default:
        return ClefType::INVALID;
    }
}

} // namespace mu::iex::musx
