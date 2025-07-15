/*
 * SPDX-License-Identifier: GPL-3.0-only
 * MuseScore-Studio-CLA-applies
 *
 * MuseScore Studio
 * Music Composition & Notation
 *
 * Copyright (C) 2025 MuseScore Limited
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
#include "dom/score.h"
#include "dom/mscore.h"
#include "dom/textbase.h"
#include "dom/mmrestrange.h"
#include "types/types.h"
#include "internal/importfinalelogger.h"
#include "internal/importfinaleparser.h"
#include "internal/finaletypesconv.h"
#include "musx/musx.h"

using namespace mu::engraving;
using namespace musx::dom;

namespace mu::iex::finale {

static const std::set<std::string_view> museScoreSMuFLFonts{
    "Bravura",
    "Leland",
    "Emmentaler",
    "Gonville",
    "MuseJazz",
    "Petaluma",
    "Finale Maestro",
    "Finale Broadway"
};

template <typename T>
static std::shared_ptr<T> getDocOptions(const FinaleParser& context, const std::string& prefsName)
{
    auto result = context.musxDocument()->getOptions()->get<T>();
    if (!result) {
        throw std::invalid_argument("document contains no default " + prefsName);
    }
    return result;
}

void FinaleOptions::init(const FinaleParser& context)
{
    auto fontOptions = getDocOptions<options::FontOptions>(context, "font");
    defaultMusicFont = fontOptions->getFontInfo(options::FontOptions::FontType::Music);
    //
    accidentalOptions = getDocOptions<options::AccidentalOptions>(context, "accidental");
    alternateNotationOptions = getDocOptions<options::AlternateNotationOptions>(context, "alternate notation");
    augDotOptions = getDocOptions<options::AugmentationDotOptions>(context, "augmentation dot");
    barlineOptions = getDocOptions<options::BarlineOptions>(context, "barline");
    beamOptions = getDocOptions<options::BeamOptions>(context, "beam");
    clefOptions = getDocOptions<options::ClefOptions>(context, "clef");
    flagOptions = getDocOptions<options::FlagOptions>(context, "flag");
    graceOptions = getDocOptions<options::GraceNoteOptions>(context, "grace note");
    keyOptions = getDocOptions<options::KeySignatureOptions>(context, "key signature");
    lineCurveOptions = getDocOptions<options::LineCurveOptions>(context, "lines & curves");
    miscOptions = getDocOptions<options::MiscOptions>(context, "miscellaneous");
    mmRestOptions = getDocOptions<options::MultimeasureRestOptions>(context, "multimeasure rest");
    musicSpacing = getDocOptions<options::MusicSpacingOptions>(context, "music spacing");
    auto pageFormatOptions = getDocOptions<options::PageFormatOptions>(context, "page format");
    pageFormat = pageFormatOptions->calcPageFormatForPart(context.currentMusxPartId());
    braceOptions = getDocOptions<options::PianoBraceBracketOptions>(context, "piano braces & brackets");
    repeatOptions = getDocOptions<options::RepeatOptions>(context, "repeat");
    smartShapeOptions = getDocOptions<options::SmartShapeOptions>(context, "smart shape");
    staffOptions = getDocOptions<options::StaffOptions>(context, "staff");
    stemOptions = getDocOptions<options::StemOptions>(context, "stem");
    tieOptions = getDocOptions<options::TieOptions>(context, "tie");
    timeOptions = getDocOptions<options::TimeSignatureOptions>(context, "time signature");
    tupletOptions = getDocOptions<options::TupletOptions>(context, "tuplet");
    //
    layerOneAttributes = context.musxDocument()->getOthers()->get<others::LayerAttributes>(context.currentMusxPartId(), 0);
    if (!layerOneAttributes) {
        throw std::invalid_argument("document contains no options for Layer 1");
    }
    auto measNumRegions = context.musxDocument()->getOthers()->getArray<others::MeasureNumberRegion>(context.currentMusxPartId());
    if (measNumRegions.size() > 0) {
        measNumScorePart = (context.currentMusxPartId() && measNumRegions[0]->useScoreInfoForPart && measNumRegions[0]->partData)
        ? measNumRegions[0]->partData
        : measNumRegions[0]->scoreData;
        if (!measNumScorePart) {
            throw std::invalid_argument("document contains no ScorePartData for measure number region " + std::to_string(measNumRegions[0]->getCmper()));
        }
    }
    partGlobals = context.musxDocument()->getOthers()->get<others::PartGlobals>(context.currentMusxPartId(), MUSX_GLOBALS_CMPER);
    if (!layerOneAttributes) {
        throw std::invalid_argument("document contains no options for Layer 1");
    }
    combinedDefaultStaffScaling = pageFormat->calcCombinedSystemScaling();
}

static uint16_t museFontEfx(const FontInfo* fontInfo)
{
    uint16_t retval = 0;

    if (fontInfo->bold) { retval |= 0x01; }
    if (fontInfo->italic) { retval |= 0x02; }
    if (fontInfo->underline) { retval |= 0x04; }
    if (fontInfo->strikeout) { retval |= 0x08; }

    return retval;
}

static double museMagVal(const FinaleParser& context, const options::FontOptions::FontType type)
{
    auto fontPrefs = options::FontOptions::getFontInfo(context.musxDocument(), type);
    if (fontPrefs->getName() == context.musxOptions().defaultMusicFont->getName()) {
        return double(fontPrefs->fontSize) / double(context.musxOptions().defaultMusicFont->fontSize);
    }
    return 1.0;
}

static Sid styleIdx(const std::string& name)
{
    auto nameStr = String::fromStdString(name);
    return MStyle::styleIdx(nameStr);
}

static void writeEvpuSpace(MStyle& style, Sid sid, Evpu evpu)
{
    style.set(sid, FinaleTConv::doubleFromEvpu(evpu));
}

static void writeEfixSpace(MStyle& style, Sid sid, Efix efix)
{
    style.set(sid, FinaleTConv::doubleFromEfix(efix));
}

static void writeFontPref(MStyle& style, const std::string& namePrefix, const std::shared_ptr<FontInfo>& fontInfo)
{
    style.set(styleIdx(namePrefix + "FontFace"), String::fromStdString(fontInfo->getName()));
    style.set(styleIdx(namePrefix + "FontSize"), FinaleTConv::spatiumScaledFontSize(fontInfo));
    style.set(styleIdx(namePrefix + "FontSpatiumDependent"), !fontInfo->absolute);
    style.set(styleIdx(namePrefix + "FontStyle"), museFontEfx(fontInfo.get()));
}

static void writeDefaultFontPref(MStyle& style, const FinaleParser& context, const std::string& namePrefix, options::FontOptions::FontType type)
{
    if (auto fontPrefs = options::FontOptions::getFontInfo(context.musxDocument(), type)) {
        writeFontPref(style, namePrefix, fontPrefs);
    } else {
        context.logger()->logWarning(String::fromStdString("unable to load default font info for type " + std::to_string(int(type))));
    }
}

void writeLinePrefs(MStyle& style,
                    const std::string& namePrefix,
                    double widthEfix,
                    double dashLength,
                    double dashGap,
                    const std::optional<LineType>& lineStyle = std::nullopt)
{
    const double lineWidthEvpu = widthEfix / EFIX_PER_EVPU;
    style.set(styleIdx(namePrefix + "LineWidth"), widthEfix / EFIX_PER_SPACE);
    if (lineStyle) {
        style.set(styleIdx(namePrefix + "LineStyle"), lineStyle.value());
    }
    style.set(styleIdx(namePrefix + "DashLineLen"), dashLength / lineWidthEvpu);
    style.set(styleIdx(namePrefix + "DashGapLen"), dashGap / lineWidthEvpu);
}

static void writeFramePrefs(MStyle& style, const std::string& namePrefix, const others::Enclosure* enclosure = nullptr)
{
    if (!enclosure || enclosure->shape == others::Enclosure::Shape::NoEnclosure || enclosure->lineWidth == 0) {
        style.set(styleIdx(namePrefix + "FrameType"), int(FrameType::NO_FRAME));
        if (!enclosure) return; // Do not override any other defaults if no enclosure
    } else if (enclosure->shape == others::Enclosure::Shape::Ellipse) {
        style.set(styleIdx(namePrefix + "FrameType"), int(FrameType::CIRCLE));
    } else {
        style.set(styleIdx(namePrefix + "FrameType"), int(FrameType::SQUARE));
    }
    writeEvpuSpace(style, styleIdx(namePrefix + "FramePadding"), enclosure->xMargin);
    writeEfixSpace(style, styleIdx(namePrefix + "FrameWidth"), enclosure->lineWidth);
    style.set(styleIdx(namePrefix + "FrameRound"),
              enclosure->roundCorners ? int(lround(enclosure->cornerRadius / EFIX_PER_EVPU)) : 0);
}

static void writeCategoryTextFontPref(MStyle& style, const FinaleParser& context, const std::string& namePrefix, others::MarkingCategory::CategoryType categoryType)
{
    auto cat = context.musxDocument()->getOthers()->get<others::MarkingCategory>(context.currentMusxPartId(), Cmper(categoryType));
    if (!cat) {
        context.logger()->logWarning(String::fromStdString("unable to load category def for " + namePrefix));
        return;
    }
    if (!cat->textFont) {
        context.logger()->logWarning(String::fromStdString("marking category " + cat->getName() + " has no text font."));
        return;
    }
    writeFontPref(style, namePrefix, cat->textFont);
    for (auto& it : cat->textExpressions) {
        if (auto exp = it.second.lock()) {
            writeFramePrefs(style, namePrefix, exp->getEnclosure().get());
            break;
        } else {
            context.logger()->logWarning(String::fromStdString("marking category " + cat->getName() + " has invalid text expression."));
        }
    }
}

static void writePagePrefs(MStyle& style, const FinaleParser& context)
{
    const auto& prefs = context.musxOptions();
    const auto& pagePrefs = prefs.pageFormat;

    style.set(Sid::pageWidth, double(pagePrefs->pageWidth) / EVPU_PER_INCH);
    style.set(Sid::pageHeight, double(pagePrefs->pageHeight) / EVPU_PER_INCH);
    style.set(Sid::pagePrintableWidth,
               double(pagePrefs->pageWidth - pagePrefs->leftPageMarginLeft + pagePrefs->leftPageMarginRight) / EVPU_PER_INCH);
    style.set(Sid::pageEvenLeftMargin, pagePrefs->leftPageMarginLeft / EVPU_PER_INCH);
    style.set(Sid::pageOddLeftMargin,
               double(pagePrefs->facingPages ? pagePrefs->rightPageMarginLeft : pagePrefs->leftPageMarginLeft) / EVPU_PER_INCH);
    style.set(Sid::pageEvenTopMargin, double(-pagePrefs->leftPageMarginTop) / EVPU_PER_INCH);
    style.set(Sid::pageEvenBottomMargin, double(pagePrefs->leftPageMarginBottom) / EVPU_PER_INCH);
    style.set(Sid::pageOddTopMargin,
               double(pagePrefs->facingPages ? -pagePrefs->rightPageMarginTop : -pagePrefs->leftPageMarginTop) / EVPU_PER_INCH);
    style.set(Sid::pageOddBottomMargin,
               double(pagePrefs->facingPages ? pagePrefs->rightPageMarginBottom : pagePrefs->leftPageMarginBottom) / EVPU_PER_INCH);
    style.set(Sid::pageTwosided, pagePrefs->facingPages);
    style.set(Sid::enableIndentationOnFirstSystem, pagePrefs->differentFirstSysMargin);
    writeEvpuSpace(style, Sid::firstSystemIndentationValue, pagePrefs->firstSysMarginLeft);

    // Calculate Spatium
    style.set(Sid::spatium, ((EVPU_PER_SPACE * prefs.combinedDefaultStaffScaling.toDouble()) / EVPU_PER_MM) * DPMM);

    // Calculate small staff size and small note size from first system, if any is there
    if (const auto& firstSystem = context.musxDocument()->getOthers()->get<others::StaffSystem>(context.currentMusxPartId(), 1)) {
        auto [minSize, maxSize] = firstSystem->calcMinMaxStaffSizes();
        if (minSize < 1) {
            style.set(Sid::smallStaffMag, minSize.toDouble());
            style.set(Sid::smallNoteMag, minSize.toDouble());
        }
    }

    // Default music font
    const auto& defaultMusicFont = context.musxOptions().defaultMusicFont;
    const bool isSMuFL = [defaultMusicFont]() -> bool {
        if (defaultMusicFont->calcIsSMuFL())
            return true;
        auto it = museScoreSMuFLFonts.find(defaultMusicFont->getName());
        return it != museScoreSMuFLFonts.end();
    }();
    const String musicFontName = [&]() {
        if (isSMuFL) {
            return String::fromStdString(defaultMusicFont->getName());
        } else if (defaultMusicFont->getName() == "Maestro") {
            return String("Finale Maestro");
        } // other `else if` checks as required go here
        return String();
    }();
    if (!musicFontName.empty()) {
        style.set(Sid::musicalSymbolFont, musicFontName);
        style.set(Sid::musicalTextFont, musicFontName + " Text");
    }
}

static void writeLyricsPrefs(MStyle& style, const FinaleParser& context)
{
    auto fontInfo = options::FontOptions::getFontInfo(context.musxDocument(), options::FontOptions::FontType::LyricVerse);
    for (auto [verseNumber, evenOdd] : {
             std::make_pair(1, "Odd"),
             std::make_pair(2, "Even")
         }) {
        auto verseText = context.musxDocument()->getTexts()->get<texts::LyricsVerse>(Cmper(verseNumber));
        if (verseText && !verseText->text.empty()) {
            auto font = verseText->getRawTextCtx(context.currentMusxPartId()).parseFirstFontInfo();
            if (font) {
                fontInfo = font;
            }
        }
        writeFontPref(style, "lyrics" + std::string(evenOdd), fontInfo);
    }
}

void writeLineMeasurePrefs(MStyle& style, const FinaleParser& context)
{
    using RepeatWingStyle = options::RepeatOptions::WingStyle;
    const auto& prefs = context.musxOptions();

    writeEfixSpace(style, Sid::barWidth, prefs.barlineOptions->barlineWidth);
    writeEfixSpace(style, Sid::doubleBarWidth, prefs.barlineOptions->barlineWidth);
    writeEfixSpace(style, Sid::endBarWidth, prefs.barlineOptions->thickBarlineWidth);

    // these calculations are based on observed behavior
    writeEfixSpace(style, Sid::doubleBarDistance,
                   prefs.barlineOptions->doubleBarlineSpace - prefs.barlineOptions->barlineWidth);
    writeEfixSpace(style, Sid::endBarDistance, prefs.barlineOptions->finalBarlineSpace);
    writeEvpuSpace(style, Sid::repeatBarlineDotSeparation, prefs.repeatOptions->forwardDotHPos);
    style.set(Sid::repeatBarTips, prefs.repeatOptions->wingStyle != RepeatWingStyle::None);

    style.set(Sid::startBarlineSingle, prefs.barlineOptions->drawLeftBarlineSingleStaff);
    style.set(Sid::startBarlineMultiple, prefs.barlineOptions->drawLeftBarlineMultipleStaves);

    style.set(Sid::bracketWidth, 0.5); // Hard-coded in Finale
    writeEvpuSpace(style, Sid::bracketDistance, -(prefs.braceOptions->defBracketPos) - 0.25 * EVPU_PER_SPACE); // Finale subtracts half the bracket width on layout (observed).
    writeEvpuSpace(style, Sid::akkoladeBarDistance, -prefs.braceOptions->defBracketPos);

    writeEvpuSpace(style, Sid::clefLeftMargin, prefs.clefOptions->clefFrontSepar);
    writeEvpuSpace(style, Sid::keysigLeftMargin, prefs.keyOptions->keyFront);

    const double timeSigSpaceBefore = context.currentMusxPartId()
                                          ? prefs.timeOptions->timeFrontParts
                                          : prefs.timeOptions->timeFront;
    writeEvpuSpace(style, Sid::timesigLeftMargin, timeSigSpaceBefore);

    writeEvpuSpace(style, Sid::clefKeyDistance,
               (prefs.clefOptions->clefBackSepar + prefs.clefOptions->clefKeySepar + prefs.keyOptions->keyFront));
    writeEvpuSpace(style, Sid::clefTimesigDistance,
               (prefs.clefOptions->clefBackSepar + prefs.clefOptions->clefTimeSepar + timeSigSpaceBefore));
    writeEvpuSpace(style, Sid::keyTimesigDistance,
               (prefs.keyOptions->keyBack + prefs.keyOptions->keyTimeSepar + timeSigSpaceBefore));
    writeEvpuSpace(style, Sid::keyBarlineDistance, prefs.repeatOptions->afterKeySpace);

    // Skipped: systemHeaderDistance, systemHeaderTimeSigDistance: these do not translate well from Finale

    writeEvpuSpace(style, Sid::clefBarlineDistance, -prefs.clefOptions->clefChangeOffset);
    writeEvpuSpace(style, Sid::timesigBarlineDistance, prefs.repeatOptions->afterClefSpace);

    writeEvpuSpace(style, Sid::measureRepeatNumberPos, -(prefs.alternateNotationOptions->twoMeasNumLift + 0.5));
    writeEfixSpace(style, Sid::staffLineWidth, prefs.lineCurveOptions->staffLineWidth);
    writeEfixSpace(style, Sid::ledgerLineWidth, prefs.lineCurveOptions->legerLineWidth);
    writeEvpuSpace(style, Sid::ledgerLineLength,
               (prefs.lineCurveOptions->legerFrontLength + prefs.lineCurveOptions->legerBackLength) / 2);
    writeEvpuSpace(style, Sid::keysigAccidentalDistance, (prefs.keyOptions->acciAdd + 4));  // Observed fudge factor
    writeEvpuSpace(style, Sid::keysigNaturalDistance, (prefs.keyOptions->acciAdd + 6));     // Observed fudge factor

    style.set(Sid::smallClefMag, FinaleTConv::doubleFromPercent(prefs.clefOptions->clefChangePercent));
    style.set(Sid::genClef, !prefs.clefOptions->showClefFirstSystemOnly);
    style.set(Sid::genKeysig, !prefs.keyOptions->showKeyFirstSystemOnly);
    style.set(Sid::genCourtesyTimesig, prefs.timeOptions->cautionaryTimeChanges);
    style.set(Sid::genCourtesyKeysig, prefs.keyOptions->cautionaryKeyChanges);
    style.set(Sid::genCourtesyClef, prefs.clefOptions->cautionaryClefChanges);

    style.set(Sid::keySigCourtesyBarlineMode,
              int(FinaleTConv::boolToCourtesyBarlineMode(prefs.barlineOptions->drawDoubleBarlineBeforeKeyChanges)));
    style.set(Sid::timeSigCourtesyBarlineMode, int(CourtesyBarlineMode::ALWAYS_SINGLE));  // Hard-coded as 0 in Finale
    style.set(Sid::hideEmptyStaves, context.currentMusxPartId() == SCORE_PARTID);
}

void writeStemPrefs(MStyle& style, const FinaleParser& context)
{
    const auto& prefs = context.musxOptions();

    style.set(Sid::useStraightNoteFlags, prefs.flagOptions->straightFlags);
    writeEfixSpace(style, Sid::stemWidth, prefs.stemOptions->stemWidth);
    style.set(Sid::shortenStem, true);
    writeEvpuSpace(style, Sid::stemLength, prefs.stemOptions->stemLength);
    writeEvpuSpace(style, Sid::shortestStem, prefs.stemOptions->shortStemLength);
    writeEfixSpace(style, Sid::stemSlashThickness, prefs.graceOptions->graceSlashWidth);
}

void writeMusicSpacingPrefs(MStyle& style, const FinaleParser& context)
{
    const auto& prefs = context.musxOptions();

    writeEvpuSpace(style, Sid::minMeasureWidth, prefs.musicSpacing->minWidth);
    writeEvpuSpace(style, Sid::minNoteDistance, prefs.musicSpacing->minDistance);
    style.set(Sid::measureSpacing, prefs.musicSpacing->scalingFactor);
    /// @todo find a conversion for note distance to tie length.
    writeEvpuSpace(style, Sid::minTieLength, prefs.musicSpacing->minDistTiedNotes);
}

void writeNoteRelatedPrefs(MStyle& style, const FinaleParser& context)
{
    const auto& prefs = context.musxOptions();

    writeEvpuSpace(style, Sid::accidentalDistance, prefs.accidentalOptions->acciAcciSpace);
    writeEvpuSpace(style, Sid::accidentalNoteDistance, prefs.accidentalOptions->acciNoteSpace);
    writeEfixSpace(style, Sid::beamWidth, prefs.beamOptions->beamWidth);
    style.set(Sid::useWideBeams, prefs.beamOptions->beamSepar > (0.75 * EVPU_PER_SPACE));

    // Finale randomly adds twice the stem width to the length of a beam stub. (Observed behavior)
    writeEvpuSpace(style, Sid::beamMinLen,
              (prefs.beamOptions->beamStubLength + (2.0 * prefs.stemOptions->stemWidth / EFIX_PER_EVPU)));

    style.set(Sid::beamNoSlope, prefs.beamOptions->beamingStyle == options::BeamOptions::FlattenStyle::AlwaysFlat);
    style.set(Sid::dotMag, museMagVal(context, options::FontOptions::FontType::AugDots));
    writeEvpuSpace(style, Sid::dotNoteDistance, prefs.augDotOptions->dotNoteOffset);
    writeEvpuSpace(style, Sid::dotRestDistance, prefs.augDotOptions->dotNoteOffset); // Same value as dotNoteDistance
    /// @todo Finale's value is calculated relative to the rightmost point of the previous dot, MuseScore the leftmost.(Observed behavior)
    /// We need to add on the symbol width of one dot for the correct value.
    writeEvpuSpace(style, Sid::dotDotDistance, prefs.augDotOptions->dotOffset);
    style.set(Sid::articulationMag, museMagVal(context, options::FontOptions::FontType::Articulation));
    style.set(Sid::graceNoteMag, FinaleTConv::doubleFromPercent(prefs.graceOptions->gracePerc));
    style.set(Sid::concertPitch, !prefs.partGlobals->showTransposed);
    style.set(Sid::multiVoiceRestTwoSpaceOffset, std::labs(prefs.layerOneAttributes->restOffset) >= 4);
    style.set(Sid::mergeMatchingRests, prefs.miscOptions->consolidateRestsAcrossLayers);
}

void writeSmartShapePrefs(MStyle& style, const FinaleParser& context)
{
    const auto& prefs = context.musxOptions();

    // Hairpin-related settings
    writeEvpuSpace(style, Sid::hairpinHeight, prefs.smartShapeOptions->shortHairpinOpeningWidth);
    style.set(Sid::hairpinContHeight, 0.5); // Hardcoded to a half space
    writeCategoryTextFontPref(style, context, "hairpin", others::MarkingCategory::CategoryType::Dynamics);
    writeLinePrefs(style, "hairpin",
                   prefs.smartShapeOptions->crescLineWidth,
                   prefs.smartShapeOptions->smartDashOn,
                   prefs.smartShapeOptions->smartDashOff);

    // Slur-related settings
    writeEvpuSpace(style, Sid::slurEndWidth, prefs.smartShapeOptions->smartSlurTipWidth);
    writeEfixSpace(style, Sid::slurDottedWidth, prefs.smartShapeOptions->smartLineWidth);

    // Tie-related settings
    writeEvpuSpace(style, Sid::tieEndWidth, prefs.tieOptions->tieTipWidth);
    writeEfixSpace(style, Sid::tieDottedWidth, prefs.smartShapeOptions->smartLineWidth);
    style.set(Sid::tiePlacementSingleNote, prefs.tieOptions->useOuterPlacement ? TiePlacement::OUTSIDE : TiePlacement::INSIDE);
	// Note: Finale's 'outer placement' for notes within chords is much closer to inside placement. But outside placement is closer overall.
    style.set(Sid::tiePlacementChord, prefs.tieOptions->useOuterPlacement ? TiePlacement::OUTSIDE : TiePlacement::INSIDE);

    // Ottava settings
    writeEvpuSpace(style, Sid::ottavaHookAbove, prefs.smartShapeOptions->hookLength);
    writeEvpuSpace(style, Sid::ottavaHookBelow, -prefs.smartShapeOptions->hookLength);
    writeLinePrefs(style, "ottava",
                   prefs.smartShapeOptions->smartLineWidth,
                   prefs.smartShapeOptions->smartDashOn,
                   prefs.smartShapeOptions->smartDashOff,
                   LineType::DASHED);
    style.set(Sid::ottavaNumbersOnly, prefs.smartShapeOptions->showOctavaAsText);
}

void writeMeasureNumberPrefs(MStyle& style, const FinaleParser& context)
{
    using MeasureNumberRegion = others::MeasureNumberRegion;
    const auto& prefs = context.musxOptions();

    style.set(Sid::showMeasureNumber, prefs.measNumScorePart != nullptr);

    if (prefs.measNumScorePart) {
        const auto& scorePart = prefs.measNumScorePart;
        style.set(Sid::showMeasureNumberOne, !scorePart->hideFirstMeasure);
        style.set(Sid::measureNumberInterval, scorePart->incidence);
        style.set(Sid::measureNumberSystem, scorePart->showOnStart && !scorePart->showOnEvery);

        auto justificationAlign = [](MeasureNumberRegion::AlignJustify justi) -> Align {
            switch (justi) {
            default:
            case MeasureNumberRegion::AlignJustify::Left: return Align(AlignH::LEFT, AlignV::BASELINE);
            case MeasureNumberRegion::AlignJustify::Center: return Align(AlignH::HCENTER, AlignV::BASELINE);
            case MeasureNumberRegion::AlignJustify::Right: return Align(AlignH::RIGHT, AlignV::BASELINE);
            }
        };

        auto horizontalAlignment = [](MeasureNumberRegion::AlignJustify align) -> PlacementH {
            switch (align) {
            default:
            case MeasureNumberRegion::AlignJustify::Left: return PlacementH::LEFT;
            case MeasureNumberRegion::AlignJustify::Center: return PlacementH::CENTER;
            case MeasureNumberRegion::AlignJustify::Right: return PlacementH::RIGHT;
            }
        };

        auto verticalAlignment = [](Evpu vertical) -> PlacementV {
            return (vertical >= 0) ? PlacementV::ABOVE : PlacementV::BELOW;
        };

        auto processSegment = [&](const std::shared_ptr<FontInfo>& fontInfo,
                                  const std::shared_ptr<others::Enclosure>& enclosure,
                                  bool useEnclosure,
                                  MeasureNumberRegion::AlignJustify justification,
                                  MeasureNumberRegion::AlignJustify alignment,
                                  Evpu vertical,
                                  const std::string& prefix)
        {
            writeFontPref(style, prefix, fontInfo);
            style.set(styleIdx(prefix + "VPlacement"), verticalAlignment(vertical));
            style.set(styleIdx(prefix + "HPlacement"), horizontalAlignment(alignment));
            style.set(styleIdx(prefix + "Align"), justificationAlign(justification));
            writeFramePrefs(style, prefix, useEnclosure ? enclosure.get() : nullptr);
        };

        // Determine source for primary segment
        auto fontInfo       = scorePart->showOnStart ? scorePart->startFont         : scorePart->multipleFont;
        auto enclosure      = scorePart->showOnStart ? scorePart->startEnclosure    : scorePart->multipleEnclosure;
        auto useEnclosure   = scorePart->showOnStart ? scorePart->useStartEncl      : scorePart->useMultipleEncl;
        auto justification  = scorePart->showOnEvery ? scorePart->multipleJustify   : scorePart->startJustify;
        auto alignment      = scorePart->showOnEvery ? scorePart->multipleAlign     : scorePart->startAlign;
        auto vertical       = scorePart->showOnStart ? scorePart->startYdisp        : scorePart->multipleYdisp;

        style.set(Sid::measureNumberOffsetType, int(OffsetType::SPATIUM)); // Hardcoded offset type
        processSegment(fontInfo, enclosure, useEnclosure, justification, alignment, vertical, "measureNumber");

        style.set(Sid::mmRestShowMeasureNumberRange, scorePart->showMmRange);
        if (scorePart->leftMmBracketChar == 0) {
            style.set(Sid::mmRestRangeBracketType, int(MMRestRangeBracketType::NONE));
        } else if (scorePart->leftMmBracketChar == '(') {
            style.set(Sid::mmRestRangeBracketType, int(MMRestRangeBracketType::PARENTHESES));
        } else {
            style.set(Sid::mmRestRangeBracketType, int(MMRestRangeBracketType::BRACKETS));
        }

        processSegment(scorePart->mmRestFont, scorePart->multipleEnclosure, scorePart->useMultipleEncl,
                       scorePart->mmRestJustify, scorePart->mmRestAlign, scorePart->mmRestYdisp, "mmRestRange");
    }

    style.set(Sid::createMultiMeasureRests, context.currentMusxPartId() != 0);
    style.set(Sid::minEmptyMeasures, prefs.mmRestOptions->numStart);
    writeEvpuSpace(style, Sid::minMMRestWidth, prefs.mmRestOptions->measWidth);
    style.set(Sid::mmRestNumberPos, (prefs.mmRestOptions->numAdjY / EVPU_PER_SPACE) + 1);
    style.set(Sid::oldStyleMultiMeasureRests,
              prefs.mmRestOptions->useSymbols && prefs.mmRestOptions->useSymsThreshold > 1);
    style.set(Sid::mmRestOldStyleMaxMeasures,
              std::max(prefs.mmRestOptions->useSymsThreshold - 1, 0));
    writeEvpuSpace(style, Sid::mmRestOldStyleSpacing, prefs.mmRestOptions->symSpacing);
}

void writeRepeatEndingPrefs(MStyle& style, const FinaleParser& context)
{
    const auto& prefs = context.musxOptions();

    writeEfixSpace(style, Sid::voltaLineWidth, prefs.repeatOptions->bracketLineWidth);
    style.set(Sid::voltaLineStyle, LineType::SOLID);
    writeDefaultFontPref(style, context, "volta", options::FontOptions::FontType::Ending);
    style.set(Sid::voltaAlign, Align(AlignH::LEFT, AlignV::BASELINE));
}

void writeTupletPrefs(MStyle& style, const FinaleParser& context)
{
    using TupletOptions = options::TupletOptions;
    const auto& prefs = context.musxOptions();
    const auto& tupletOptions = prefs.tupletOptions;

    style.set(Sid::tupletOutOfStaff, tupletOptions->avoidStaff);
    writeEvpuSpace(style, Sid::tupletStemLeftDistance, tupletOptions->leftHookExt);
    writeEvpuSpace(style, Sid::tupletStemRightDistance, tupletOptions->rightHookExt);
    writeEvpuSpace(style, Sid::tupletNoteLeftDistance, tupletOptions->leftHookExt);
    writeEvpuSpace(style, Sid::tupletNoteRightDistance, tupletOptions->rightHookExt);
    writeEfixSpace(style, Sid::tupletBracketWidth, tupletOptions->tupLineWidth);

    switch (tupletOptions->posStyle) {
    case TupletOptions::PositioningStyle::Above:
        style.set(Sid::tupletDirection, DirectionV::UP);
        break;
    case TupletOptions::PositioningStyle::Below:
        style.set(Sid::tupletDirection, DirectionV::DOWN);
        break;
    default:
        style.set(Sid::tupletDirection, DirectionV::AUTO);
        break;
    }

    style.set(Sid::tupletNumberType, int(FinaleTConv::toMuseScoreTupletNumberType(tupletOptions->numStyle)));

    if (tupletOptions->brackStyle == TupletOptions::BracketStyle::Nothing) {
        style.set(Sid::tupletBracketType, int(TupletBracketType::SHOW_NO_BRACKET));
    } else if (tupletOptions->autoBracketStyle == TupletOptions::AutoBracketStyle::Always) {
        style.set(Sid::tupletBracketType, int(TupletBracketType::SHOW_BRACKET));
    } else {
        style.set(Sid::tupletBracketType, int(TupletBracketType::AUTO_BRACKET));
    }

    const auto& fontInfo = options::FontOptions::getFontInfo(context.musxDocument(), options::FontOptions::FontType::Tuplet);
    if (!fontInfo) {
        throw std::invalid_argument("Unable to load font pref for tuplets");
    }

    if (fontInfo->calcIsSMuFL()) {
        style.set(Sid::tupletMusicalSymbolsScale, museMagVal(context, options::FontOptions::FontType::Tuplet));
        style.set(Sid::tupletUseSymbols, true);
    } else {
        writeFontPref(style, "tuplet", fontInfo);
        style.set(Sid::tupletMusicalSymbolsScale, 1.0);
        style.set(Sid::tupletUseSymbols, false);
    }

    writeEvpuSpace(style, Sid::tupletBracketHookHeight,
                (std::max)(-tupletOptions->leftHookLen, -tupletOptions->rightHookLen));
}

void writeMarkingPrefs(MStyle& style, const FinaleParser& context)
{
    using FontType = options::FontOptions::FontType;
    using CategoryType = others::MarkingCategory::CategoryType;
    const auto& prefs = context.musxOptions();

    auto cat = context.musxDocument()->getOthers()->get<others::MarkingCategory>(context.currentMusxPartId(), Cmper(CategoryType::Dynamics));
    if (!cat) {
        throw std::invalid_argument("unable to find MarkingCategory for dynamics");
    }
    auto catFontInfo = cat->musicFont;
    bool override = catFontInfo && catFontInfo->calcIsSMuFL() && catFontInfo->fontId != 0;
    style.set(Sid::dynamicsOverrideFont, override);
    if (override) {
        style.set(Sid::dynamicsFont, String::fromStdString(catFontInfo->getName()));
        style.set(Sid::dynamicsSize, catFontInfo->fontSize / prefs.defaultMusicFont->fontSize);
    } else {
        style.set(Sid::dynamicsFont, String::fromStdString(prefs.defaultMusicFont->getName()));
        style.set(Sid::dynamicsSize,
                  catFontInfo->calcIsSMuFL() ? (catFontInfo->fontSize / prefs.defaultMusicFont->fontSize) : 1.0);
    }

    auto textBlockFont = options::FontOptions::getFontInfo(context.musxDocument(), FontType::TextBlock);
    if (!textBlockFont) {
        throw std::invalid_argument("unable to find font prefs for Text Blocks");
    }
    writeFontPref(style, "default", textBlockFont);
    style.set(Sid::titleFontFace, String::fromStdString(textBlockFont->getName()));
    style.set(Sid::subTitleFontFace, String::fromStdString(textBlockFont->getName()));
    style.set(Sid::composerFontFace, String::fromStdString(textBlockFont->getName()));
    style.set(Sid::lyricistFontFace, String::fromStdString(textBlockFont->getName()));

    writeDefaultFontPref(style, context, "longInstrument", FontType::StaffNames);
    const auto fullPosition = prefs.staffOptions->namePos;
    if (!fullPosition) {
        throw std::invalid_argument("unable to find default full name positioning for staves");
    }
    style.set(Sid::longInstrumentAlign, FinaleTConv::justifyToAlignment(fullPosition->justify));

    writeDefaultFontPref(style, context, "shortInstrument", FontType::AbbrvStaffNames);
    const auto abbreviatedPosition = prefs.staffOptions->namePosAbbrv;
    if (!abbreviatedPosition) {
        throw std::invalid_argument("unable to find default abbreviated name positioning for staves");
    }
    style.set(Sid::shortInstrumentAlign, FinaleTConv::justifyToAlignment(abbreviatedPosition->justify));

    writeDefaultFontPref(style, context, "partInstrument", FontType::StaffNames);
    writeCategoryTextFontPref(style, context, "dynamics", CategoryType::Dynamics);
    writeCategoryTextFontPref(style, context, "expression", CategoryType::ExpressiveText);
    writeCategoryTextFontPref(style, context, "tempo", CategoryType::TempoMarks);
    writeCategoryTextFontPref(style, context, "tempoChange", CategoryType::ExpressiveText);
    writeLinePrefs(style, "tempoChange",
                   prefs.smartShapeOptions->smartLineWidth,
                   prefs.smartShapeOptions->smartDashOn,
                   prefs.smartShapeOptions->smartDashOff,
                   LineType::DASHED);
    writeCategoryTextFontPref(style, context, "metronome", CategoryType::TempoMarks);
    style.set(Sid::translatorFontFace, String::fromStdString(textBlockFont->getName()));
    writeCategoryTextFontPref(style, context, "systemText", CategoryType::ExpressiveText);
    writeCategoryTextFontPref(style, context, "staffText", CategoryType::TechniqueText);
    writeCategoryTextFontPref(style, context, "rehearsalMark", CategoryType::RehearsalMarks);
    writeDefaultFontPref(style, context, "repeatLeft", FontType::Repeat);
    writeDefaultFontPref(style, context, "repeatRight", FontType::Repeat);
    writeFontPref(style, "frame", textBlockFont);
    writeCategoryTextFontPref(style, context, "textLine", CategoryType::TechniqueText);
    writeCategoryTextFontPref(style, context, "systemTextLine", CategoryType::ExpressiveText);
    writeCategoryTextFontPref(style, context, "glissando", CategoryType::TechniqueText);
    writeCategoryTextFontPref(style, context, "bend", CategoryType::TechniqueText);
    writeFontPref(style, "header", textBlockFont);
    writeFontPref(style, "footer", textBlockFont);
    writeFontPref(style, "copyright", textBlockFont);
    writeFontPref(style, "pageNumber", textBlockFont);
    writeFontPref(style, "instrumentChange", textBlockFont);
    writeFontPref(style, "sticking", textBlockFont);
    for (int i = 1; i <= 12; ++i) {
        writeFontPref(style, "user" + std::to_string(i), textBlockFont);
    }
}

void FinaleParser::importStyles()
{
    MStyle& style = m_score->style();
    writePagePrefs(style, *this);
    writeLyricsPrefs(style, *this);
    writeLineMeasurePrefs(style, *this);
    writeStemPrefs(style, *this);
    writeMusicSpacingPrefs(style, *this);
    writeNoteRelatedPrefs(style, *this);
    writeSmartShapePrefs(style, *this);
    writeMeasureNumberPrefs(style, *this);
    writeRepeatEndingPrefs(style, *this);
    writeTupletPrefs(style, *this);
    writeMarkingPrefs(style, *this);
}

}
