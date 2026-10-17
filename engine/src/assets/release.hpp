#pragma once
// A release's files around its archive. `--data` may be a folder: the Android release's
// main.<version>.com.square_enix.chaosrings2gp.obb (an MDB1 archive itself) or a CRDB.*.mvgl in
// it. The Android release keeps what the archive lacks in its APK, beside the OBB: the maps'
// table in lib/armeabi-v7a/libChaosRings2.so, the fonts as assets/00000 (Bold) and 00001
// (Regular) - read out of the APK (a zip), or from an unpacked copy of it.

#include <cstdint>
#include <string>
#include <vector>

#include "assets/maptable.hpp"

namespace cr2 {

// the archive `path` names: itself, or the one in that folder (the OBB first)
std::string resolveArchive(const std::string& path);

// a zip's entry (stored, or deflated: with zlib)
bool readZipEntry(const std::string& zip, const std::string& name, std::vector<uint8_t>& out, std::string* error = nullptr);

// `name` (a path inside the APK) of the Android release beside `archivePath`: from an unpacked
// APK in its folder (the file itself, or under extracted/), else from an .apk there that holds
// it. `from`: where it was found
bool androidFile(const std::string& archivePath, const std::string& name, std::vector<uint8_t>& out, std::string* from = nullptr);

// The maps' table for the archive's release: `binary` if given, else the iPad's executable or
// the Vita's eboot.bin beside the archive (mapTableCandidates), else the Android release's library
bool releaseMapTable(const std::string& archivePath, const std::string& binary, std::vector<MapInfo>& table,
                     std::string* from = nullptr);

} // namespace cr2
