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
#include "notationmnxreader.h"
#include "mnximporter.h"

#include "engraving/dom/masterscore.h"
#include "engraving/engravingerrors.h"
#include "io/file.h"

#include "mnxdom.h"

using namespace mu::iex::mnx;
using namespace mu::engraving;
using namespace muse;

Ret NotationMnxReader::read(MasterScore* score, const io::path_t& path, const Options&)
{
    io::File jsonFile(path);
    if (!jsonFile.exists()) {
        return make_ret(Err::FileNotFound, path);
    }

    if (!jsonFile.open(io::IODevice::ReadOnly)) {
        LOGE() << "could not open MNX file: " << path.toString();
        return make_ret(Err::FileOpenError, path);
    }

    ByteArray data = jsonFile.readAll();
    jsonFile.close();

    try {
        MnxImporter importer(score, ::mnx::Document::create(data.constData(), data.size()));
        data.clear();
        if (importer.mnxDocument().global().measures().empty()) {
            LOGE() << path << " contains no measures\n";
            return make_ret(Ret::Code::NotSupported, TranslatableString("importexport/mnx", "File contains no measures,").str);
        }
        importer.importMnx();
    } catch (const std::exception& ex) {
        LOGE() << String::fromStdString(ex.what()) << "\n";
        return make_ret(Ret::Code::InternalError);
    }

    return make_ok();
}
