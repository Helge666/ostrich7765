#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
#
# The install instructions, in one place: INSTALL.txt in each zip, and the release notes.
#
#   .github/release-text.sh windows|linux
#   .github/release-text.sh notes VERSION

set -euo pipefail

roms() {
	cat <<'EOF'
You need your own ROM images: the sound ROMs of the machines you want to play (TR-505,
TR-626, TR-707; the TR-727's are prepared). They are recognised by their contents, so the
file names do not matter. The list: roms/README.md in the source repository. No ROMs are
included, and none ever will be.
EOF
}

windows() {
	cat <<'EOF'
Ostrich 7765 for Windows (64-bit)

VST3: copy the folder Ostrich7765.vst3 to
  C:\Program Files\Common Files\VST3\
CLAP: copy Ostrich7765.clap to
  C:\Program Files\Common Files\CLAP\
Standalone: Ostrich7765.exe, anywhere.

ROMs go in
  C:\ProgramData\Ostrich 7765\roms\

EOF
	roms
}

linux() {
	cat <<'EOF'
Ostrich 7765 for Linux (x86-64)

VST3: copy the folder Ostrich7765.vst3 to ~/.vst3/
CLAP: copy Ostrich7765.clap to ~/.clap/
Standalone: Ostrich7765 (JACK), anywhere.

ROMs go in
  ~/.local/share/Ostrich 7765/roms/

EOF
	roms
}

notes() {
	local version="$1"
	cat <<EOF
Ostrich 7765 ${version}: the Roland TR-505, TR-626 and TR-707, played from their original
sound ROMs. The TR-727 is prepared, but does not play yet: no dump of its sound ROMs is
public.

| File | Platform | Contents |
|---|---|---|
| \`Ostrich7765-Windows.zip\` | Windows x86-64 | VST3, CLAP, standalone |
| \`Ostrich7765-Linux.zip\` | Linux x86-64 | VST3, CLAP, standalone (JACK) |

Each zip has an INSTALL.txt. You need your own ROM images; none are included.
EOF
}

case "${1:-}" in
	windows) windows ;;
	linux)   linux ;;
	notes)   notes "${2:?version}" ;;
	*) echo "usage: $0 windows|linux|notes VERSION" >&2; exit 2 ;;
esac
