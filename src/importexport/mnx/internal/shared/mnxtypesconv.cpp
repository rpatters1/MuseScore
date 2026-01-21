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
#include <unordered_map>

#include "engraving/dom/utils.h"
#include "engraving/dom/clef.h"
#include "framework/global/containers.h"

#include "notation/notationtypes.h"

#include "mnxtypesconv.h"

using namespace mu::engraving;

namespace mu::iex::mnxio {

namespace {

const std::unordered_map<mnx::BarlineType, BarLineType> barLineTypeTable = {
    { mnx::BarlineType::Regular,    BarLineType::NORMAL },
    { mnx::BarlineType::Dashed,     BarLineType::DASHED },
    { mnx::BarlineType::Dotted,     BarLineType::DOTTED },
    { mnx::BarlineType::Double,     BarLineType::DOUBLE },
    { mnx::BarlineType::Final,      BarLineType::END },
    { mnx::BarlineType::Heavy,      BarLineType::HEAVY },
    { mnx::BarlineType::HeavyHeavy, BarLineType::DOUBLE_HEAVY },
    { mnx::BarlineType::HeavyLight, BarLineType::REVERSE_END },
};

} // namespace

BarLineType toMuseScoreBarLineType(mnx::BarlineType blt)
{
    return muse::value(barLineTypeTable, blt, BarLineType::NORMAL);
}

mnx::BarlineType toMnxBarLineType(BarLineType blt)
{
    return muse::key(barLineTypeTable, blt, mnx::BarlineType::Regular);
}

std::optional<mnx::TimeSignatureUnit> toMnxTimeSignatureUnit(int denominator)
{
    static const std::unordered_map<int, std::optional<mnx::TimeSignatureUnit>> units = {
        { 1, mnx::TimeSignatureUnit::Whole },
        { 2, mnx::TimeSignatureUnit::Half },
        { 4, mnx::TimeSignatureUnit::Quarter },
        { 8, mnx::TimeSignatureUnit::Eighth },
        { 16, mnx::TimeSignatureUnit::Value16th },
        { 32, mnx::TimeSignatureUnit::Value32nd },
        { 64, mnx::TimeSignatureUnit::Value64th },
        { 128, mnx::TimeSignatureUnit::Value128th },
    };
    return muse::value(units, denominator, std::nullopt);
}

std::optional<mnx::part::Clef::Required> toMnxClefRequired(ClefType clefType)
{
    using Required = mnx::part::Clef::Required;
    using ClefSign = mnx::ClefSign;
    using OttavaAmountOrZero = mnx::OttavaAmountOrZero;

    ClefSign sign{};
    OttavaAmountOrZero octave = OttavaAmountOrZero::NoTransposition;

    switch (clefType) {
    case ClefType::G:
    case ClefType::G_1:
        sign = ClefSign::GClef;
        break;
    case ClefType::G8_VB:
    case ClefType::G8_VB_O:
    case ClefType::G8_VB_P:
    case ClefType::G8_VB_C:
        sign = ClefSign::GClef;
        octave = OttavaAmountOrZero::OctaveDown;
        break;
    case ClefType::G8_VA:
        sign = ClefSign::GClef;
        octave = OttavaAmountOrZero::OctaveUp;
        break;
    case ClefType::G15_MB:
        sign = ClefSign::GClef;
        octave = OttavaAmountOrZero::TwoOctavesDown;
        break;
    case ClefType::G15_MA:
        sign = ClefSign::GClef;
        octave = OttavaAmountOrZero::TwoOctavesUp;
        break;
    case ClefType::F:
    case ClefType::F_B:
    case ClefType::F_C:
    case ClefType::F_F18C:
    case ClefType::F_19C:
        sign = ClefSign::FClef;
        break;
    case ClefType::F8_VB:
        sign = ClefSign::FClef;
        octave = OttavaAmountOrZero::OctaveDown;
        break;
    case ClefType::F_8VA:
        sign = ClefSign::FClef;
        octave = OttavaAmountOrZero::OctaveUp;
        break;
    case ClefType::F15_MB:
        sign = ClefSign::FClef;
        octave = OttavaAmountOrZero::TwoOctavesDown;
        break;
    case ClefType::F_15MA:
        sign = ClefSign::FClef;
        octave = OttavaAmountOrZero::TwoOctavesUp;
        break;
    case ClefType::C1:
    case ClefType::C2:
    case ClefType::C3:
    case ClefType::C4:
    case ClefType::C5:
    case ClefType::C_19C:
    case ClefType::C1_F18C:
    case ClefType::C3_F18C:
    case ClefType::C4_F18C:
    case ClefType::C1_F20C:
    case ClefType::C3_F20C:
    case ClefType::C4_F20C:
        sign = ClefSign::CClef;
        break;
    case ClefType::C4_8VB:
        sign = ClefSign::CClef;
        octave = OttavaAmountOrZero::OctaveDown;
        break;
    case ClefType::INVALID:
    case ClefType::PERC:
    case ClefType::PERC2:
    case ClefType::TAB:
    case ClefType::TAB4:
    case ClefType::TAB_SERIF:
    case ClefType::TAB4_SERIF:
    case ClefType::MAX:
        return std::nullopt;
    }

    const int staffPosition = (ClefInfo::line(clefType) - 3) * 2;
    return Required { sign, staffPosition, octave };
}

std::optional<mnx::NoteValue::Required> toMnxNoteValue(const TDuration& duration)
{
    static const std::unordered_map<DurationType, std::optional<mnx::NoteValueBase>> noteValueBaseTable = {
        { DurationType::V_LONG,    mnx::NoteValueBase::Longa },
        { DurationType::V_BREVE,   mnx::NoteValueBase::Breve },
        { DurationType::V_WHOLE,   mnx::NoteValueBase::Whole },
        { DurationType::V_HALF,    mnx::NoteValueBase::Half },
        { DurationType::V_QUARTER, mnx::NoteValueBase::Quarter },
        { DurationType::V_EIGHTH,  mnx::NoteValueBase::Eighth },
        { DurationType::V_16TH,    mnx::NoteValueBase::Note16th },
        { DurationType::V_32ND,    mnx::NoteValueBase::Note32nd },
        { DurationType::V_64TH,    mnx::NoteValueBase::Note64th },
        { DurationType::V_128TH,   mnx::NoteValueBase::Note128th },
        { DurationType::V_256TH,   mnx::NoteValueBase::Note256th },
        { DurationType::V_512TH,   mnx::NoteValueBase::Note512th },
        { DurationType::V_1024TH,  mnx::NoteValueBase::Note1024th },
        { DurationType::V_ZERO,    std::nullopt },
        { DurationType::V_MEASURE, std::nullopt },
        { DurationType::V_INVALID, std::nullopt },
    };

    const auto base = muse::value(noteValueBaseTable, duration.type(), std::nullopt);
    if (!base) {
        return std::nullopt;
    }

    return mnx::NoteValue::make(*base, static_cast<unsigned>(duration.dots()));
}

BeamMode toMuseScoreBeamMode(int lowestBeamStart)
{
    static const std::unordered_map<int, BeamMode> beamModeTable = {
        { 0,    BeamMode::MID },
        { 1,    BeamMode::BEGIN },
        { 2,    BeamMode::BEGIN16 },
        { 3,    BeamMode::BEGIN32 },
        // MNX can go arbitrarily higher but MuseScore currently cannot
    };
    return muse::value(beamModeTable, lowestBeamStart, BeamMode::MID);
}

BracketType toMuseScoreBracketType(mnx::LayoutSymbol lys)
{
    static const std::unordered_map<mnx::LayoutSymbol, BracketType> bracketTable = {
        { mnx::LayoutSymbol::NoSymbol,   BracketType::NO_BRACKET },
        { mnx::LayoutSymbol::Brace,      BracketType::BRACE },
        { mnx::LayoutSymbol::Bracket,    BracketType::NORMAL },
    };
    return muse::value(bracketTable, lys, BracketType::NO_BRACKET);
}

DynamicType toMuseScoreDynamicType(const String& glyph)
{
    static const std::unordered_map<String, DynamicType> dynamicTypes {
        { u"<sym>dynamicPPPPPP</sym>",              DynamicType::PPPPPP },
        { u"<sym>dynamicPPPPP</sym>",               DynamicType::PPPPP },
        { u"<sym>dynamicPPPP</sym>",                DynamicType::PPPP },
        { u"<sym>dynamicPPP</sym>",                 DynamicType::PPP },
        { u"<sym>dynamicPP</sym>",                  DynamicType::PP },
        { u"<sym>dynamicP</sym>",                   DynamicType::P },
        { u"<sym>dynamicMP</sym>",                  DynamicType::MP },
        { u"<sym>dynamicMF</sym>",                  DynamicType::MF },
        { u"<sym>dynamicPF</sym>",                  DynamicType::PF },
        { u"<sym>dynamicF</sym>",                   DynamicType::F },
        { u"<sym>dynamicFF</sym>",                  DynamicType::FF },
        { u"<sym>dynamicFFF</sym>",                 DynamicType::FFF },
        { u"<sym>dynamicFFFF</sym>",                DynamicType::FFFF },
        { u"<sym>dynamicFFFFF</sym>",               DynamicType::FFFFF },
        { u"<sym>dynamicFFFFFF</sym>",              DynamicType::FFFFFF },
        { u"<sym>dynamicFortePiano</sym>",          DynamicType::FP },
        { u"<sym>dynamicForzando</sym>",            DynamicType::FZ },
        { u"<sym>dynamicSforzando1</sym>",          DynamicType::SF },
        { u"<sym>dynamicSforzandoPiano</sym>",      DynamicType::SFP },
        { u"<sym>dynamicSforzandoPianissimo</sym>", DynamicType::SFPP },
        { u"<sym>dynamicSforzato</sym>",            DynamicType::SFZ },
        { u"<sym>dynamicSforzatoPiano</sym>",       DynamicType::SFZ }, // SFZP does not exist
        { u"<sym>dynamicSforzatoFF</sym>",          DynamicType::SFFZ },
        { u"<sym>dynamicRinforzando1</sym>",        DynamicType::RF },
        { u"<sym>dynamicRinforzando2</sym>",        DynamicType::RFZ },
    };
    return muse::value(dynamicTypes, glyph, DynamicType::OTHER);
}

DurationType toMuseScoreDurationType(mnx::NoteValueBase nvb)
{
    static const std::unordered_map<mnx::NoteValueBase, DurationType> duraTypeTable = {
        { mnx::NoteValueBase::Note4096th,   DurationType::V_INVALID },
        { mnx::NoteValueBase::Note2048th,   DurationType::V_INVALID },
        { mnx::NoteValueBase::Note1024th,   DurationType::V_1024TH },
        { mnx::NoteValueBase::Note512th,    DurationType::V_512TH },
        { mnx::NoteValueBase::Note256th,    DurationType::V_256TH },
        { mnx::NoteValueBase::Note128th,    DurationType::V_128TH },
        { mnx::NoteValueBase::Note64th,     DurationType::V_64TH },
        { mnx::NoteValueBase::Note32nd,     DurationType::V_32ND },
        { mnx::NoteValueBase::Note16th,     DurationType::V_16TH },
        { mnx::NoteValueBase::Eighth,       DurationType::V_EIGHTH },
        { mnx::NoteValueBase::Quarter,      DurationType::V_QUARTER },
        { mnx::NoteValueBase::Half,         DurationType::V_HALF },
        { mnx::NoteValueBase::Whole,        DurationType::V_WHOLE },
        { mnx::NoteValueBase::Breve,        DurationType::V_BREVE },
        { mnx::NoteValueBase::Longa,        DurationType::V_LONG },
        { mnx::NoteValueBase::Maxima,       DurationType::V_INVALID },
        { mnx::NoteValueBase::DuplexMaxima, DurationType::V_INVALID },
    };
    return muse::value(duraTypeTable, nvb, DurationType::V_INVALID);
}

TDuration toMuseScoreDuration(mnx::NoteValue nv)
{
    return TDuration(DurationTypeWithDots(toMuseScoreDurationType(nv.base()), nv.dots()));
}

JumpType toMuseScoreJumpType(mnx::JumpType jt)
{
    static const std::unordered_map<mnx::JumpType, JumpType> jumpTable = {
        { mnx::JumpType::DsAlFine,      JumpType::DC_AL_FINE },
        { mnx::JumpType::Segno,         JumpType::DSS },
    };
    return muse::value(jumpTable, jt, JumpType::USER);
}

LyricsSyllabic toMuseScoreLyricsSyllabic(mnx::LyricLineType llt)
{
    using LineType = mnx::LyricLineType;
    static const std::unordered_map<LineType, LyricsSyllabic> lineTypeTable = {
        { LineType::Whole,          LyricsSyllabic::SINGLE },
        { LineType::Start,          LyricsSyllabic::BEGIN },
        { LineType::Middle,         LyricsSyllabic::MIDDLE },
        { LineType::End,            LyricsSyllabic::END },
    };
    return muse::value(lineTypeTable, llt, LyricsSyllabic::SINGLE);
}

OttavaType toMuseScoreOttavaType(mnx::OttavaAmount ottavaAmount)
{
    using OttavaAmount = mnx::OttavaAmount;
    static const std::unordered_map<OttavaAmount, OttavaType> ottavaTypeTable = {
        { OttavaAmount::OctaveDown,         OttavaType::OTTAVA_8VB },
        { OttavaAmount::OctaveUp,           OttavaType::OTTAVA_8VA },
        { OttavaAmount::TwoOctavesDown,     OttavaType::OTTAVA_15MB },
        { OttavaAmount::TwoOctavesUp,       OttavaType::OTTAVA_15MA },
        { OttavaAmount::ThreeOctavesDown,   OttavaType::OTTAVA_22MB },
        { OttavaAmount::ThreeOctavesUp,     OttavaType::OTTAVA_22MA },
    };
    return muse::value(ottavaTypeTable, ottavaAmount, OttavaType::OTTAVA_8VA);
}

Fraction toMuseScoreRTick(const mnx::RhythmicPosition& position)
{
    return toMuseScoreFraction(position.fraction());
}

TremoloType toMuseScoreTremoloType(int numberOfBeams)
{
    static const std::unordered_map<int, TremoloType> tremoloTypeTable = {
        { 1,     TremoloType::C8 },
        { 2,     TremoloType::C16 },
        { 3,     TremoloType::C32 },
        { 4,     TremoloType::C64 },
    };
    return muse::value(tremoloTypeTable, numberOfBeams, TremoloType::INVALID_TREMOLO);
}

SlurStyleType toMuseScoreSlurStyleType(mnx::LineType lineType)
{
    static const std::unordered_map<mnx::LineType, SlurStyleType> slurStyleTable = {
        { mnx::LineType::Dashed,        SlurStyleType::Dashed },
        { mnx::LineType::Dotted,        SlurStyleType::Dotted },
        { mnx::LineType::Solid,         SlurStyleType::Solid },
    };
    return muse::value(slurStyleTable, lineType, SlurStyleType::Solid);
}

TupletBracketType toMuseScoreTupletBracketType(mnx::AutoYesNo bracketOption)
{
    static const std::unordered_map<mnx::AutoYesNo, TupletBracketType> tupletBracketTypeTable = {
        { mnx::AutoYesNo::Auto,     TupletBracketType::AUTO_BRACKET },
        { mnx::AutoYesNo::Yes,      TupletBracketType::SHOW_BRACKET },
        { mnx::AutoYesNo::No,       TupletBracketType::SHOW_NO_BRACKET },
    };
    return muse::value(tupletBracketTypeTable, bracketOption, TupletBracketType::AUTO_BRACKET);
}

TupletNumberType toMuseScoreTupletNumberType(mnx::TupletDisplaySetting numberStyle)
{
    static const std::unordered_map<mnx::TupletDisplaySetting, TupletNumberType> tupletNumberTypeTable = {
        { mnx::TupletDisplaySetting::NoNumber,  TupletNumberType::NO_TEXT },
        { mnx::TupletDisplaySetting::Inner,     TupletNumberType::SHOW_NUMBER },
        { mnx::TupletDisplaySetting::Both,      TupletNumberType::SHOW_RELATION },
    };
    return muse::value(tupletNumberTypeTable, numberStyle, TupletNumberType::SHOW_NUMBER);
}

NoteVal toNoteVal(const mnx::sequence::Pitch::Required& pitch, Key key, int octaveShift)
{
    int step = static_cast<int>(pitch.step);
    int alteration = pitch.alter;
    NoteVal nval;
    nval.pitch = 60 /*middle C*/ + (octaveShift + pitch.octave - 4) * PITCH_DELTA_OCTAVE + step2pitch(step) + alteration;
    if (alteration < int(AccidentalVal::MIN) || alteration > int(AccidentalVal::MAX) || !pitchIsValid(nval.pitch)) {
        nval.pitch = clampPitch(nval.pitch);
        nval.tpc1 = pitch2tpc(nval.pitch, key, Prefer::NEAREST);
    } else {
        nval.tpc1 = step2tpc(step, AccidentalVal(alteration));
    }
    nval.tpc2 = nval.tpc1;
    return nval;
}

ClefType toMuseScoreClefType(const mnx::part::Clef& mnxClef)
{
    using ClefSign = mnx::ClefSign;
    using OttavaAmountOrZero = mnx::OttavaAmountOrZero;

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

Fraction toMuseScoreFraction(const mnx::FractionValue& fraction)
{
    return Fraction(fraction.numerator(), fraction.denominator());
}

Key toMuseScoreKey(int fifths) {
    if (fifths < static_cast<int>(Key::MIN) || fifths > static_cast<int>(Key::MAX)) {
        return Key::INVALID;
    }
    return static_cast<Key>(fifths);
}

NoteType duraTypeToGraceNoteType(DurationType type, bool useLeft)
{
    if (int(type) < int(DurationType::V_EIGHTH)) {
        return useLeft ? NoteType::GRACE8_AFTER : NoteType::GRACE4;
    }
    if (int(type) >= int(DurationType::V_32ND)) {
        return useLeft ? NoteType::GRACE32_AFTER : NoteType::GRACE32;
    }
    if (type == DurationType::V_16TH) {
        return useLeft ? NoteType::GRACE16_AFTER : NoteType::GRACE16;
    }
    return useLeft ? NoteType::GRACE8_AFTER : NoteType::APPOGGIATURA;
}

} // namespace mu::iex::mnxio
