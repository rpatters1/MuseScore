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
#include "finaletypesconv.h"

#include <vector>
#include <string_view>

#include "musx/musx.h"

#include "types/string.h"

#include "engraving/dom/accidental.h"
#include "engraving/dom/note.h"
#include "engraving/dom/noteval.h"

#include "importfinalelogger.h"

#include "log.h"

using namespace mu::engraving;
using namespace muse;
using namespace musx::dom;

namespace mu::iex::finale {

ID FinaleTConv::createPartId(int partNumber)
{
    return "P" + std::to_string(partNumber);
}

ID FinaleTConv::createStaffId(musx::dom::InstCmper staffId)
{
    return std::to_string(staffId);
}

int FinaleTConv::createFinaleVoiceId(musx::dom::LayerIndex layerIndex, bool forV2)
{
    return (layerIndex * 2 + int(forV2));
}

DurationType FinaleTConv::noteTypeToDurationType(musx::dom::NoteType noteType)
{
    static const std::unordered_map<musx::dom::NoteType, DurationType> noteTypeTable = {
        { musx::dom::NoteType::Maxima,     DurationType::V_INVALID },
        { musx::dom::NoteType::Longa,      DurationType::V_LONG },
        { musx::dom::NoteType::Breve,      DurationType::V_BREVE },
        { musx::dom::NoteType::Whole,      DurationType::V_WHOLE },
        { musx::dom::NoteType::Half,       DurationType::V_HALF },
        { musx::dom::NoteType::Quarter,    DurationType::V_QUARTER },
        { musx::dom::NoteType::Eighth,     DurationType::V_EIGHTH },
        { musx::dom::NoteType::Note16th,   DurationType::V_16TH },
        { musx::dom::NoteType::Note32nd,   DurationType::V_32ND },
        { musx::dom::NoteType::Note64th,   DurationType::V_64TH },
        { musx::dom::NoteType::Note128th,  DurationType::V_128TH },
        { musx::dom::NoteType::Note256th,  DurationType::V_256TH },
        { musx::dom::NoteType::Note512th,  DurationType::V_512TH },
        { musx::dom::NoteType::Note1024th, DurationType::V_1024TH },
        { musx::dom::NoteType::Note2048th, DurationType::V_INVALID },
        { musx::dom::NoteType::Note4096th, DurationType::V_INVALID },
    };
    return muse::value(noteTypeTable, noteType, DurationType::V_INVALID);
}

TDuration FinaleTConv::noteInfoToDuration(std::pair<musx::dom::NoteType, unsigned> noteInfo)
{
    TDuration d = FinaleTConv::noteTypeToDurationType(noteInfo.first);
    int ndots = static_cast<int>(noteInfo.second);
    if (d.isValid() && ndots <= MAX_DOTS) {
        d.setDots(ndots);
        return d;
    }
    return TDuration(DurationType::V_INVALID);
}

engraving::NoteType FinaleTConv::durationTypeToNoteType(DurationType type, bool after)
{
    if (int(type) < int(DurationType::V_EIGHTH)) {
        return after ? engraving::NoteType::GRACE4 : engraving::NoteType::GRACE8_AFTER;
    }
    if (int(type) >= int(DurationType::V_32ND)) {
        return after ? engraving::NoteType::GRACE32_AFTER : engraving::NoteType::GRACE32;
    }
    if (type == DurationType::V_16TH) {
        return after ? engraving::NoteType::GRACE16_AFTER : engraving::NoteType::GRACE16;
    }
    return after ? engraving::NoteType::GRACE8_AFTER : engraving::NoteType::APPOGGIATURA;
}

ClefType FinaleTConv::toMuseScoreClefType(ClefIndex clef)
{
    // For now, base this on the default clef definitions.
    // A future todo could be to infer the clef from the actual
    // clef definition record in the Musx document's clef options.

    static const std::unordered_map<DefaultClefType, ClefType> defaultClefTypeTable = {
        { DefaultClefType::Treble,        ClefType::G },
        { DefaultClefType::Alto,          ClefType::C3 },
        { DefaultClefType::Tenor,         ClefType::C4 },
        { DefaultClefType::Bass,          ClefType::F },
        { DefaultClefType::Percussion,    ClefType::PERC2 },
        { DefaultClefType::Treble8vb,     ClefType::G8_VB },
        { DefaultClefType::Bass8vb,       ClefType::F8_VB },
        { DefaultClefType::Baritone,      ClefType::F_B },
        { DefaultClefType::FrenchViolin,  ClefType::G_1 },
        { DefaultClefType::BaritoneC,     ClefType::C5 },
        { DefaultClefType::MezzoSoprano,  ClefType::C2 },
        { DefaultClefType::Soprano,       ClefType::C1 },
        { DefaultClefType::AltPercussion, ClefType::PERC },
        { DefaultClefType::Treble8va,     ClefType::G8_VA },
        { DefaultClefType::Bass8va,       ClefType::F_8VA },
        { DefaultClefType::Blank,         ClefType::INVALID },
        { DefaultClefType::Tab1,          ClefType::TAB },
        { DefaultClefType::Tab2,          ClefType::TAB },
    };
    return muse::value(defaultClefTypeTable, DefaultClefType(clef), ClefType::INVALID);
}

String FinaleTConv::instrTemplateIdfromUuid(std::string uuid)
{
    // keep in sync with 'id' property of https://docs.google.com/spreadsheets/d/1SwqZb8lq5rfv5regPSA10drWjUAoi65EuMoYtG-4k5s/edit
    // todo: Add (sensible) defaults: woodwinds-end
    // todo: Detect midi program
    static const std::unordered_map<std::string_view, String> uuidTable = {
        // General
        { uuid::BlankStaff,                u"piano" }, // 'sensible' different default
        { uuid::GrandStaff,                u"piano" }, //
        { uuid::Unknown,                   u"piano" }, //

        // Strings
        { uuid::Violin,                    u"violin" },
        { uuid::Viola,                     u"viola" },
        { uuid::Cello,                     u"violoncello" },
        { uuid::DoubleBass,                u"contrabass" },
        { uuid::ViolinSection,             u"violins" },
        { uuid::ViolaSection,              u"violas" },
        { uuid::CelloSection,              u"violoncellos" },
        { uuid::VioloncelloSection,        u"violoncellos" },
        { uuid::DoubleBassSection,         u"contrabasses" },
        { uuid::ContrabassSection,         u"contrabasses" },
        { uuid::StringEnsemble,            u"strings" },
        { uuid::ViolaDAmore,               u"violoncello" }, //
        { uuid::Ajaeng,                    u"erhu" }, //
        { uuid::Arpeggione,                u"viola-da-gamba" }, //
        { uuid::Baryton,                   u"baryton" },
        { uuid::ByzantineLyra,             u"violin" }, //
        { uuid::CretanLyra,                u"violin" }, //
        { uuid::Crwth,                     u"violoncello" }, //
        { uuid::Dahu,                      u"erhu" }, //
        { uuid::Dangao,                    u"violin" }, //
        { uuid::Dihu,                      u"erhu" }, //
        { uuid::Erhu,                      u"erhu" },
        { uuid::Erxian,                    u"erhu" }, //
        { uuid::Fiddle,                    u"violin" }, //
        { uuid::Gaohu,                     u"erhu" }, //
        { uuid::Gehu,                      u"violoncello" }, //
        { uuid::Haegeum,                   u"erhu" }, //
        { uuid::HardangerFiddle,           u"violin" }, //
        { uuid::HurdyGurdy,                u"violin" }, //
        { uuid::Igil,                      u"violin" }, //
        { uuid::Kamancha,                  u"violin" }, //
        { uuid::Kokyu,                     u"violin" }, //
        { uuid::Kora,                      u"lute" }, //
        { uuid::LaruAn,                    u"violoncello" }, //
        { uuid::Leiqin,                    u"erhu" }, //
        { uuid::Lirone,                    u"viola-da-gamba" }, //
        { uuid::MorinKhuur,                u"violin" }, //
        { uuid::Nyckelharpa,               u"nyckelharpa" },
        { uuid::Octobass,                  u"octobass" },
        { uuid::Rebab,                     u"violin" }, //
        { uuid::Rebec,                     u"viola-da-gamba" }, //
        { uuid::Sarangi,                   u"violin" }, //
        { uuid::SarangiDrone,              u"violin" }, //
        { uuid::StrohViolin,               u"violin" }, //
        { uuid::Trombamarina,              u"violoncello" }, //
        { uuid::Vielle,                    u"viola" }, //
        { uuid::Viol,                      u"viola-da-gamba" }, //
        { uuid::ViolaDaGamba,              u"viola-da-gamba" },
        { uuid::ViolinoPiccolo,            u"violin" }, //
        { uuid::VioloncelloPiccolo,        u"violoncello" }, //
        { uuid::Violotta,                  u"violoncello" }, //
        { uuid::Zhonghu,                   u"erhu" }, //

        // Keyboards
        { uuid::Piano,                     u"piano" },
        { uuid::PianoNoName,               u"piano" },
        { uuid::Harpsichord,               u"harpsichord" },
        { uuid::Organ,                     u"organ" },
        { uuid::Organ2Staff,               u"organ" },
        { uuid::Celesta,                   u"celesta" },
        { uuid::Accordion,                 u"accordion" },
        { uuid::Melodica,                  u"melodica" },
        { uuid::ElectricPiano,             u"electric-piano" },
        { uuid::Clavinet,                  u"clavinet" },
        { uuid::SynthPad,                  u"pad-synth" },
        { uuid::SynthLead,                 u"saw-synth" }, //
        { uuid::SynthBrass,                u"brass-synthesizer" },
        { uuid::SynthSoundtrack,           u"soundtrack-synth" },
        { uuid::SoundFX,                   u"piano" }, //
        { uuid::Harmonium,                 u"harmonium" },
        { uuid::OndesMartenot,             u"ondes-martenot" },
        { uuid::Theremin,                  u"theremin" },
        { uuid::Virginal,                  u"virginal" },
        { uuid::Clavichord,                u"clavichord" },

        // Voices
        { uuid::SopranoVoice,              u"soprano" }, // todo, account for u"soprano-c-clef", same for alt-baritone and mezzo-soprano
        { uuid::AltoVoice,                 u"alto" },
        { uuid::TenorVoice,                u"tenor" },
        { uuid::BaritoneVoice,             u"baritone" },
        { uuid::BassVoice,                 u"bass" },
        { uuid::Vocals,                    u"voice" }, //
        { uuid::Voice,                     u"voice" },
        { uuid::VoiceNoName,               u"voice" },
        { uuid::MezzoSopranoVoice,         u"mezzo-soprano" },
        { uuid::ContraltoVoice,            u"contralto" },
        { uuid::CountertenorVoice,         u"countertenor" },
        { uuid::BassBaritoneVoice,         u"bass" }, //
        { uuid::ChoirAahs,                 u"voice" }, //
        { uuid::ChoirOohs,                 u"voice" }, //
        { uuid::Yodel,                     u"voice" }, //
        { uuid::Beatbox,                   u"voice" }, //
        { uuid::Kazoo,                     u"kazoo" },
        { uuid::Talkbox,                   u"voice" },
        { uuid::VocalPercussion,           u"voice" }, //

        // Woodwinds
        { uuid::Piccolo,                   u"piccolo" },
        { uuid::Flute,                     u"flute" },
        { uuid::AltoFlute,                 u"alto-flute" },
        { uuid::Oboe,                      u"oboe" },
        { uuid::OboeDAmore,                u"oboe-d'amore" },
        { uuid::EnglishHorn,               u"english-horn" },
        { uuid::ClarinetBFlat,             u"bb-clarinet" },
        { uuid::ClarinetA,                 u"a-clarinet" },
        { uuid::ClarinetEFlat,             u"eb-clarinet" },
        { uuid::AltoClarinet,              u"alto-clarinet" },
        { uuid::ContraltoClarinet,         u"contra-alto-clarinet" },
        { uuid::BassClarinet,              u"bass-clarinet" },
        { uuid::ContrabassClarinet,        u"contrabass-clarinet" },
        { uuid::Bassoon,                   u"bassoon" },
        { uuid::Contrabassoon,             u"contrbassoon" },
        { uuid::WindSection,               u"winds" },
        { uuid::SopranoSax,                u"soprano-saxophone" },
        { uuid::AltoSax,                   u"alto-saxophone" },
        { uuid::TenorSax,                  u"tenor-saxophone" },
        { uuid::BaritoneSax,               u"baritone-saxophone" },
        { uuid::SopranoRecorder,           u"soprano-recorder" },
        { uuid::SopraninoRecorder,         u"sopranino-recorder" },
        { uuid::AltoRecorder,              u"alto-recorder" },
        { uuid::TenorRecorder,             u"tenor-recorder" },
        { uuid::BassRecorder,              u"bass-recorder" },
        { uuid::DescantRecorder,           u"soprano-recorder" }, //
        { uuid::Ocarina,                   u"ocarina" },
        { uuid::PennyWhistle,              u"c-tin-whistle" }, //
        { uuid::PennyWhistleD,             u"d-tin-whistle" }, //
        { uuid::PennyWhistleG,             u"c-tin-whistle" }, //
        { uuid::LowIrishWhistle,           u"c-tin-whistle" }, //
        { uuid::TinWhistleBFlat,           u"bflat-tin-whistle" },
        { uuid::Harmonica,                 u"harmonica" },
        { uuid::BassHarmonica,             u"bass-harmonica" },
        { uuid::Concertina,                u"concertina" },
        { uuid::Bandoneon,                 u"bandoneon" },
        { uuid::HornF_WWQuintet,           u"horn" }, //
        { uuid::Bagpipes,                  u"bagpipe" },
        { uuid::UilleannPipes,             u"bagpipe" }, //
        { uuid::GaidaPipes,                u"bagpipe" }, //
        { uuid::ContraAltoFlute,           u"contra-alto-flute" },
        { uuid::BassFlute,                 u"bass-flute" },
        { uuid::ContrabassFlute,           u"contrabass-flute" },
        { uuid::DoubleContrabassFlute,     u"double-contrabass-flute" },
        { uuid::HyperbassFlute,            u"hyperbass-flute" },
        { uuid::PanPipes,                  u"pan-flute" },
        { uuid::Fife,                      u"fife" },
        { uuid::BottleBlow,                u"flute" }, //
        { uuid::Jug,                       u"flute" }, //
        { uuid::PiccoloOboe,               u"piccolo-oboe" },
        { uuid::PiccoloHeckelphone,        u"piccolo-heckelphone" },
        { uuid::Heckelphone,               u"heckelphone" },
        { uuid::BassOboe,                  u"bass-oboe" },
        { uuid::BassetClarinet,            u"basset-clarinet" },
        { uuid::BassetHorn,                u"basset-horn" },
        { uuid::Hornpipe,                  u"english-horn" }, //
        { uuid::PiccoloClarinet,           u"piccolo-clarinet" },
        { uuid::Saxonette,                 u"c-clarinet" }, //
        { uuid::SopraninoSax,              u"sopranino-saxophone" },
        { uuid::MezzoSopranoSax,           u"mezzo-soprano-saxophone" },
        { uuid::Sopranino,                 u"sopranino-saxophone" }, //
        { uuid::CMelodySax,                u"melody-saxophone" },
        { uuid::Aulochrome,                u"aulochrome" },
        { uuid::Xaphoon,                   u"xaphoon" },
        { uuid::BassSax,                   u"bass-saxophone" },
        { uuid::ContrabassSax,             u"contrabass-saxophone" },
        { uuid::SubContrabassSax,          u"subcontrabass-saxophone" },
        { uuid::Tubax,                     u"contrabass-saxophone" }, //
        { uuid::Bansuri,                   u"flute" }, //
        { uuid::Danso,                     u"danso" },
        { uuid::Dizi,                      u"g-dizi" },
        { uuid::DilliKaval,                u"flute" }, //
        { uuid::Diple,                     u"flute" }, //
        { uuid::DoubleFlute,               u"flute" }, //
        { uuid::Dvojnice,                  u"flute" }, //
        { uuid::DvojniceDrone,             u"flute" }, //
        { uuid::Flageolet,                 u"flageolet" },
        { uuid::Fujara,                    u"contrabass-flute" }, //
        { uuid::Gemshorn,                  u"gemshorn" },
        { uuid::Hocchiku,                  u"shakuhachi" }, //
        { uuid::Hun,                       u"flute" }, //
        { uuid::IrishFlute,                u"irish-flute" },
        { uuid::Kaval,                     u"flute" }, //
        { uuid::Khlui,                     u"flute" }, //
        { uuid::KnotweedFlute,             u"flute" }, //
        { uuid::KoncovkaAltoFlute,         u"alto-flute" }, //
        { uuid::Koudi,                     u"flute" }, //
        { uuid::Ney,                       u"flute" }, //
        { uuid::Nohkan,                    u"flute" }, //
        { uuid::NoseFlute,                 u"flute" }, //
        { uuid::Palendag,                  u"flute" }, //
        { uuid::Quena,                     u"flute" }, //
        { uuid::Ryuteki,                   u"flute" }, //
        { uuid::Shakuhachi,                u"shakuhachi" },
        { uuid::ShepherdsPipe,             u"flute" },
        { uuid::Shinobue,                  u"flute" },
        { uuid::ShivaWhistle,              u"flute" },
        { uuid::Shvi,                      u"flute" },
        { uuid::Suling,                    u"flute" },
        { uuid::Tarka,                     u"flute" },
        { uuid::TenorOvertoneFlute,        u"flute" },
        { uuid::Tumpong,                   u"flute" },
        { uuid::Venu,                      u"flute" },
        { uuid::Xiao,                      u"flute" },
        { uuid::Xun,                       u"flute" },
        { uuid::Albogue,                   u"flute" },
        { uuid::Alboka,                    u"flute" },
        { uuid::AltoCrumhorn,              u"flute" },
        { uuid::Arghul,                    u"flute" },
        { uuid::Bawu,                      u"flute" },
        { uuid::Chalumeau,                 u"flute" },
        { uuid::ClarinetteDAmour,          u"flute" },
        { uuid::Cornamuse,                 u"flute" },
        { uuid::Diplica,                   u"flute" },
        { uuid::DoubleClarinet,            u"flute" },
        { uuid::HeckelClarina,             u"flute" },
        { uuid::HeckelphoneClarinet,       u"flute" },
        { uuid::Hirtenschalmei,            u"flute" },
        { uuid::Launeddas,                 u"flute" },
        { uuid::Maqrunah,                  u"flute" },
        { uuid::Mijwiz,                    u"flute" },
        { uuid::Octavin,                   u"flute" },
        { uuid::Pibgorn,                   u"flute" },
        { uuid::Rauschpfeife,              u"flute" },
        { uuid::Sipsi,                     u"flute" },
        { uuid::ModernTarogato,            u"flute" },
        { uuid::TenorCrumhorn,             u"flute" },
        { uuid::Zhaleika,                  u"flute" },
        { uuid::Algaita,                   u"flute" },
        { uuid::Bifora,                    u"flute" },
        { uuid::Bombarde,                  u"flute" },
        { uuid::Cromorne,                  u"flute" },
        { uuid::Duduk,                     u"flute" },
        { uuid::Dulcian,                   u"flute" },
        { uuid::Dulzaina,                  u"flute" },
        { uuid::Guan,                      u"flute" },
        { uuid::Guanzi,                    u"flute" },
        { uuid::Hichiriki,                 u"flute" },
        { uuid::Hne,                       u"flute" },
        { uuid::JogiBaja,                  u"flute" },
        { uuid::KenBau,                    u"flute" },
        { uuid::Mizmar,                    u"flute" },
        { uuid::Nadaswaram,                u"flute" },
        { uuid::OboeDaCaccia,              u"flute" },
        { uuid::Pi,                        u"flute" },
        { uuid::Piri,                      u"flute" },
        { uuid::PungiSnakeCharmer,         u"flute" },
        { uuid::Rackett,                   u"flute" },
        { uuid::ReedContrabass,            u"flute" },
        { uuid::Rhaita,                    u"flute" },
        { uuid::Rothphone,                 u"flute" },
        { uuid::Sarrusophone,              u"flute" },
        { uuid::Shawm,                     u"flute" },
        { uuid::Shehnai,                   u"flute" },
        { uuid::Sopila,                    u"flute" },
        { uuid::Sorna,                     u"flute" },
        { uuid::Sralai,                    u"flute" },
        { uuid::Suona,                     u"flute" },
        { uuid::Surnay,                    u"flute" },
        { uuid::Taepyeongso,               u"flute" },
        { uuid::AncientTarogato,           u"flute" },
        { uuid::TrompetaChina,             u"flute" },
        { uuid::Zurla,                     u"flute" },
        { uuid::Zurna,                     u"flute" },
        { uuid::KhaenMouthOrgan,           u"flute" },
        { uuid::Hulusi,                    u"flute" },
        { uuid::Sheng,                     u"flute" },

        // Brass
        { uuid::TrumpetBFlat,              u"trumpet" },
        { uuid::TrumpetC,                  u"trumpet" },
        { uuid::TrumpetD,                  u"trumpet" },
        { uuid::Cornet,                    u"trumpet" },
        { uuid::Flugelhorn,                u"trumpet" },
        { uuid::Mellophone,                u"trumpet" },
        { uuid::HornF,                     u"trumpet" },
        { uuid::Trombone,                  u"trumpet" },
        { uuid::BassTrombone,              u"trumpet" },
        { uuid::Euphonium,                 u"trumpet" },
        { uuid::BaritoneBC,                u"trumpet" },
        { uuid::BaritoneTC,                u"trumpet" },
        { uuid::Tuba,                      u"trumpet" },
        { uuid::BassTuba,                  u"trumpet" },
        { uuid::Sousaphone,                u"trumpet" },
        { uuid::BrassSection,              u"trumpet" },
        { uuid::PiccoloTrumpetA,           u"trumpet" },
        { uuid::Bugle,                     u"trumpet" },
        { uuid::CornetEFlat,               u"trumpet" },
        { uuid::HornEFlat,                 u"trumpet" },
        { uuid::AltoTrombone,              u"trumpet" },
        { uuid::TenorTrombone,             u"trumpet" },
        { uuid::ContrabassTrombone,        u"trumpet" },
        { uuid::Alphorn,                   u"trumpet" },
        { uuid::AltoHorn,                  u"trumpet" },
        { uuid::Didgeridoo,                u"trumpet" },
        { uuid::PostHorn,                  u"trumpet" },
        { uuid::ViennaHorn,                u"trumpet" },
        { uuid::WagnerTuba,                u"trumpet" },
        { uuid::BaroqueTrumpet,            u"trumpet" },
        { uuid::BassTrumpet,               u"trumpet" },
        { uuid::Cornetto,                  u"trumpet" },
        { uuid::Fiscorn,                   u"trumpet" },
        { uuid::Kuhlohorn,                 u"trumpet" },
        { uuid::PocketTrumpet,             u"trumpet" },
        { uuid::Saxhorn,                   u"trumpet" },
        { uuid::SlideTrumpet,              u"trumpet" },
        { uuid::Cimbasso,                  u"trumpet" },
        { uuid::DoubleBellEuphonium,       u"trumpet" },
        { uuid::Sackbut,                   u"trumpet" },
        { uuid::Helicon,                   u"trumpet" },
        { uuid::Ophicleide,                u"trumpet" },
        { uuid::Serpent,                   u"trumpet" },
        { uuid::SubContrabassTuba,         u"trumpet" },
        { uuid::ConchShell,                u"trumpet" },
        { uuid::Horagai,                   u"trumpet" },
        { uuid::Shofar,                    u"trumpet" },
        { uuid::Vuvuzela,                  u"trumpet" },

        // Plucked Strings
        { uuid::Harp,                      u"guitar-steel" },
        { uuid::TroubadorHarp,             u"guitar-steel" },
        { uuid::Guitar,                    u"guitar-steel" },
        { uuid::Guitar8vb,                 u"guitar-steel" },
        { uuid::AcousticGuitar,            u"guitar-steel" },
        { uuid::ClassicalGuitar,           u"guitar-steel" },
        { uuid::ElectricGuitar,            u"guitar-steel" },
        { uuid::SteelGuitar,               u"guitar-steel" },
        { uuid::Banjo,                     u"guitar-steel" },
        { uuid::TenorBanjo,                u"guitar-steel" },
        { uuid::AcousticBass,              u"guitar-steel" },
        { uuid::BassGuitar,                u"guitar-steel" },
        { uuid::ElectricBass,              u"guitar-steel" },
        { uuid::FretlessBass,              u"guitar-steel" },
        { uuid::StringBass,                u"guitar-steel" },
        { uuid::Mandolin,                  u"guitar-steel" },
        { uuid::Dulcimer,                  u"guitar-steel" },
        { uuid::HammeredDulcimer,          u"guitar-steel" },
        { uuid::Dulcimer8vb,               u"guitar-steel" },
        { uuid::Autoharp,                  u"guitar-steel" },
        { uuid::Lute,                      u"guitar-steel" },
        { uuid::Ukulele,                   u"guitar-steel" },
        { uuid::TenorUkulele,              u"guitar-steel" },
        { uuid::Sitar,                     u"guitar-steel" },
        { uuid::Zither,                    u"guitar-steel" },
        { uuid::Archlute,                  u"guitar-steel" },
        { uuid::Baglama,                   u"guitar-steel" },
        { uuid::Balalaika,                 u"guitar-steel" },
        { uuid::Bandura,                   u"guitar-steel" },
        { uuid::Banjolele,                 u"guitar-steel" },
        { uuid::Barbat,                    u"guitar-steel" },
        { uuid::Begena,                    u"guitar-steel" },
        { uuid::Biwa,                      u"guitar-steel" },
        { uuid::Bolon,                     u"guitar-steel" },
        { uuid::Bordonua,                  u"guitar-steel" },
        { uuid::Bouzouki,                  u"guitar-steel" },
        { uuid::BulgarianTambura,          u"guitar-steel" },
        { uuid::ChapmanStick,              u"guitar-steel" },
        { uuid::Charango,                  u"guitar-steel" },
        { uuid::ChitarraBattente,          u"guitar-steel" },
        { uuid::ChaozhouGuzheng,           u"guitar-steel" },
        { uuid::Cimbalom,                  u"guitar-steel" },
        { uuid::Cittern,                   u"guitar-steel" },
        { uuid::Cuatro,                    u"guitar-steel" },
        { uuid::DanBau,                    u"guitar-steel" },
        { uuid::DanNguyet,                 u"guitar-steel" },
        { uuid::DanTamThapLuc,             u"guitar-steel" },
        { uuid::DanTranh,                  u"guitar-steel" },
        { uuid::DanTyBa,                   u"guitar-steel" },
        { uuid::DiddleyBow,                u"guitar-steel" },
        { uuid::Dobro,                     u"guitar-steel" },
        { uuid::Domra,                     u"guitar-steel" },
        { uuid::Dutar,                     u"guitar-steel" },
        { uuid::Duxianqin,                 u"guitar-steel" },
        { uuid::Ektara1,                   u"guitar-steel" },
        { uuid::FlamencoGuitar,            u"guitar-steel" },
        { uuid::Geomungo,                  u"guitar-steel" },
        { uuid::Ektara2,                   u"guitar-steel" },
        { uuid::Gottuvadhyam,              u"guitar-steel" },
        { uuid::GuitarraQuintaHuapanguera, u"guitar-steel" },
        { uuid::Guitarron,                 u"guitar-steel" },
        { uuid::Guitjo,                    u"guitar-steel" },
        { uuid::GuitjoDoubleNeck,          u"guitar-steel" },
        { uuid::Guqin,                     u"guitar-steel" },
        { uuid::Guzheng,                   u"guitar-steel" },
        { uuid::HarpGuitar,                u"guitar-steel" },
        { uuid::IrishBouzouki,             u"guitar-steel" },
        { uuid::JaranaHuasteca,            u"guitar-steel" },
        { uuid::JaranaJarocho,             u"guitar-steel" },
        { uuid::JaranaMosquito,            u"guitar-steel" },
        { uuid::JaranaSegunda,             u"guitar-steel" },
        { uuid::JaranaTercera,             u"guitar-steel" },
        { uuid::Kabosy,                    u"guitar-steel" },
        { uuid::Kantele,                   u"guitar-steel" },
        { uuid::Kayagum,                   u"guitar-steel" },
        { uuid::Khim,                      u"guitar-steel" },
        { uuid::Kobza,                     u"guitar-steel" },
        { uuid::Komuz,                     u"guitar-steel" },
        { uuid::Koto,                      u"guitar-steel" },
        { uuid::Kutiyapi,                  u"guitar-steel" },
        { uuid::Langeleik,                 u"guitar-steel" },
        { uuid::Lyre,                      u"guitar-steel" },
        { uuid::MandoBass,                 u"guitar-steel" },
        { uuid::MandoCello,                u"guitar-steel" },
        { uuid::Mandola,                   u"guitar-steel" },
        { uuid::Mandora,                   u"guitar-steel" },
        { uuid::Mandore,                   u"guitar-steel" },
        { uuid::Mangbetu,                  u"guitar-steel" },
        { uuid::Marovany,                  u"guitar-steel" },
        { uuid::MohanVeena,                u"guitar-steel" },
        { uuid::MoodSwinger,               u"guitar-steel" },
        { uuid::MusicalBow,                u"guitar-steel" },
        { uuid::Ngoni,                     u"guitar-steel" },
        { uuid::OctaveMandolin,            u"guitar-steel" },
        { uuid::Oud,                       u"guitar-steel" },
        { uuid::Pipa,                      u"guitar-steel" },
        { uuid::PortugueseGuitar,          u"guitar-steel" },
        { uuid::Psaltery,                  u"guitar-steel" },
        { uuid::RequintoGuitar,            u"guitar-steel" },
        { uuid::Ruan,                      u"guitar-steel" },
        { uuid::RudraVeena,                u"guitar-steel" },
        { uuid::Sallaneh,                  u"guitar-steel" },
        { uuid::Sanshin,                   u"guitar-steel" },
        { uuid::Santoor,                   u"guitar-steel" },
        { uuid::Sanxian,                   u"guitar-steel" },
        { uuid::Sarod,                     u"guitar-steel" },
        { uuid::Saung,                     u"guitar-steel" },
        { uuid::Saz,                       u"guitar-steel" },
        { uuid::Se,                        u"guitar-steel" },
        { uuid::Setar,                     u"guitar-steel" },
        { uuid::Shamisen,                  u"guitar-steel" },
        { uuid::Tambura,                   u"guitar-steel" },
        { uuid::TarPlucked,                u"guitar-steel" },
        { uuid::Theorbo,                   u"guitar-steel" },
        { uuid::Timple,                    u"guitar-steel" },
        { uuid::Tres,                      u"guitar-steel" },
        { uuid::Tsymbaly,                  u"guitar-steel" },
        { uuid::Valiha,                    u"guitar-steel" },
        { uuid::Veena,                     u"guitar-steel" },
        { uuid::VichitraVeena,             u"guitar-steel" },
        { uuid::VihuelaMexico,             u"guitar-steel" },
        { uuid::VihuelaSpain,              u"guitar-steel" },
        { uuid::WashtubBass,               u"guitar-steel" },
        { uuid::Whamola,                   u"guitar-steel" },
        { uuid::Xalam,                     u"guitar-steel" },
        { uuid::Yangqin,                   u"guitar-steel" },
        { uuid::Yazheng,                   u"guitar-steel" },
        { uuid::Yueqin,                    u"guitar-steel" },

        // Tablature
        { uuid::TabGuitar,                 u"guitar-steel" },
        { uuid::TabGuitarNoName,           u"guitar-steel" },
        { uuid::TabGuitarStems,            u"guitar-steel" },
        { uuid::TabGuitarD,                u"guitar-steel" },
        { uuid::TabGuitarDADGAD,           u"guitar-steel" },
        { uuid::TabGuitarDoubled,          u"guitar-steel" },
        { uuid::TabGuitarDropD,            u"guitar-steel" },
        { uuid::TabGuitarG,                u"guitar-steel" },
        { uuid::TabGuitar7String,          u"guitar-steel" },
        { uuid::TabBanjoG,                 u"guitar-steel" },
        { uuid::TabTenorBanjo,             u"guitar-steel" },
        { uuid::TabBanjoC,                 u"guitar-steel" },
        { uuid::TabBanjoD,                 u"guitar-steel" },
        { uuid::TabBanjoDoubleC,           u"guitar-steel" },
        { uuid::TabBanjoGModal,            u"guitar-steel" },
        { uuid::TabBanjoPlectrum,          u"guitar-steel" },
        { uuid::TabBassGuitar4,            u"guitar-steel" },
        { uuid::TabBassGuitar5,            u"guitar-steel" },
        { uuid::TabBassGuitar6,            u"guitar-steel" },
        { uuid::TabDulcimerDAA,            u"guitar-steel" },
        { uuid::TabDulcimerDAAUnison,      u"guitar-steel" },
        { uuid::TabDulcimerDAD,            u"guitar-steel" },
        { uuid::TabGamba,                  u"guitar-steel" },
        { uuid::TabLuteItalian,            u"guitar-steel" },
        { uuid::TabLuteLetters,            u"guitar-steel" },
        { uuid::TabMandolin,               u"guitar-steel" },
        { uuid::TabRequinto,               u"guitar-steel" },
        { uuid::TabSitarShankar,           u"guitar-steel" },
        { uuid::TabSitarKhan,              u"guitar-steel" },
        { uuid::TabUkulele,                u"guitar-steel" },
        { uuid::TabVihuela,                u"guitar-steel" },

        // Pitched Percussion
        { uuid::Timpani,                   u"piano" },
        { uuid::Mallets,                   u"piano" },
        { uuid::Bells,                     u"piano" },
        { uuid::Chimes,                    u"piano" },
        { uuid::Crotales,                  u"piano" },
        { uuid::Glockenspiel,              u"piano" },
        { uuid::SopranoGlockenspiel,       u"piano" },
        { uuid::AltoGlockenspiel,          u"piano" },
        { uuid::Marimba,                   u"piano" },
        { uuid::BassMarimba,               u"piano" },
        { uuid::MarimbaSingleStaff,        u"piano" },
        { uuid::TubularBells,              u"piano" },
        { uuid::Vibraphone,                u"piano" },
        { uuid::Xylophone,                 u"piano" },
        { uuid::SopranoXylophone,          u"piano" },
        { uuid::AltoXylophone,             u"piano" },
        { uuid::BassXylophone,             u"piano" },
        { uuid::Xylorimba,                 u"piano" },
        { uuid::BellLyre,                  u"piano" },
        { uuid::Boomwhackers,              u"piano" },
        { uuid::ChromanotesInstruments,    u"piano" },
        { uuid::Carillon,                  u"piano" },
        { uuid::CrystalGlasses,            u"piano" },
        { uuid::FlexatonePitched,          u"piano" },
        { uuid::GlassHarmonica,            u"piano" },
        { uuid::GlassMarimba,              u"piano" },
        { uuid::Handbells,                 u"piano" },
        { uuid::HandbellsTClef,            u"piano" },
        { uuid::HandbellsBClef,            u"piano" },
        { uuid::HangTClef,                 u"piano" },
        { uuid::JawHarp,                   u"piano" },
        { uuid::Kalimba,                   u"piano" },
        { uuid::SopranoMetallophone,       u"piano" },
        { uuid::AltoMetallophone,          u"piano" },
        { uuid::BassMetallophone,          u"piano" },
        { uuid::MusicalSaw,                u"piano" },
        { uuid::SlideWhistle,              u"piano" },
        { uuid::SteelDrumsTClef,           u"piano" },
        { uuid::SteelDrumsBClef,           u"piano" },
        { uuid::BonangGamelan,             u"piano" },
        { uuid::GansaGamelan,              u"piano" },
        { uuid::GenderGamelan,             u"piano" },
        { uuid::GiyingGamelan,             u"piano" },
        { uuid::KantilGamelan,             u"piano" },
        { uuid::PelogPanerusGamelan,       u"piano" },
        { uuid::PemadeGamelan,             u"piano" },
        { uuid::PenyacahGamelan,           u"piano" },
        { uuid::SaronBarungGamelan,        u"piano" },
        { uuid::SaronDemongGamelan,        u"piano" },
        { uuid::SaronPanerusGamelan,       u"piano" },
        { uuid::SlendroPanerusGamelan,     u"piano" },
        { uuid::SlenthemGamelan,           u"piano" },
        { uuid::Almglocken,                u"piano" },
        { uuid::Angklung,                  u"piano" },
        { uuid::ArrayMbira,                u"piano" },
        { uuid::Balafon,                   u"piano" },
        { uuid::Balaphon,                  u"piano" },
        { uuid::Bianqing,                  u"piano" },
        { uuid::Bianzhong,                 u"piano" },
        { uuid::Fangxiang,                 u"piano" },
        { uuid::GandinganAKayo,            u"piano" },
        { uuid::Gyil,                      u"piano" },
        { uuid::Kubing,                    u"piano" },
        { uuid::Kulintang,                 u"piano" },
        { uuid::KulintangAKayo,            u"piano" },
        { uuid::KulintangATiniok,          u"piano" },
        { uuid::Lamellaphone,              u"piano" },
        { uuid::Likembe,                   u"piano" },
        { uuid::Luntang,                   u"piano" },
        { uuid::Mbira,                     u"piano" },
        { uuid::Murchang,                  u"piano" },
        { uuid::RanatEklek,                u"piano" },
        { uuid::RanatThumLek,              u"piano" },
        { uuid::Sanza,                     u"piano" },
        { uuid::TaikoDrums,                u"piano" },
        { uuid::TempleBells,               u"piano" },
        { uuid::TibetanBells,              u"piano" },
        { uuid::TibetanSingingBowls,       u"piano" },

        // Drums
        { uuid::SnareDrum,                 u"snare-drum" },
        { uuid::BassDrum,                  u"snare-drum" },
        { uuid::DrumSet,                   u"snare-drum" },
        { uuid::TenorDrum,                 u"snare-drum" },
        { uuid::QuadToms,                  u"snare-drum" },
        { uuid::QuintToms,                 u"snare-drum" },
        { uuid::RotoToms,                  u"snare-drum" },
        { uuid::TenorLine,                 u"snare-drum" },
        { uuid::SnareLine,                 u"snare-drum" },
        { uuid::BassDrums5Line,            u"snare-drum" },
        { uuid::Djembe,                    u"snare-drum" },
        { uuid::BongoDrums,                u"snare-drum" },
        { uuid::CongaDrums,                u"snare-drum" },
        { uuid::LogDrum,                   u"snare-drum" },
        { uuid::Tablas,                    u"snare-drum" },
        { uuid::Timbales,                  u"snare-drum" },
        { uuid::AfricanLogDrum,            u"snare-drum" },
        { uuid::Apentemma,                 u"snare-drum" },
        { uuid::ArabianFrameDrum,          u"snare-drum" },
        { uuid::Ashiko,                    u"snare-drum" },
        { uuid::Atabaque,                  u"snare-drum" },
        { uuid::Bata,                      u"snare-drum" },
        { uuid::Bendir,                    u"snare-drum" },
        { uuid::Bodhran,                   u"snare-drum" },
        { uuid::Bombo,                     u"snare-drum" },
        { uuid::Bougarabou,                u"snare-drum" },
        { uuid::BuffaloDrum,               u"snare-drum" },
        { uuid::Chenda,                    u"snare-drum" },
        { uuid::Chudaiko,                  u"snare-drum" },
        { uuid::Dabakan,                   u"snare-drum" },
        { uuid::Daibyosi,                  u"snare-drum" },
        { uuid::Damroo,                    u"snare-drum" },
        { uuid::Darabuka,                  u"snare-drum" },
        { uuid::DatangulionDrum,           u"snare-drum" },
        { uuid::Dhol,                      u"snare-drum" },
        { uuid::Dholak,                    u"snare-drum" },
        { uuid::Dollu,                     u"snare-drum" },
        { uuid::Dondo,                     u"snare-drum" },
        { uuid::Doundounba,                u"snare-drum" },
        { uuid::Duff,                      u"snare-drum" },
        { uuid::Dumbek,                    u"snare-drum" },
        { uuid::EweDrumKagan,              u"snare-drum" },
        { uuid::EweDrumKpanlogo1Large,     u"snare-drum" },
        { uuid::EweDrumKpanlogo2Medium,    u"snare-drum" },
        { uuid::EweDrumKpanlogo3Combo,     u"snare-drum" },
        { uuid::EweDrumSogo,               u"snare-drum" },
        { uuid::Fontomfrom,                u"snare-drum" },
        { uuid::Geduk,                     u"snare-drum" },
        { uuid::HandDrum,                  u"snare-drum" },
        { uuid::Hiradaiko,                 u"snare-drum" },
        { uuid::Igihumurizo,               u"snare-drum" },
        { uuid::Ingoma,                    u"snare-drum" },
        { uuid::Inyahura,                  u"snare-drum" },
        { uuid::Janggu,                    u"snare-drum" },
        { uuid::Kakko,                     u"snare-drum" },
        { uuid::Kanjira,                   u"snare-drum" },
        { uuid::KendangGamelan,            u"snare-drum" },
        { uuid::Kenkeni,                   u"snare-drum" },
        { uuid::Khol,                      u"snare-drum" },
        { uuid::Kodaiko,                   u"snare-drum" },
        { uuid::Kudum,                     u"snare-drum" },
        { uuid::LambegDrum,                u"snare-drum" },
        { uuid::Madal,                     u"snare-drum" },
        { uuid::Maddale,                   u"snare-drum" },
        { uuid::MoroccoDrum,               u"snare-drum" },
        { uuid::Mridangam,                 u"snare-drum" },
        { uuid::Naal,                      u"snare-drum" },
        { uuid::NagaDodaiko,               u"snare-drum" },
        { uuid::Nagara,                    u"snare-drum" },
        { uuid::Naqara,                    u"snare-drum" },
        { uuid::NativeLogDrum,             u"snare-drum" },
        { uuid::NigerianLogDrum,           u"snare-drum" },
        { uuid::Odaiko,                    u"snare-drum" },
        { uuid::Okawa,                     u"snare-drum" },
        { uuid::OkedoDodaiko,              u"snare-drum" },
        { uuid::PahuHula,                  u"snare-drum" },
        { uuid::Pakhavaj,                  u"snare-drum" },
        { uuid::Pandero,                   u"snare-drum" },
        { uuid::PowwowDrum,                u"snare-drum" },
        { uuid::PuebloDrum,                u"snare-drum" },
        { uuid::Repinique,                 u"snare-drum" },
        { uuid::Sabar,                     u"snare-drum" },
        { uuid::Sakara,                    u"snare-drum" },
        { uuid::Sampho,                    u"snare-drum" },
        { uuid::Sangban,                   u"snare-drum" },
        { uuid::ShimeDaiko,                u"snare-drum" },
        { uuid::Surdo,                     u"snare-drum" },
        { uuid::TalkingDrum,               u"snare-drum" },
        { uuid::Tama,                      u"snare-drum" },
        { uuid::Tamborita,                 u"snare-drum" },
        { uuid::Tamte,                     u"snare-drum" },
        { uuid::Tantan,                    u"snare-drum" },
        { uuid::Tangku,                    u"snare-drum" },
        { uuid::Taphon,                    u"snare-drum" },
        { uuid::TarDrum,                   u"snare-drum" },
        { uuid::Tasha,                     u"snare-drum" },
        { uuid::Thavil,                    u"snare-drum" },
        { uuid::Tombak,                    u"snare-drum" },
        { uuid::Tumbak,                    u"snare-drum" },
        { uuid::Tsuzumi,                   u"snare-drum" },
        { uuid::UchiwaDaiko,               u"snare-drum" },
        { uuid::Udaku,                     u"snare-drum" },
        { uuid::Zarb,                      u"snare-drum" },

        // Percussion
        { uuid::PercussionGeneral,         u"percussion" },
        { uuid::PercAccessories,           u"percussion" },
        { uuid::WindChimes,                u"percussion" },
        { uuid::ChimeTree,                 u"percussion" },
        { uuid::BellTree,                  u"percussion" },
        { uuid::JingleBells,               u"percussion" },
        { uuid::Tambourine,                u"percussion" },
        { uuid::Triangle,                  u"percussion" },
        { uuid::Cymbals,                   u"percussion" },
        { uuid::FingerCymbals,             u"percussion" },
        { uuid::CrashCymbal,               u"percussion" },
        { uuid::HiHatCymbal,               u"percussion" },
        { uuid::RideCymbal,                u"percussion" },
        { uuid::SplashCymbal,              u"percussion" },
        { uuid::TamTam,                    u"percussion" },
        { uuid::Gong,                      u"percussion" },
        { uuid::AgogoBells,                u"percussion" },
        { uuid::AirHorn,                   u"percussion" },
        { uuid::BrakeDrum,                 u"percussion" },
        { uuid::Cabasa,                    u"percussion" },
        { uuid::Cajon,                     u"percussion" },
        { uuid::Castanets,                 u"percussion" },
        { uuid::Clap,                      u"percussion" },
        { uuid::Clapper,                   u"percussion" },
        { uuid::Claves,                    u"percussion" },
        { uuid::Cowbell,                   u"percussion" },
        { uuid::Cuica,                     u"percussion" },
        { uuid::Guiro,                     u"percussion" },
        { uuid::Maracas,                   u"percussion" },
        { uuid::PoliceWhistle,             u"percussion" },
        { uuid::Rainstick,                 u"percussion" },
        { uuid::Ratchet,                   u"percussion" },
        { uuid::Rattle,                    u"percussion" },
        { uuid::SandBlock,                 u"percussion" },
        { uuid::Shakers,                   u"percussion" },
        { uuid::Spoons,                    u"percussion" },
        { uuid::TempleBlocks,              u"percussion" },
        { uuid::Vibraslap,                 u"percussion" },
        { uuid::Washboard,                 u"percussion" },
        { uuid::Whip,                      u"percussion" },
        { uuid::WindMachine,               u"percussion" },
        { uuid::WoodBlocks,                u"percussion" },
        { uuid::CengCengGamelan,           u"percussion" },
        { uuid::GongAgengGamelan,          u"percussion" },
        { uuid::KempulGamelan,             u"percussion" },
        { uuid::KempyangGamelan,           u"percussion" },
        { uuid::KenongGamelan,             u"percussion" },
        { uuid::KetukGamelan,              u"percussion" },
        { uuid::ReyongGamelan,             u"percussion" },
        { uuid::Adodo,                     u"percussion" },
        { uuid::AeolianHarp,               u"percussion" },
        { uuid::Afoxe,                     u"percussion" },
        { uuid::AgogoBlock,                u"percussion" },
        { uuid::Agung,                     u"percussion" },
        { uuid::AgungAtamLang,             u"percussion" },
        { uuid::Ahoko,                     u"percussion" },
        { uuid::Babendil,                  u"percussion" },
        { uuid::BasicIndianPercussion,     u"percussion" },
        { uuid::Berimbau,                  u"percussion" },
        { uuid::Bo,                        u"percussion" },
        { uuid::Bones,                     u"percussion" },
        { uuid::BongoBells,                u"percussion" },
        { uuid::Bullroarer,                u"percussion" },
        { uuid::Caxixi,                    u"percussion" },
        { uuid::ChachaBells,               u"percussion" },
        { uuid::Chabara,                   u"percussion" },
        { uuid::Chanchiki,                 u"percussion" },
        { uuid::Chimta,                    u"percussion" },
        { uuid::ChinaTempleBlocks,         u"percussion" },
        { uuid::ChineseCymbals,            u"percussion" },
        { uuid::ChineseGongs,              u"percussion" },
        { uuid::ChinesePercussionEnsemble, u"percussion" },
        { uuid::Ching,                     u"percussion" },
        { uuid::Chippli,                   u"percussion" },
        { uuid::Daff,                      u"percussion" },
        { uuid::Dafli,                     u"percussion" },
        { uuid::Dawuro,                    u"percussion" },
        { uuid::Def,                       u"percussion" },
        { uuid::Doira,                     u"percussion" },
        { uuid::EweDrumAtoke,              u"percussion" },
        { uuid::EweDrumAxatse,             u"percussion" },
        { uuid::EweDrumGangokui,           u"percussion" },
        { uuid::FlexatonePerc,             u"percussion" },
        { uuid::Gandingan,                 u"percussion" },
        { uuid::Ganza,                     u"percussion" },
        { uuid::Ghatam,                    u"percussion" },
        { uuid::Ghungroo,                  u"percussion" },
        { uuid::Gome,                      u"percussion" },
        { uuid::Guban,                     u"percussion" },
        { uuid::HandCymbal,                u"percussion" },
        { uuid::Hang,                      u"percussion" },
        { uuid::Hatheli,                   u"percussion" },
        { uuid::Hosho,                     u"percussion" },
        { uuid::Hyoushigi,                 u"percussion" },
        { uuid::Ibo,                       u"percussion" },
        { uuid::IndianGong,                u"percussion" },
        { uuid::Ipu,                       u"percussion" },
        { uuid::Jawbone,                   u"percussion" },
        { uuid::KaEkeEke,                  u"percussion" },
        { uuid::Kagul,                     u"percussion" },
        { uuid::Kalaau,                    u"percussion" },
        { uuid::Kashiklar,                 u"percussion" },
        { uuid::Kesi,                      u"percussion" },
        { uuid::Khartal,                   u"percussion" },
        { uuid::Kkwaenggwari,              u"percussion" },
        { uuid::Kpokopoko,                 u"percussion" },
        { uuid::KrinSlitDrum,              u"percussion" },
        { uuid::LavaStones,                u"percussion" },
        { uuid::LuoGong,                   u"percussion" },
        { uuid::Manjeera,                  u"percussion" },
        { uuid::PanClappers,               u"percussion" },
        { uuid::Patschen,                  u"percussion" },
        { uuid::RattleCog,                 u"percussion" },
        { uuid::Riq,                       u"percussion" },
        { uuid::Shekere,                   u"percussion" },
        { uuid::Sistre,                    u"percussion" },
        { uuid::Sistrum,                   u"percussion" },
        { uuid::SlideWhistlePercClef,      u"percussion" },
        { uuid::SlitDrum,                  u"percussion" },
        { uuid::Snap,                      u"percussion" },
        { uuid::Stamp,                     u"percussion" },
        { uuid::StirDrum,                  u"percussion" },
        { uuid::TebYoshi,                  u"percussion" },
        { uuid::Televi,                    u"percussion" },
        { uuid::Teponaztli,                u"percussion" },
        { uuid::ThaiGong,                  u"percussion" },
        { uuid::TibetanCymbals,            u"percussion" },
        { uuid::TicTocBlock,               u"percussion" },
        { uuid::TimbaleBell,               u"percussion" },
        { uuid::Tinaja,                    u"percussion" },
        { uuid::Tingsha,                   u"percussion" },
        { uuid::Toere,                     u"percussion" },
        { uuid::ToneTang,                  u"percussion" },
        { uuid::Trychel,                   u"percussion" },
        { uuid::Udu,                       u"percussion" },
        { uuid::Zills,                     u"percussion" },
    };
    // todo: different fallback for unpitched percussion
    return muse::value(uuidTable, uuid, u"piano");
}

BracketType FinaleTConv::toMuseScoreBracketType(details::StaffGroup::BracketStyle style)
{
    using MusxBracketStyle = details::StaffGroup::BracketStyle;
    static const std::unordered_map<MusxBracketStyle, BracketType> bracketTypeTable = {
        { MusxBracketStyle::None,                 BracketType::NO_BRACKET },
        { MusxBracketStyle::ThickLine,            BracketType::LINE },
        { MusxBracketStyle::BracketStraightHooks, BracketType::NORMAL },
        { MusxBracketStyle::PianoBrace,           BracketType::BRACE },
        { MusxBracketStyle::BracketCurvedHooks,   BracketType::NORMAL },
        { MusxBracketStyle::DeskBracket,          BracketType::SQUARE },
    };
    return muse::value(bracketTypeTable, style, BracketType::NO_BRACKET);
}

TupletNumberType FinaleTConv::toMuseScoreTupletNumberType(options::TupletOptions::NumberStyle numberStyle)
{
    using MusxTupletNumberType = options::TupletOptions::NumberStyle;
    static const std::unordered_map<MusxTupletNumberType, TupletNumberType> tupletNumberTypeTable = {
        { MusxTupletNumberType::Nothing,                  TupletNumberType::NO_TEXT },
        { MusxTupletNumberType::Number,                   TupletNumberType::SHOW_NUMBER },
        { MusxTupletNumberType::UseRatio,                 TupletNumberType::SHOW_RELATION },
        { MusxTupletNumberType::RatioPlusDenominatorNote, TupletNumberType::SHOW_RELATION }, // not supported
        { MusxTupletNumberType::RatioPlusBothNotes,       TupletNumberType::SHOW_RELATION }, // not supported
    };
    return muse::value(tupletNumberTypeTable, numberStyle, TupletNumberType::SHOW_NUMBER);
}

Align FinaleTConv::justifyToAlignment(others::NamePositioning::AlignJustify alignJustify)
{
    static const std::unordered_map<others::NamePositioning::AlignJustify, Align> alignTable = {
        { others::NamePositioning::AlignJustify::Left,   Align(AlignH::LEFT, AlignV::VCENTER) },
        { others::NamePositioning::AlignJustify::Right,  Align(AlignH::RIGHT, AlignV::VCENTER) },
        { others::NamePositioning::AlignJustify::Center, Align(AlignH::HCENTER, AlignV::VCENTER) },
    };
    return muse::value(alignTable, alignJustify, Align(AlignH::HCENTER, AlignV::VCENTER));
}

CourtesyBarlineMode FinaleTConv::boolToCourtesyBarlineMode(bool useDoubleBarlines)
{
    static const std::unordered_map<bool, CourtesyBarlineMode> courtesyBarlineModeTable = {
        { false, CourtesyBarlineMode::ALWAYS_SINGLE },
        { true,  CourtesyBarlineMode::ALWAYS_DOUBLE },
    };
    return muse::value(courtesyBarlineModeTable, useDoubleBarlines, CourtesyBarlineMode::DOUBLE_BEFORE_COURTESY);
}

NoteVal FinaleTConv::notePropertiesToNoteVal(const musx::dom::Note::NoteProperties& noteProperties, Key key)
{
    auto [noteType, octave, alteration, staffLine] = noteProperties;
    NoteVal nval;
    int absStep = 35 /*middle C*/ + int(noteType) + (octave - 4) * STEP_DELTA_OCTAVE;
    nval.pitch = absStep2pitchByKey(absStep, Key::C) + alteration; //assume EDO division is semitone
    if (alteration < int(AccidentalVal::MIN) || alteration > int(AccidentalVal::MAX) || !pitchIsValid(nval.pitch)) {
        nval.pitch = std::clamp(nval.pitch, 0, 127);
        nval.tpc1 = pitch2tpc(nval.pitch, key, Prefer::NEAREST);
    } else {
        nval.tpc1 = step2tpc(int(noteType), AccidentalVal(alteration));
    }
    return nval;
}

Fraction FinaleTConv::musxFractionToFraction(const musx::util::Fraction& fraction)
{
    // unlike with time signatures, remainder does not need to be accounted for
    return Fraction(fraction.numerator(), fraction.denominator());
}

Fraction FinaleTConv::eduToFraction(Edu edu)
{
    return musxFractionToFraction(musx::util::Fraction::fromEdu(edu));
}

Fraction FinaleTConv::simpleMusxTimeSigToFraction(const std::pair<musx::util::Fraction, musx::dom::NoteType>& simpleMusxTimeSig, FinaleLoggerPtr& logger)
{
    auto [count, noteType] = simpleMusxTimeSig;
    if (count.remainder()) {
        if ((Edu(noteType) % count.denominator()) == 0) {
            noteType = musx::dom::NoteType(Edu(noteType) / count.denominator());
            count *= count.denominator();
        } else {
            logger->logWarning(String(u"Time signature has fractional portion that could not be reduced."));
            return Fraction(4, 4);
        }
    }
    return Fraction(count.quotient(),  musx::util::Fraction::fromEdu(Edu(noteType)).denominator());
}

Key FinaleTConv::keyFromAlteration(int musxAlteration)
{
    return Key(musxAlteration);
}

KeyMode FinaleTConv::keyModeFromDiatonicMode(music_theory::DiatonicMode diatonicMode)
{
    using DiatonicMode = music_theory::DiatonicMode;
    static const std::unordered_map<DiatonicMode, KeyMode> keyModeTypeTable = {
        { DiatonicMode::Ionian,             KeyMode::IONIAN },
        { DiatonicMode::Dorian,             KeyMode::DORIAN },
        { DiatonicMode::Phrygian,           KeyMode::PHRYGIAN },
        { DiatonicMode::Lydian,             KeyMode::LYDIAN },
        { DiatonicMode::Mixolydian,         KeyMode::MIXOLYDIAN },
        { DiatonicMode::Aeolian,            KeyMode::AEOLIAN },
        { DiatonicMode::Locrian,            KeyMode::LOCRIAN },
    };
    return muse::value(keyModeTypeTable, diatonicMode, KeyMode::UNKNOWN);
}

SymId FinaleTConv::acciSymbolFromAcciAmount(int acciAmount)
{
    /// @todo add support for microtonal symbols (will require access to musx KeySignature instance)
    /// This code assumes each chromatic halfstep is 1 EDO division, but we cannot make that assumption
    /// with microtonal symbols.
    AccidentalType at = Accidental::value2subtype(AccidentalVal(acciAmount));
    return at != AccidentalType::NONE ? Accidental::subtype2symbol(at) : SymId::noSym;
}

StaffGroup FinaleTConv::staffGroupFromNotationStyle(musx::dom::others::Staff::NotationStyle notationStyle)
{
    using NotationStyle = musx::dom::others::Staff::NotationStyle;
    static const std::unordered_map<NotationStyle, StaffGroup> staffGroupMapTable = {
        { NotationStyle::Standard,          StaffGroup::STANDARD },
        { NotationStyle::Percussion,        StaffGroup::PERCUSSION },
        { NotationStyle::Tablature,         StaffGroup::TAB },
    };
    return muse::value(staffGroupMapTable, notationStyle, StaffGroup::STANDARD);

}

double FinaleTConv::doubleFromEvpu(Evpu evpu)
{
    return double(evpu) / EVPU_PER_SPACE;
}

PointF FinaleTConv::evpuToPointF(Evpu xEvpu, Evpu yEvpu)
{
    return PointF(doubleFromEvpu(xEvpu), doubleFromEvpu(yEvpu));
}

double FinaleTConv::doubleFromEfix(Efix efix)
{
    return double(efix) / EFIX_PER_SPACE;
}

String FinaleTConv::metaTagFromFileInfo(texts::FileInfoText::TextType textType)
{
    using TextType = texts::FileInfoText::TextType;
    static const std::unordered_map<TextType, String> metaTagTable = {
        { TextType::Title,       u"workTitle" },
        { TextType::Composer,    u"composer" },
        { TextType::Copyright,   u"copyright" },
        { TextType::Description, u"description" }, // created by Finale importer
        { TextType::Lyricist,    u"lyricist" },
        { TextType::Arranger,    u"arranger" },
        { TextType::Subtitle,    u"subtitle" },
    };
    return muse::value(metaTagTable, textType, String());
}

String FinaleTConv::metaTagFromTextComponent(const std::string& component)
{
    static const std::unordered_map<std::string_view, String> metaTagTable = {
        { "title",       u"workTitle" },
        { "composer",    u"composer" },
        { "copyright",   u"copyright" },
        { "description", u"description" }, // created by Finale importer
        { "lyricist",    u"lyricist" },
        { "arranger",    u"arranger" },
        { "subtitle",    u"subtitle" },
    };
    return muse::value(metaTagTable, component, String());
}

double FinaleTConv::doubleFromPercent(int percent)
{
    return double(percent) / 100.0;
}

double FinaleTConv::spatiumScaledFontSize(const std::shared_ptr<FontInfo>& fontInfo)
{
    // Finale uses music font size 24 to fill a space.
    // MuseScore uses music font size 20 to fill a space.
    // This scaling carries over to any font setting whose font size scales with spatium.
    constexpr static double MUSE_FINALE_SCALE_DIFFERENTIAL = 20.0 / 24.0;

    return double(fontInfo->fontSize) * (fontInfo->absolute ? 1.0 : MUSE_FINALE_SCALE_DIFFERENTIAL);
}

}
