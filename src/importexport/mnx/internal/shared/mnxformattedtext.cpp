/*
 * SPDX-License-Identifier: GPL-3.0-only
 * MuseScore-Studio-CLA-applies
 *
 * MuseScore Studio
 * Music Composition & Notation
 *
 * Copyright (C) 2026 MuseScore Limited and others
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

#include "mnxformattedtext.h"

#include <memory>
#include <optional>

#include "engraving/iengravingfontsprovider.h"
#include "engraving/style/style.h"
#include "engraving/types/symnames.h"
#include "modularity/ioc.h"

namespace mu::iex::mnxio {
namespace {

std::shared_ptr<mu::engraving::IEngravingFontsProvider> engravingFonts()
{
    return muse::modularity::globalIoc()->resolve<mu::engraving::IEngravingFontsProvider>("engraving");
}

std::optional<std::string> glyphNameForScoreTextChar(const mu::engraving::Char ch)
{
    const mu::engraving::SymId symId = engravingFonts()->fallbackFont()->fromCode(ch.unicode());
    if (symId == mu::engraving::SymId::noSym) {
        return std::nullopt;
    }

    return mu::engraving::SymNames::nameForSymId(symId).ascii();
}

mu::engraving::String resolvedFontFamily(const mu::engraving::TextBase& textBase, const mu::engraving::CharFormat& format)
{
    if (format.fontFamily() == u"ScoreText") {
        return textBase.style().styleSt(mu::engraving::Sid::musicalSymbolFont);
    }

    if (!format.fontFamily().empty()) {
        return format.fontFamily();
    }

    return textBase.propertyDefault(mu::engraving::Pid::FONT_FACE).value<mu::engraving::String>();
}

template <typename T>
void applyStyle(T item, const mu::engraving::TextBase& textBase, const mu::engraving::CharFormat& format)
{
    auto style = item.ensure_style();
    style.set_font(resolvedFontFamily(textBase, format).toStdString());
    style.set_size(format.fontSize());
    style.set_or_clear_fontStyle(format.italic() ? mnx::FontStyle::Italic : mnx::FontStyle::Plain);
    style.set_or_clear_weight(format.bold() ? mnx::FontWeight::Bold : mnx::FontWeight::Plain);
}

template <typename T>
void applySmuflStyle(T item, const mu::engraving::TextBase& textBase)
{
    auto style = item.ensure_style();
    style.set_font(textBase.style().styleSt(mu::engraving::Sid::musicalSymbolFont).toStdString());
    style.set_size(mu::engraving::StyleDef::DEFAULT_SMUFL_POINT_SIZE());
}

void appendTextChunk(mnx::FormattedText& dst, const mu::engraving::TextBase& textBase, const mu::engraving::String& text,
                     const mu::engraving::CharFormat& format)
{
    if (text.isEmpty()) {
        return;
    }

    auto item = dst.append<mnx::text::Text>(text.toStdString());
    applyStyle(item, textBase, format);
}

void appendSmuflChunk(mnx::FormattedText& dst, const mu::engraving::TextBase& textBase, const std::vector<std::string>& glyphs)
{
    if (glyphs.empty()) {
        return;
    }

    auto item = dst.append<mnx::text::Smufl>(glyphs);
    applySmuflStyle(item, textBase);
}

void appendFragment(mnx::FormattedText& dst, const mu::engraving::TextBase& textBase, const mu::engraving::TextFragment& fragment)
{
    const mu::engraving::CharFormat& format = fragment.format;
    if (format.fontFamily() != u"ScoreText") {
        appendTextChunk(dst, textBase, fragment.text, format);
        return;
    }

    mu::engraving::String pendingText;
    std::vector<std::string> pendingGlyphs;

    auto flushText = [&]() {
        if (!pendingText.isEmpty()) {
            appendTextChunk(dst, textBase, pendingText, format);
            pendingText.clear();
        }
    };

    auto flushGlyphs = [&]() {
        if (!pendingGlyphs.empty()) {
            appendSmuflChunk(dst, textBase, pendingGlyphs);
            pendingGlyphs.clear();
        }
    };

    for (size_t i = 0; i < fragment.text.size(); ++i) {
        const mu::engraving::Char ch = fragment.text.at(i);
        const std::optional<std::string> glyphName = glyphNameForScoreTextChar(ch);
        if (glyphName) {
            flushText();
            pendingGlyphs.push_back(*glyphName);
        } else {
            flushGlyphs();
            pendingText += ch;
        }
    }

    flushText();
    flushGlyphs();
}

} // namespace

void setMnxFormattedText(mnx::FormattedText& dst, const mu::engraving::TextBase& textBase)
{
    dst.clear();
    for (const mu::engraving::TextFragment& fragment : textBase.fragmentList()) {
        appendFragment(dst, textBase, fragment);
    }
}

} // namespace mu::iex::mnxio
