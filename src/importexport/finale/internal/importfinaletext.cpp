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
#include "internal/importfinaleparser.h"
#include "internal/importfinalelogger.h"
#include "finaletypesconv.h"
#include "internal/text/finaletextconv.h"

#include <vector>
#include <exception>
#include <sstream>

#include "musx/musx.h"

#include "types/string.h"

#include "engraving/dom/excerpt.h"
#include "engraving/dom/factory.h"
#include "engraving/dom/measure.h"
#include "engraving/dom/masterscore.h"
#include "engraving/dom/page.h"
#include "engraving/dom/score.h"
#include "engraving/dom/segment.h"
#include "engraving/dom/sig.h"
#include "engraving/dom/staff.h"
#include "engraving/dom/system.h"
#include "engraving/dom/utils.h"

#include "engraving/types/symnames.h"

#include "log.h"

using namespace mu::engraving;
using namespace muse;
using namespace musx::dom;

namespace mu::iex::finale {

/// @todo Instead of hard-coding page 1 and page 2, we need to find the first page in the Finale file with music on it
/// and use that as the first page. At least, that is my impression. How to handle blank pages in MuseScore is an open question.
/// - RGP

String FinaleParser::stringFromEnigmaText(const musx::util::EnigmaParsingContext& parsingContext, const EnigmaParsingOptions& options)
{
    String endString;
    const bool isHeaderOrFooter = options.hfType != HeaderFooterType::None;
    std::shared_ptr<FontInfo> prevFont;
    /// @todo textstyle support: initialise value by checking if with &, then using +/- to set font style

    // helper lamdas
    auto calcEffectiveFontSize = [&](const std::shared_ptr<FontInfo>& fontInfo) -> double {
        if (!fontInfo) return -1; // impossible initial value
        double finalScaling = fontInfo->absolute ? 1.0 : options.scaleFontSizeBy;
        return FinaleTConv::spatiumScaledFontSize(fontInfo) * finalScaling;
    };

    auto checkFontStylebit = [&](bool bit, bool prevBit, const String& museScoreTag) {
        if (bit != prevBit) {
            if (bit) {
                endString.append(String(u"<" + museScoreTag + u">"));
            } else {
                endString.append(String(u"</" + museScoreTag + u">"));
            }
        }
    };

    // The processTextChunk function process each chunk of processed text with font information. It is only
    // called when the font information changes.
    auto processTextChunk = [&](const std::string& nextChunk, const musx::util::EnigmaStyles& styles) -> bool {
        const std::shared_ptr<FontInfo>& font = styles.font;
        if (!prevFont || prevFont->fontId != font->fontId) {
            // When using musical fonts, don't actually set the font type since symbols are loaded separately.
            /// @todo decide when we want to not convert symbols/fonts, e.g. to allow multiple musical fonts in one score.
            /// @todo append this based on whether symbol ends up being replaced or not.
            //if (!font->calcIsDefaultMusic()) { /// @todo RGP changed from a name check, but each notation element has its own default font setting in Finale. We need to handle that.
                endString.append(String(u"<font face=\"" + String::fromStdString(font->getName()) + u"\"/>"));
            //}
        }
        double prevSize = calcEffectiveFontSize(prevFont);
        double currSize = calcEffectiveFontSize(font);
        if (prevSize != currSize) {
            endString.append(String(u"<font size=\""));
            endString.append(String::number(currSize, 2) + String(u"\"/>"));
        }
        if (!prevFont || prevFont->getEnigmaStyles() != font->getEnigmaStyles()) {
            checkFontStylebit(font->bold, prevFont && prevFont->bold, u"b");
            checkFontStylebit(font->italic, prevFont && prevFont->italic, u"i");
            checkFontStylebit(font->underline, prevFont && prevFont->underline, u"u");
            checkFontStylebit(font->strikeout, prevFont && prevFont->strikeout, u"s");
        }
        prevFont = std::make_shared<FontInfo>(*font);
        endString.append(String::fromStdString(nextChunk));
        return true;
    };

    // The processCommand function sends back to the parser a subsitution string for the Enigma command.
    // The command is parsed with the command in the first element and any parameters in subsequent elements.
    // Return "" to remove the command from the processed string. Return std::nullopt to have the parsing function insert
    // a default value.
    auto processCommand = [&](const std::vector<std::string>& parsedCommand) -> std::optional<std::string> {
        if (parsedCommand.empty()) {
            // log error
            return std::nullopt;
        }
        /// @todo Perhaps add parse functions to classes like PageTextAssign to handle this automatically. But it also may be important
        /// to handle it here for an intelligent import, if text can reference a page number offset in MuseScore.
        if (parsedCommand[0] == "page") {
            // ignore page offset argument. we set this once in the style settings.
            switch (options.hfType) {
                default:
                case HeaderFooterType::None: break;
                case HeaderFooterType::FirstPage: break;
                case HeaderFooterType::SecondPageToEnd:
                    if (parsedCommand.size() > 1) {
                        // always overwrite with the last one we find.
                        m_score->setPageNumberOffset(std::stoi(parsedCommand[1]));
                    }
                    return "$p";
            }
        } else if (parsedCommand[0] == "partname") {
            switch (options.hfType) {
                /// @todo maybe create a "partname" metatag instead? (Especially if excerpts can have different values.)
                default:
                case HeaderFooterType::None: break;
                case HeaderFooterType::FirstPage: return "$I";
                case HeaderFooterType::SecondPageToEnd: return "$i";
            }
        } else if (parsedCommand[0] ==  "totpages") {
            if (isHeaderOrFooter) {
                return "$n";
            }
            return std::to_string(m_score->npages());
        } else if (parsedCommand[0] == "filename") {
            if (isHeaderOrFooter) {
                return "$f";
            }
            /// @todo Does the file have a name at import time? Otherwise we could use the musx filename we opened.
            return m_score->masterScore()->name().toStdString();
        } else if (parsedCommand[0] ==  "perftime") {
            /// @todo: honor format code (see class comments for musx::util::EnigmaString)
            /// Note that Finale's UI does not support any format but m'ss", but plugins could have inserted other formats.
            int rawDurationSeconds = m_score->duration();
            int minutes = rawDurationSeconds / 60;
            int seconds = rawDurationSeconds % 60;
            std::ostringstream oss;
            oss << minutes << '\'' << std::setw(2) << std::setfill('0') << seconds << '"';
            return oss.str();
        } else if (parsedCommand[0] ==  "copyright") {
            /// @todo maybe not use $C/$c at all in favor of $:copyright:.?
            switch (options.hfType) {
                default:
                case HeaderFooterType::None: return "$:copyright:";
                case HeaderFooterType::FirstPage: return "$C";
                case HeaderFooterType::SecondPageToEnd: return "$c";
            }
        }
        // Find and insert metaTags when appropriate
        if (isHeaderOrFooter) {
            String metaTag = FinaleTConv::metaTagFromTextComponent(parsedCommand[0]);
            if (!metaTag.isEmpty()) {
                return "$:" + metaTag.toStdString() + ":";
            }
        }
        // Returning std::nullopt allows the musx library to fill in any we have not handled.
        return std::nullopt;
    };

    parsingContext.parseEnigmaText(processTextChunk, processCommand);

    return endString;
};

bool FinaleParser::isOnlyPage(const std::shared_ptr<others::PageTextAssign>& pageTextAssign, PageCmper page)
{
    const std::optional<PageCmper> startPageNum = pageTextAssign->calcStartPageNumber(m_currentMusxPartId);
    const std::optional<PageCmper> endPageNum = pageTextAssign->calcEndPageNumber(m_currentMusxPartId); // calcEndPageNumber handles case when endPage is zero
    return (startPageNum == page && endPageNum == page);
};

void FinaleParser::importPageTexts()
{
    FinaleTextConv::init();
    std::vector<std::shared_ptr<others::PageTextAssign>> pageTextAssignList = m_doc->getOthers()->getArray<others::PageTextAssign>(m_currentMusxPartId);

    // we need to work with real-time positions and pages, so we layout the score.
    m_score->setLayoutAll();
    m_score->doLayout();


    // code idea:
    // first, read score metadata
    // then, handle page text as header/footer vframes where applicable and if not, assign it to a measure
    // each handled textblock is parsed as possible with fontdata and appended to a list of used fontdata
    // whenever an element is read, check with Cmper in a map to see if textblock has already been parsed, if so, used it from there, if not, parse and add
    // measure-anchored-texts: determine text style based on (??? contents? font settings?), add to score, same procedure with text block
    // if centered or rightmost, create as marking to get correct anchoring???
    // expressions::
    // tempo changes
    // need to create character conversion map for non-smufl fonts???

    // set score metadata
    std::vector<std::shared_ptr<texts::FileInfoText>> fileInfoTexts = m_doc->getTexts()->getArray<texts::FileInfoText>();
    for (std::shared_ptr<texts::FileInfoText> fileInfoText : fileInfoTexts) {
        String metaTag = FinaleTConv::metaTagFromFileInfo(fileInfoText->getTextType());
        std::string fileInfoValue = musx::util::EnigmaString::trimTags(fileInfoText->text);
        if (!metaTag.empty() && !fileInfoValue.empty()) {
            m_score->setMetaTag(metaTag, String::fromStdString(fileInfoValue));
        }
    }

    struct HeaderFooter {
        bool show = false;
        bool showFirstPage = true; // always show first page
        bool oddEven = true; // always different odd/even pages
        std::vector<std::shared_ptr<others::PageTextAssign>> oddLeftTexts;
        std::vector<std::shared_ptr<others::PageTextAssign>> oddMiddleTexts;
        std::vector<std::shared_ptr<others::PageTextAssign>> oddRightTexts;
        std::vector<std::shared_ptr<others::PageTextAssign>> evenLeftTexts;
        std::vector<std::shared_ptr<others::PageTextAssign>> evenMiddleTexts;
        std::vector<std::shared_ptr<others::PageTextAssign>> evenRightTexts;
    };

    HeaderFooter header;
    HeaderFooter footer;
    std::vector<std::shared_ptr<others::PageTextAssign>> notHF;
    std::vector<std::shared_ptr<others::PageTextAssign>> remainder;

    // gather texts by position
    for (std::shared_ptr<others::PageTextAssign> pageTextAssign : pageTextAssignList) {
        if (pageTextAssign->hidden) {
            // there may be something we can do with hidden assignments created for Patterson's Copyist Helper plugin,
            // but generally it means the header is not applicable to this part.
            continue;
        }
        const std::optional<PageCmper> startPage = pageTextAssign->calcStartPageNumber(m_currentMusxPartId);
        const std::optional<PageCmper> endPage = pageTextAssign->calcEndPageNumber(m_currentMusxPartId);
        if (!startPage || !endPage) {
            // this page text does not appear on any page in this musx score/linked part.
            // it happens
            //  1) when the assignment is to a leading blank page that does not exist in this score/part
            //  2) when the start page assignment is beyond the number of pages in this score/part
            continue;
        }

        // if text is not at top or bottom, invisible,
        // not recurring, or not on page 1, don't import as hf
        // For 2-page scores, we can import text only assigned to page 2 as a regular even hf.
        // For 3-page scores, we can import text only assigned to page 2 as a regular odd hf if we disable hf on page one.
        // RGP: I don't think we should do the 3rd option. Disabling hf on page one is a non-starter. We should never do that,
        //      because it causes far more damage than benefit. I changed it not to.
        /// @todo add sensible limits for xDisp and such.
        if (pageTextAssign->vPos == others::PageTextAssign::VerticalAlignment::Center
                                 || pageTextAssign->hidden
                                 || startPage.value() >= 3 /// @todo must be changed to be first non-blank page + 2
                                 || endPage.value() < PageCmper(m_score->npages())) {
            notHF.emplace_back(pageTextAssign);
            continue;
        }
        remainder.emplace_back(pageTextAssign);
    }

    for (std::shared_ptr<others::PageTextAssign> pageTextAssign : remainder) {
        HeaderFooter& hf = pageTextAssign->vPos == others::PageTextAssign::VerticalAlignment::Top ? header : footer;
        hf.show = true;
    }
    for (std::shared_ptr<others::PageTextAssign> pageTextAssign : remainder) {
        HeaderFooter& hf = pageTextAssign->vPos == others::PageTextAssign::VerticalAlignment::Top ? header : footer;
        /// @todo this has got to take into account the page text's hPosLp or hPosRp based on indRpPos.
        /// @todo Finale bases right/left on the actual page numbers, not the visual page numbers. But MuseScore's
        /// left/right headers display based on visual page numbers. So the whole calculation must be reversed if
        /// m_score->pageNumberOffset() is odd.
        switch (pageTextAssign->hPosLp) {
        case others::PageTextAssign::HorizontalAlignment::Left:
            if (pageTextAssign->oddEven != others::PageTextAssign::PageAssignType::Even) {
                hf.oddLeftTexts.emplace_back(pageTextAssign);
            }
            if (pageTextAssign->oddEven != others::PageTextAssign::PageAssignType::Odd) {
                hf.evenLeftTexts.emplace_back(pageTextAssign);
            }
            break;
        case others::PageTextAssign::HorizontalAlignment::Center:
            if (pageTextAssign->oddEven != others::PageTextAssign::PageAssignType::Even) {
                hf.oddMiddleTexts.emplace_back(pageTextAssign);
            }
            if (pageTextAssign->oddEven != others::PageTextAssign::PageAssignType::Odd) {
                hf.evenMiddleTexts.emplace_back(pageTextAssign);
            }
            break;
        case others::PageTextAssign::HorizontalAlignment::Right:
            if (pageTextAssign->oddEven != others::PageTextAssign::PageAssignType::Even) {
                hf.oddRightTexts.emplace_back(pageTextAssign);
            }
            if (pageTextAssign->oddEven != others::PageTextAssign::PageAssignType::Odd) {
                hf.evenRightTexts.emplace_back(pageTextAssign);
            }
            break;
        }
    }

    auto stringFromPageText = [&](const std::shared_ptr<others::PageTextAssign>& pageText, bool isForHeaderFooter = true) {
        std::optional<PageCmper> startPage = pageText->calcStartPageNumber(m_currentMusxPartId);
        std::optional<PageCmper> endPage = pageText->calcEndPageNumber(m_currentMusxPartId);
        HeaderFooterType hfType = isForHeaderFooter ? HeaderFooterType::FirstPage : HeaderFooterType::None;
        if (isForHeaderFooter && startPage == 2 && endPage.value() == PageCmper(m_score->npages())) {
            hfType = HeaderFooterType::SecondPageToEnd;
        }
        std::optional<PageCmper> forPageId = hfType != HeaderFooterType::SecondPageToEnd ? startPage : std::nullopt;
        musx::util::EnigmaParsingContext parsingContext = pageText->getRawTextCtx(m_currentMusxPartId, forPageId);
        EnigmaParsingOptions options(hfType);
        /// @todo set options.scaleFontSizeBy to per-page scaling if MuseScore can't do per-page scaling directly.
        return stringFromEnigmaText(parsingContext, hfType);
    };

    if (header.show) {
        m_score->style().set(Sid::showHeader,      true);
        m_score->style().set(Sid::headerFirstPage, header.showFirstPage);
        m_score->style().set(Sid::headerOddEven,   header.oddEven);
        m_score->style().set(Sid::evenHeaderL,     header.evenLeftTexts.empty() ? String() : stringFromPageText(header.evenLeftTexts.front())); // for now
        m_score->style().set(Sid::evenHeaderC,     header.evenMiddleTexts.empty() ? String() : stringFromPageText(header.evenMiddleTexts.front()));
        m_score->style().set(Sid::evenHeaderR,     header.evenRightTexts.empty() ? String() : stringFromPageText(header.evenRightTexts.front()));
        m_score->style().set(Sid::oddHeaderL,      header.oddLeftTexts.empty() ? String() : stringFromPageText(header.oddLeftTexts.front()));
        m_score->style().set(Sid::oddHeaderC,      header.oddMiddleTexts.empty() ? String() : stringFromPageText(header.oddMiddleTexts.front()));
        m_score->style().set(Sid::oddHeaderR,      header.oddRightTexts.empty() ? String() : stringFromPageText(header.oddRightTexts.front()));
    }

    if (footer.show) {
        m_score->style().set(Sid::showFooter,      true);
        m_score->style().set(Sid::footerFirstPage, footer.showFirstPage);
        m_score->style().set(Sid::footerOddEven,   footer.oddEven);
        m_score->style().set(Sid::evenFooterL,     footer.evenLeftTexts.empty() ? String() : stringFromPageText(footer.evenLeftTexts.front())); // for now
        m_score->style().set(Sid::evenFooterC,     footer.evenMiddleTexts.empty() ? String() : stringFromPageText(footer.evenMiddleTexts.front()));
        m_score->style().set(Sid::evenFooterR,     footer.evenRightTexts.empty() ? String() : stringFromPageText(footer.evenRightTexts.front()));
        m_score->style().set(Sid::oddFooterL,      footer.oddLeftTexts.empty() ? String() : stringFromPageText(footer.oddLeftTexts.front()));
        m_score->style().set(Sid::oddFooterC,      footer.oddMiddleTexts.empty() ? String() : stringFromPageText(footer.oddMiddleTexts.front()));
        m_score->style().set(Sid::oddFooterR,      footer.oddRightTexts.empty() ? String() : stringFromPageText(footer.oddRightTexts.front()));
    }

    std::vector<Cmper> pagesWithHeaderFrames;
    std::vector<Cmper> pagesWithFooterFrames;

    auto getPages = [this](const std::shared_ptr<others::PageTextAssign>& pageTextAssign) -> std::vector<page_idx_t> {
        std::vector<page_idx_t> pagesWithText;
        page_idx_t startP = page_idx_t(pageTextAssign->calcStartPageNumber(m_currentMusxPartId).value_or(1) - 1);
        page_idx_t endP = page_idx_t(pageTextAssign->calcStartPageNumber(m_currentMusxPartId).value_or(PageCmper(m_score->npages())) - 1);
        for (page_idx_t i = startP; i <= endP; ++i) {
            pagesWithText.emplace_back(i);
        }
        return pagesWithText;
    };

    auto pagePosOfPageTextAssign = [](Page* page, const std::shared_ptr<others::PageTextAssign>& pageTextAssign) -> PointF {
        RectF pageBox = page->ldata()->bbox(); // height and width definitely work, this hopefully too
        PointF p;

        switch (pageTextAssign->vPos) {
        case others::PageTextAssign::VerticalAlignment::Center:
            p.ry() = pageBox.y() / 2;
            break;
        case others::PageTextAssign::VerticalAlignment::Top:
            p.ry() = pageBox.y();
            break;
        case others::PageTextAssign::VerticalAlignment::Bottom:;
            break;
        }

        if (pageTextAssign->indRpPos && !(page->no() & 1)) {
            switch(pageTextAssign->hPosRp) {
            case others::PageTextAssign::HorizontalAlignment::Center:
                p.rx() = pageBox.x() / 2;
                break;
            case others::PageTextAssign::HorizontalAlignment::Right:;
                p.rx() = pageBox.x();
                break;
            case others::PageTextAssign::HorizontalAlignment::Left:
                break;
            }
            p.rx() += FinaleTConv::doubleFromEvpu(pageTextAssign->rightPgXDisp);
            p.ry() += FinaleTConv::doubleFromEvpu(pageTextAssign->rightPgYDisp);
        } else {
            switch(pageTextAssign->hPosLp) {
            case others::PageTextAssign::HorizontalAlignment::Center:
                p.rx() = pageBox.x() / 2;
                break;
            case others::PageTextAssign::HorizontalAlignment::Right:;
                p.rx() = pageBox.x();
                break;
            case others::PageTextAssign::HorizontalAlignment::Left:
                break;
            }
            p.rx() += FinaleTConv::doubleFromEvpu(pageTextAssign->xDisp);
            p.ry() += FinaleTConv::doubleFromEvpu(pageTextAssign->yDisp);
        }
        return p;
    };

    auto getMeasureForPageTextAssign = [](Page* page, PointF p, bool allowNonMeasures = true) -> MeasureBase* {
        MeasureBase* closestMB = nullptr;
        double prevDist = DBL_MAX;
        for (System* s : page->systems()) {
            for (MeasureBase* m : s->measures()) {
                if (allowNonMeasures || m->isMeasure()) {
                    if (m->ldata()->bbox().distanceTo(p) < prevDist) {
                        closestMB = m;
                        prevDist = m->ldata()->bbox().distanceTo(p);
                    }
                }
            }
        }
        return closestMB;
    };

    auto addPageTextToMeasure = [](const std::shared_ptr<others::PageTextAssign>& pageTextAssign, PointF p, MeasureBase* mb, Page* page) {
        PointF relativePos = p - mb->pagePos();
        // if (item->placeBelow()) {
        // ldata->setPosY(item->staff() ? item->staff()->staffHeight(item->tick()) : 0.0);
    };

    for (std::shared_ptr<others::PageTextAssign> pageTextAssign : remainder) {
        //@todo: use sophisticated check for whether to import as frame or not. (i.e. distance to measure is too large, frame would get in the way of music)
        if (pageTextAssign->vPos == others::PageTextAssign::VerticalAlignment::Center) {
            std::vector<page_idx_t> pagesWithText = getPages(pageTextAssign);
            for (page_idx_t i : pagesWithText) {
                Page* page = m_score->pages().at(i);
                PointF pagePosOfPageText = pagePosOfPageTextAssign(page, pageTextAssign);
                MeasureBase* mb = getMeasureForPageTextAssign(page, pagePosOfPageText);
                IF_ASSERT_FAILED (mb) {
                    // RGP: Finale pages can be blank, so this will definitely happen on the Finale side. (Check others::Page::isBlank to determine if it is blank)
                    // log error
                    // this should never happen! all pages need at least one measurebase
                }
                addPageTextToMeasure(pageTextAssign, pagePosOfPageText, mb, page);
            }
        }
    }

    // Don't add frames for text vertically aligned to the center.
    // if top or bottom, we should hopefully be able to check for distance to surrounding music and work from that
    // if not enough space, attempt to position based on closest measure
    //note: text is placed slightly lower than indicated position (line space?)
    // todo: read text properties but also tempo, swing, etc
    //  NOTE from RGP: tempo, swing, etc. are text expressions and will be handled separately from page text.
}

}
