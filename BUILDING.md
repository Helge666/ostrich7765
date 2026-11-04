# Building Ostrich 7765

Everything builds on Linux (developed on Ubuntu 24.04). Windows binaries are cross-compiled
there with mingw-w64.

## 1. Packages

```sh
sudo apt-get install --no-install-recommends build-essential git pkg-config ccache \
    libjack-jackd2-dev libgl1-mesa-dev libx11-dev libxext-dev libxcursor-dev \
    libcairo2-dev libdbus-1-dev g++-mingw-w64-x86-64
```

`g++-mingw-w64-x86-64` brings two thread models, chosen with `update-alternatives`. Builds
here and in CI use **win32** (the default on Ubuntu 24.04): the binaries then need no mingw
runtime DLL. Objects from the two models must not be mixed.

## 2. Clone

```sh
git clone --recurse-submodules https://github.com/Helge666/ostrich7765.git
```

Two levels are needed: `plugin/dpf` and DPF's own `dgl/src/pugl-upstream`. After a clone
without `--recurse-submodules`, or after a pull that moves a submodule:

```sh
git submodule update --init --recursive
```

`git submodule status --recursive` shows a leading space on every line when all is in place.

## 3. Build

```sh
cd plugin/ostrich
make -j5          # Linux: VST3, CLAP, standalone (JACK)  -> plugin/bin/
make -j5 win      # Windows: VST3, CLAP, standalone       -> plugin/ostrich/bin-win/
```

The Windows binaries are statically linked and depend on nothing beyond Windows. The
standalone uses DPF's RtAudio backend on Windows and JACK on Linux.

The smoke test (as in CI) loads the Linux CLAP without a host, plays a few notes, saves
the state and writes the outputs to a WAV:

```sh
gcc -std=gnu11 -O2 -o plugin/tools/ostrich_clap_render plugin/tools/ostrich_clap_render.c \
    -Iplugin/dpf/distrho/src -ldl
plugin/tools/ostrich_clap_render plugin/bin/Ostrich7765.clap /tmp/out.wav
```

## 4. DPF

`plugin/dpf` is our fork of DPF, [Helge666/DPF](https://github.com/Helge666/DPF), branch
`logan`, pinned to a tag (`logan-1`, ...); DPF's own pugl submodule points at
[Helge666/pugl](https://github.com/Helge666/pugl). The changes we need are commits there,
not patches here: `plugin/dpf` stays clean. What the fork adds is listed in Ostrich 220's
BUILDING.md; Ostrich 7765 relies on these:

| Change | What it does |
|---|---|
| VST3: extra outputs off | the 16 mono outputs are main buses, off until the host switches them on |
| VST3: 16 MIDI channels | the MIDI input bus has 16 channels (Cubase folded all onto channel 1) |
| JACK, CLAP, VST3: DSP → UI state | `updateStateValue()` reaches the UI |
| VST3: size rounding, UI idle watchdog | the panel opens at the right size in Cubase and keeps redrawing |
| VST3: UTF-8 state | state strings (file paths) converted between UTF-8 and UTF-16 for real |

## 5. Versions

The version is in `plugin/ostrich/Version.hpp` and nowhere else; the panel and the plugin
formats take it from there. A release: raise it in a commit "Version x.y.z", tag that
commit `vx.y.z` and push the tag; CI checks that tag and header agree, builds, and
publishes the zips. A `preview-x.y.z` tag does the same without publishing.
