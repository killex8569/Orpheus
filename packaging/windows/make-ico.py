#!/usr/bin/env python3
"""Régénère packaging/windows/orpheus.ico depuis icons/orpheus.svg (nécessite rsvg-convert).

Format ICO avec images PNG (Windows Vista et suivants), sans dépendance Python.
"""
import pathlib
import struct
import subprocess

here = pathlib.Path(__file__).resolve().parent
svg = here.parent.parent / "icons" / "orpheus.svg"
sizes = [16, 20, 24, 32, 40, 48, 64, 128, 256]

images = [
    subprocess.run(["rsvg-convert", "-w", str(s), "-h", str(s), str(svg)], check=True, capture_output=True).stdout
    for s in sizes
]
header = struct.pack("<HHH", 0, 1, len(sizes))
offset = len(header) + 16 * len(sizes)
entries = b""
for s, png in zip(sizes, images):
    dim = 0 if s >= 256 else s  # 0 signifie 256
    entries += struct.pack("<BBBBHHII", dim, dim, 0, 0, 1, 32, len(png), offset)
    offset += len(png)
(here / "orpheus.ico").write_bytes(header + entries + b"".join(images))
print(f"orpheus.ico : {len(sizes)} tailles")
