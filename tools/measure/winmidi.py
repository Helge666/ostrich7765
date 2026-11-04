#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Minimal Windows MIDI out/in through winmm (no packages needed).
import ctypes
import ctypes.wintypes as wt
import time

winmm = ctypes.WinDLL("winmm")


class MIDIOUTCAPSW(ctypes.Structure):
    _fields_ = [("wMid", wt.WORD), ("wPid", wt.WORD), ("vDriverVersion", wt.UINT),
                ("szPname", wt.WCHAR * 32), ("wTechnology", wt.WORD), ("wVoices", wt.WORD),
                ("wNotes", wt.WORD), ("wChannelMask", wt.WORD), ("dwSupport", wt.DWORD)]


class MIDIINCAPSW(ctypes.Structure):
    _fields_ = [("wMid", wt.WORD), ("wPid", wt.WORD), ("vDriverVersion", wt.UINT),
                ("szPname", wt.WCHAR * 32), ("dwSupport", wt.DWORD)]


def outputs():
    res = []
    for i in range(winmm.midiOutGetNumDevs()):
        c = MIDIOUTCAPSW()
        winmm.midiOutGetDevCapsW(i, ctypes.byref(c), ctypes.sizeof(c))
        res.append(c.szPname)
    return res


def inputs():
    res = []
    for i in range(winmm.midiInGetNumDevs()):
        c = MIDIINCAPSW()
        winmm.midiInGetDevCapsW(i, ctypes.byref(c), ctypes.sizeof(c))
        res.append(c.szPname)
    return res


class Out:
    def __init__(self, name):
        names = outputs()
        idx = next(i for i, n in enumerate(names) if name.lower() in n.lower())
        self.h = wt.HANDLE()
        r = winmm.midiOutOpen(ctypes.byref(self.h), idx, 0, 0, 0)
        if r:
            raise OSError(f"midiOutOpen({names[idx]}) failed: {r}")
        self.name = names[idx]

    def send(self, *b):
        msg = b[0] | (b[1] << 8 if len(b) > 1 else 0) | (b[2] << 16 if len(b) > 2 else 0)
        winmm.midiOutShortMsg(self.h, msg)

    def note(self, ch, n, vel, length=0.1):
        self.send(0x90 | ch, n, vel)
        time.sleep(length)
        self.send(0x80 | ch, n, 0)

    def close(self):
        winmm.midiOutReset(self.h)
        winmm.midiOutClose(self.h)


if __name__ == "__main__":
    print("MIDI out:", outputs())
    print("MIDI in: ", inputs())
