#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
#
# The licence texts the binaries' parts ask to travel with them (THIRD-PARTY.md), into a
# folder of a release zip.
#
#   .github/licenses.sh <folder>

set -euo pipefail
cd "$(dirname "$0")/.."
out="${1:?folder}"
mkdir -p "$out"

cp plugin/dpf/LICENSE "$out/DPF-ISC.txt"
cp plugin/dpf/dgl/src/pugl-upstream/COPYING "$out/pugl-ISC.txt"
cp plugin/dpf/dgl/src/nanovg/LICENSE.txt "$out/NanoVG-zlib.txt"
cp plugin/dpf/dgl/src/resources/LICENSE-DejaVuSans.ttf.txt "$out/DejaVuSans.txt"
cp plugin/dpf/distrho/src/jackbridge/rtaudio/LICENSE "$out/RtAudio.txt"
cp plugin/dpf/distrho/src/jackbridge/rtmidi/LICENSE "$out/RtMidi.txt"
