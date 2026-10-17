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

// the archive `path` names: itself, or the one in that folder (the OBB first) - or in the
// release's own place under it: a Vita copy's PCSG00498 folder (CR2/resources/CRDB.psp2.mvgl, or
// its CR2 folder), an unpacked .ipa (Payload/ChaosRings2enHD.app/CRDB.ios.mvgl)
std::string resolveArchive(const std::string& path);

// With no --data: CR2_DATA, else a copy the build carries beside its executable (`baseDir`):
// vita/, android/ or ipad/ (each as resolveArchive takes a folder), else the folder itself.
// Empty: none found
std::string defaultArchive(const std::string& baseDir);

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

// The staff roll's lines (StaffrollMenu::AllTextTextureGeneration's table: 544 strings, then the
// four endings' figure names end2_BG, end1_BG, end3_BG, end4_BG) from the release's executable -
// found by those four names' pointers, whatever the executable's layout; false: not found
bool releaseStaffRoll(const std::string& archivePath, const std::string& binary, std::vector<std::string>& lines,
                      std::string figures[4] = nullptr);

} // namespace cr2
