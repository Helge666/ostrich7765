# Third-party code

Ostrich 7765 as a whole is distributed under the GNU General Public License, version 3 or
later ([LICENSE](LICENSE)). It contains, or is built from, the following.

| Component | Where | Licence | Copyright |
|---|---|---|---|
| DPF | `plugin/dpf/` (submodule) | ISC | Filipe Coelho |
| pugl | `plugin/dpf/dgl/src/pugl-upstream` | ISC | David Robillard |
| NanoVG | `plugin/dpf/dgl/src/nanovg` | zlib | Mikko Mononen |
| stb_truetype, stb_image | `plugin/dpf/dgl/src/nanovg` | public domain / MIT | Sean Barrett |
| DejaVu Sans | the panel's font, built into DGL | Bitstream Vera / DejaVu licence (`plugin/dpf/dgl/src/resources/LICENSE-DejaVuSans.ttf.txt`) | Bitstream, DejaVu authors |
| RtAudio | the Windows standalone's audio, through DPF | MIT-style (RtAudio licence) | Gary P. Scavone |
| RtMidi | built into the standalone through DPF | MIT-style (RtMidi licence) | Gary P. Scavone |
| MAME's TR-707 sound board (`src/mame/roland/roland_tr707.cpp`), ported to C++ | `plugin/ostrich/engine/Tr707.cpp`, `Analog.hpp` | BSD-3-Clause (`plugin/ostrich/engine/LICENSE-MAME-BSD3.txt`) | AJR, m1macrophage |

The release zips carry these licence texts in their folder `licenses`, next to the GPL 3.0
(`LICENSE`); `.github/licenses.sh` puts them together.

No Roland sound ROM, firmware, manual or schematic is part of this project. "Roland",
"TR-505", "TR-626", "TR-707" and "TR-727" are trademarks of Roland Corporation; this
project is not affiliated with Roland.
