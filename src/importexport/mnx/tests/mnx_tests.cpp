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

#include <gtest/gtest.h>

#include <algorithm>
#include <memory>
#include <regex>
#include <string>
#include <unordered_set>
#include <vector>

#include "engraving/dom/masterscore.h"
#include "engraving/dom/mscore.h"
#include "engraving/engravingerrors.h"

#include "engraving/tests/utils/scorerw.h"
#include "importexport/mnx/internal/notationmnxreader.h"
#include "importexport/mnx/internal/export/mnxexporter.h"

#include "io/dir.h"
#include "io/buffer.h"
#include "io/file.h"
#include "io/fileinfo.h"
#include "io/path.h"

#include "types/bytearray.h"
#include "types/ret.h"
#include "engraving/rw/rwregister.h"

using namespace mu::engraving;
using namespace mu::iex::mnxio;
using namespace muse;

static const String MNX_DATA_DIR(u"data/");
static const String MNX_REFERENCE_DIR(u"data/mnx_reference_examples/");
static const String MSCX_REFERENCE_DIR(u"data/mscx_reference_examples/");
static const String MSCX_PROJECT_REFERENCE_DIR(u"data/");

static const std::unordered_set<std::string> MNX_NO_ROUNDTRIP {
    /// @note Files listed here are known to contain dynamics, which are not currently exported to MNX.
    "dynamics"
};

class Mnx_Tests : public ::testing::Test
{
public:
    MasterScore* readMnxScore(const String& fileName, bool isAbsolutePath = false);
    bool exportMnxScore(Score* score, const String& fileName);
    MasterScore* roundTripMnxScore(const String& sourceFile, const String& exportedFile);

    bool compareWithMscxReference(Score* score, const String& referencePath);
    bool importReferenceExample(const String& baseName);
    void runProjectFileTest(const char* name);
    void runW3cExampleTest(const char* name);
};

//---------------------------------------------------------
//   fixupScore -- do required fixups after reading/importing score
//---------------------------------------------------------

static void fixupScore(MasterScore* score)
{
    score->connectTies();
    score->masterScore()->rebuildMidiMapping();
    score->setSaved(false);
}

MasterScore* Mnx_Tests::readMnxScore(const String& fileName, bool isAbsolutePath)
{
    auto importFunc = [](MasterScore* score, const muse::io::path_t& path) -> Err {
        NotationMnxReader reader;
        Ret ret = reader.read(score, path);
        if (ret.success()) {
            return Err::NoError;
        }

        const int code = ret.code();
        if (code >= static_cast<int>(Ret::Code::EngravingFirst)
            && code <= static_cast<int>(Ret::Code::EngravingLast)) {
            return static_cast<Err>(code);
        }

        if (code == static_cast<int>(Ret::Code::NotSupported)
            || code == static_cast<int>(Ret::Code::BadData)) {
            return Err::FileBadFormat;
        }

        return Err::UnknownError;
    };

    MasterScore* score = ScoreRW::readScore(fileName, isAbsolutePath, importFunc);
    return score;
}

bool Mnx_Tests::exportMnxScore(Score* score, const String& fileName)
{
    MnxExporter exporter(score);
    Ret ret = exporter.exportMnx();
    if (!ret.success()) {
        return false;
    }

    std::string json = exporter.mnxDocument().root()->dump(2);
    ByteArray data = ByteArray::fromRawData(json.data(), json.size());

    Ret writeRet = io::File::writeFile(fileName, data);
    return writeRet.success();
}

MasterScore* Mnx_Tests::roundTripMnxScore(const String& sourceFile, const String& exportedFile)
{
    std::unique_ptr<MasterScore> score(readMnxScore(sourceFile));
    if (!score) {
        return nullptr;
    }

    fixupScore(score.get());
    score->doLayout();

    if (!exportMnxScore(score.get(), exportedFile)) {
        return nullptr;
    }

    io::path_t exportedPath = io::absoluteFilePath(exportedFile);
    MasterScore* roundTrip = readMnxScore(exportedPath.toString(), true);
    if (roundTrip) {
        fixupScore(roundTrip);
        roundTrip->doLayout();
    }

    return roundTrip;
}

bool Mnx_Tests::compareWithMscxReference(Score* score, const String& referencePath)
{
#if MUE_MNX_WRITE_REFS
    const String referenceAbsPath = ScoreRW::rootPath() + u"/" + referencePath;
    const io::path_t referenceDir = io::dirpath(referenceAbsPath);
    io::Dir dir(referenceDir);
    if (!dir.exists()) {
        io::Dir::mkpath(referenceDir);
    }
    return ScoreRW::saveScore(score, referenceAbsPath);
#else
    io::Buffer buffer;
    if (!buffer.open(io::IODevice::WriteOnly)) {
        return false;
    }

    if (!engraving::rw::RWRegister::writer(score->iocContext())->writeScore(score, &buffer)) {
        return false;
    }

    const std::string outputText = normalizeMscxText(
        std::string(reinterpret_cast<const char*>(buffer.data().constData()), buffer.data().size()));

    ByteArray referenceData;
    const String referenceAbsPath = ScoreRW::rootPath() + u"/" + referencePath;
    Ret readRet = io::File::readFile(referenceAbsPath, referenceData);
    if (!readRet.success()) {
        return false;
    }

    const std::string referenceText = normalizeMscxText(
        std::string(referenceData.constChar(), referenceData.size()));

    if (referenceText == outputText) {
        return true;
    }

    return false;
#endif
}

static String mnxBaseNameFromMacro(const char* name)
{
    std::string baseName = name;
    std::replace(baseName.begin(), baseName.end(), '_', '-');
    return String::fromUtf8(baseName.c_str());
}

static String mscxRefName(const String& baseName)
{
    return baseName + u"_ref.mscx";
}

static String projectRefPath(const String& baseName)
{
    return MSCX_PROJECT_REFERENCE_DIR + mscxRefName(baseName);
}

static String w3cRefPath(const String& baseName)
{
    return MSCX_REFERENCE_DIR + mscxRefName(baseName);
}

static String tempRoundTripPath(const String& baseName)
{
    return u"mnx_roundtrip_" + baseName + u".mnx";
}

static std::string normalizeMscxText(const std::string& text)
{
    static const std::regex trackNameRe("<trackName>[\\s\\S]*?</trackName>");
    return std::regex_replace(text, trackNameRe, "");
}

bool Mnx_Tests::importReferenceExample(const String& baseName)
{
    const String referencePath = w3cRefPath(baseName);
    const String referenceAbsPath = ScoreRW::rootPath() + u"/" + referencePath;
#if !MUE_MNX_WRITE_REFS
    if (!io::FileInfo::exists(referenceAbsPath)) {
        ADD_FAILURE() << "Missing MSCX reference file: " << referencePath.toStdString();
        return false;
    }
#endif

    SCOPED_TRACE(baseName.toStdString());
    const String sourcePath = MNX_REFERENCE_DIR + baseName + u".json";

    std::unique_ptr<MasterScore> score(readMnxScore(sourcePath));
    if (!score) {
        ADD_FAILURE() << "Failed to import MNX reference file: " << sourcePath.toStdString();
        return false;
    }

    fixupScore(score.get());
    score->doLayout();

    EXPECT_TRUE(compareWithMscxReference(score.get(), referencePath));
    return true;
}

void Mnx_Tests::runProjectFileTest(const char* name)
{
    const String baseName = String::fromUtf8(name);
    const String sourcePath = MNX_DATA_DIR + baseName + u".mnx";

    std::unique_ptr<MasterScore> score(readMnxScore(sourcePath));
    ASSERT_TRUE(score);

    fixupScore(score.get());
    score->doLayout();

    const String referencePath = projectRefPath(baseName);
    EXPECT_TRUE(compareWithMscxReference(score.get(), referencePath));

    if (MUE_MNX_WRITE_REFS) {
        return;
    }
    if (MNX_NO_ROUNDTRIP.count(baseName.toStdString()) > 0) {
        return;
    }

    const String exportName = tempRoundTripPath(baseName);
    std::unique_ptr<MasterScore> roundTrip(roundTripMnxScore(sourcePath, exportName));
    (void)io::File::remove(exportName);
    ASSERT_TRUE(roundTrip);

    EXPECT_TRUE(compareWithMscxReference(roundTrip.get(), referencePath));
}

void Mnx_Tests::runW3cExampleTest(const char* name)
{
    const String baseName = mnxBaseNameFromMacro(name);

    if (!importReferenceExample(baseName)) {
        return;
    }

    if (MUE_MNX_WRITE_REFS) {
        return;
    }
    if (MNX_NO_ROUNDTRIP.count(baseName.toStdString()) > 0) {
        return;
    }

    const String referencePath = w3cRefPath(baseName);
    const String referenceAbsPath = ScoreRW::rootPath() + u"/" + referencePath;
    if (!io::FileInfo::exists(referenceAbsPath)) {
        return;
    }

    const String exportName = tempRoundTripPath(baseName);
    std::unique_ptr<MasterScore> roundTrip(roundTripMnxScore(MNX_REFERENCE_DIR + baseName + u".json", exportName));
    (void)io::File::remove(exportName);
    ASSERT_TRUE(roundTrip);

    EXPECT_TRUE(compareWithMscxReference(roundTrip.get(), referencePath));
}

#define MNX_PROJECT_FILE_TEST(name) \
    TEST_F(Mnx_Tests, project_##name) { runProjectFileTest(#name); }

#define MNX_PROJECT_FILE_TEST_DISABLED(name) \
    TEST_F(Mnx_Tests, DISABLED_project_##name) { runProjectFileTest(#name); }

#define MNX_W3C_EXAMPLE_TEST(name) \
    TEST_F(Mnx_Tests, w3c_##name) { runW3cExampleTest(#name); }

#define MNX_W3C_EXAMPLE_TEST_DISABLED(name) \
    TEST_F(Mnx_Tests, DISABLED_w3c_##name) { runW3cExampleTest(#name); }

MNX_PROJECT_FILE_TEST(altoFluteTrem)
MNX_PROJECT_FILE_TEST(altoFluteTremMissingKey)
MNX_PROJECT_FILE_TEST(barlineTypesOriginal)
MNX_PROJECT_FILE_TEST(bcl)
MNX_PROJECT_FILE_TEST(beamsOverBarlines)
MNX_PROJECT_FILE_TEST(clarinet38)
MNX_PROJECT_FILE_TEST(clarinet38MissingTime)
MNX_PROJECT_FILE_TEST(enharmonicPart)
MNX_PROJECT_FILE_TEST(graceBeamed)
MNX_PROJECT_FILE_TEST(key77)
MNX_PROJECT_FILE_TEST(key77Wrapped)
MNX_PROJECT_FILE_TEST(layoutBrackets)
MNX_PROJECT_FILE_TEST(multinoteTremolos)
MNX_PROJECT_FILE_TEST(multinoteTremolosAdv)
MNX_PROJECT_FILE_TEST(ottavas)
MNX_PROJECT_FILE_TEST(percussionKit)
MNX_PROJECT_FILE_TEST(tupletHiddenRest)
MNX_PROJECT_FILE_TEST(tupletNested)
MNX_PROJECT_FILE_TEST(tupletSimple)

MNX_W3C_EXAMPLE_TEST(accidentals)
MNX_W3C_EXAMPLE_TEST(articulations)
MNX_W3C_EXAMPLE_TEST(beam_hooks)
MNX_W3C_EXAMPLE_TEST(beams_across_barlines)
MNX_W3C_EXAMPLE_TEST(beams_inner_grace_notes)
MNX_W3C_EXAMPLE_TEST(beams_secondary_beam_breaks_implied)
MNX_W3C_EXAMPLE_TEST(beams_secondary_beam_breaks)
MNX_W3C_EXAMPLE_TEST(beams)
MNX_W3C_EXAMPLE_TEST(clef_changes)
MNX_W3C_EXAMPLE_TEST(dotted_notes)
MNX_W3C_EXAMPLE_TEST(dynamics)
MNX_W3C_EXAMPLE_TEST(grace_note)
MNX_W3C_EXAMPLE_TEST(grace_notes_beamed)
MNX_W3C_EXAMPLE_TEST(grand_staff)
MNX_W3C_EXAMPLE_TEST(hello_world)
MNX_W3C_EXAMPLE_TEST(jumps_dal_segno)
MNX_W3C_EXAMPLE_TEST(jumps_ds_al_fine)
MNX_W3C_EXAMPLE_TEST(key_signatures)
MNX_W3C_EXAMPLE_TEST(lyric_line_metadata)
MNX_W3C_EXAMPLE_TEST(lyrics_basic)
MNX_W3C_EXAMPLE_TEST(lyrics_multi_line)
MNX_W3C_EXAMPLE_TEST(multi_note_tremolos)
MNX_W3C_EXAMPLE_TEST(multimeasure_rests)
MNX_W3C_EXAMPLE_TEST(multiple_layouts)
MNX_W3C_EXAMPLE_TEST(multiple_voices)
MNX_W3C_EXAMPLE_TEST_DISABLED(orchestral_layout)
MNX_W3C_EXAMPLE_TEST(organ_layout)
MNX_W3C_EXAMPLE_TEST(ottavas_8va)
MNX_W3C_EXAMPLE_TEST(parts)
MNX_W3C_EXAMPLE_TEST(repeats_alternate_endings_advanced)
MNX_W3C_EXAMPLE_TEST(repeats_alternate_endings_simple)
MNX_W3C_EXAMPLE_TEST(repeats_implied_start_repeat)
MNX_W3C_EXAMPLE_TEST(repeats_more_once_repeated)
MNX_W3C_EXAMPLE_TEST(repeats)
MNX_W3C_EXAMPLE_TEST(rest_positions)
MNX_W3C_EXAMPLE_TEST(single_note_tremolos)
MNX_W3C_EXAMPLE_TEST(slurs_chords)
MNX_W3C_EXAMPLE_TEST(slurs_targeting_specific_notes)
MNX_W3C_EXAMPLE_TEST(slurs)
MNX_W3C_EXAMPLE_TEST_DISABLED(system_layouts)
MNX_W3C_EXAMPLE_TEST(tempo_markings)
MNX_W3C_EXAMPLE_TEST(three_note_chord_and_half_rest)
MNX_W3C_EXAMPLE_TEST(tie_target_type)
MNX_W3C_EXAMPLE_TEST(ties)
MNX_W3C_EXAMPLE_TEST(time_signature_glyphs)
MNX_W3C_EXAMPLE_TEST(time_signatures)
MNX_W3C_EXAMPLE_TEST(tuplets)
MNX_W3C_EXAMPLE_TEST(two_bar_c_major_scale)

#undef MNX_W3C_EXAMPLE_TEST_DISABLED
#undef MNX_W3C_EXAMPLE_TEST
#undef MNX_PROJECT_FILE_TEST_DISABLED
#undef MNX_PROJECT_FILE_TEST
