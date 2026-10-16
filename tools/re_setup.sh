#!/bin/bash
# Builds CR2-U's local reverse-engineering groundwork in re/ (never committed) from your own copies:
#   tools/re_setup.sh <ChaosRings2enHD.app> <unpacked Android APK> <Vita PCSG00498 folder>
# Needs llvm-objdump / llvm-nm / llvm-lipo and Ghidra (GHIDRA=/opt/ghidra by default). The Android
# build (2.1.2) exports its functions by name; the iPad's (2.1.0) and the Vita's are stripped and
# get Android's names by matching. Produces:
#   re/android/armv7/, re/android/x86/   libChaosRings2.so: symbols.txt, decomp.c, functions.tsv,
#                                        features.jsonl, ghidra/ (x86 decompiles best; armv7 matches)
#   re/android/dex/                      classes.dex (the Java shell)
#   re/ios/      ChaosRings2enHD.armv7 (the fat binary's armv7 slice), symbols, disasm, stubs,
#                names_from_android.tsv, decomp.c, functions.tsv, ghidra/
#   re/vita/     eboot.elf + imports.tsv (tools/vita_self.py), names_from_android.tsv, decomp.c, ghidra/
# Vita import names come from re/vita/nids_vita3k.tsv (NID <tab> name) when present.
set -e
[ $# -eq 3 ] || { sed -n 2,3p "$0"; exit 1; }
R="$(cd "$(dirname "$0")/.." && pwd)"; T="$R/tools"; G="${GHIDRA:-/opt/ghidra}/support/analyzeHeadless"
APP="$1"; APK="$2"; V="$3"
mkdir -p "$R/re/android/armv7/ghidra" "$R/re/android/x86/ghidra" "$R/re/android/dex/ghidra" "$R/re/ios/ghidra" "$R/re/vita/ghidra"

for abi in armeabi-v7a:armv7 x86:x86; do
    d="${abi%%:*}"; o="$R/re/android/${abi##*:}"; so="$APK/lib/$d/libChaosRings2.so"
    echo "ghidra: android $d"
    llvm-nm -D -n --defined-only "$so" > "$o/symbols.txt"
    "$G" "$o/ghidra" cr2_${abi##*:} -import "$so" -overwrite -scriptPath "$T/ghidra" \
        -postScript ExportDecomp.java "$o" -postScript ExportFeatures.java "$o/features.jsonl" > "$o/ghidra.log" 2>&1
done
echo "ghidra: android dex"
"$G" "$R/re/android/dex/ghidra" dex -import "$APK/classes.dex" -overwrite -scriptPath "$T/ghidra" \
    -postScript ExportDecomp.java "$R/re/android/dex" > "$R/re/android/dex/ghidra.log" 2>&1

echo "ghidra: iPad"
B="$R/re/ios/ChaosRings2enHD.armv7"
llvm-lipo -thin armv7 "$APP/ChaosRings2enHD" -output "$B"
llvm-nm -n "$B" > "$R/re/ios/symbols.txt"
llvm-objdump -d "$B" > "$R/re/ios/disasm.txt"
llvm-objdump --macho --indirect-symbols "$B" | awk '/^Indirect symbols for/ {s=$4} /^0x/ && s ~ /symbolstub/ {print $1, $3}' > "$R/re/ios/stubs.txt"
python3 -I "$T/annotate_stubs.py" "$R/re/ios/stubs.txt" "$R/re/ios/disasm.txt"
"$G" "$R/re/ios/ghidra" cr2ios -import "$B" -overwrite -scriptPath "$T/ghidra" \
    -postScript ExportFeatures.java "$R/re/ios/features.jsonl" > "$R/re/ios/ghidra.log" 2>&1
python3 -I "$T/ghidra/match_functions.py" "$R/re/android/armv7/features.jsonl" "$R/re/ios/features.jsonl" \
    "$R/re/ios/names_from_android.tsv" --cross > "$R/re/ios/match.log"
"$G" "$R/re/ios/ghidra" cr2ios -process ChaosRings2enHD.armv7 -noanalysis -scriptPath "$T/ghidra" \
    -postScript ApplyNames.java "$R/re/ios/names_from_android.tsv" "Android" -postScript ExportDecomp.java "$R/re/ios" > "$R/re/ios/apply.log" 2>&1

echo "ghidra: Vita"
NIDS=(); [ -f "$R/re/vita/nids_vita3k.tsv" ] && NIDS=("$R/re/vita/nids_vita3k.tsv")
python3 -I "$T/vita_self.py" "$V/eboot.bin" "$R/re/vita/eboot.elf" "$R/re/vita/imports.tsv" "${NIDS[@]}" > "$R/re/vita/self.log"
"$G" "$R/re/vita/ghidra" cr2vita -import "$R/re/vita/eboot.elf" -overwrite -scriptPath "$T/ghidra" \
    -preScript VitaSetup.java "$R/re/vita/imports.tsv" > "$R/re/vita/ghidra.log" 2>&1
"$G" "$R/re/vita/ghidra" cr2vita -process eboot.elf -scriptPath "$T/ghidra" -preScript VitaFindFunctions.java \
    -postScript ExportFeatures.java "$R/re/vita/features.jsonl" > "$R/re/vita/ghidra2.log" 2>&1
python3 -I "$T/ghidra/match_functions.py" "$R/re/android/armv7/features.jsonl" "$R/re/vita/features.jsonl" \
    "$R/re/vita/names_from_android.tsv" --cross > "$R/re/vita/match.log"
"$G" "$R/re/vita/ghidra" cr2vita -process eboot.elf -noanalysis -scriptPath "$T/ghidra" \
    -postScript ApplyNames.java "$R/re/vita/names_from_android.tsv" "Android" -postScript ExportDecomp.java "$R/re/vita" > "$R/re/vita/apply.log" 2>&1
grep -h "^matched" "$R/re/ios/match.log" "$R/re/vita/match.log"
echo "done: re/android, re/ios, re/vita"
