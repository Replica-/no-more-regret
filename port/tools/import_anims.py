#!/usr/bin/env python3
"""Writes No Remorse's static/imported.dat from No Regret's data: animations
of the Silencer (shape 1 in anim.dat) which No Remorse's anim.dat has not, the
forward roll (5) and the kneeling steps to the right and left (1bh, 35h), and
the frames of shape 1 they are drawn with, which No Remorse's shape has not,
and the weapon overlays drawn over them (No Regret's wpnovlay.dat's for each
animation, of each overlay type: the two games' overlay frames are numbered
the same) (common/src/modern/imported.h reads it). The frames become the
shape's past No Remorse's own (3960 on), the animations' frames numbered so:
past 4095, the 12 bits of an animation frame's number have the next bit
(unknown0d, which No Remorse's own frames never set) for a 13th. The two
games' palettes are the same, so the frames' bytes are kept as they are.

usage: import_anims.py NO_REGRET_DIR NO_REMORSE_DIR"""
import argparse
import struct
import sys
from pathlib import Path

SHAPE = 1
ANIMS = [5, 0x1b, 0x35]
HIGH_BIT = 0x2000


def static_file(game, name):
    """A file of the game's static folder, whatever its case."""
    folder = next(p for p in Path(game).iterdir() if p.name.lower() == 'static')
    return next(p for p in folder.iterdir() if p.name.lower() == name)


def shape_frames(path, shape):
    """The frames' bytes of shape in a shape file (a flex)."""
    with open(path, 'rb') as f:
        f.seek(0x80 + 8 * shape)
        offset, size = struct.unpack('<II', f.read(8))
        f.seek(offset)
        data = f.read(size)
    count = struct.unpack_from('<H', data, 4)[0]
    frames = []
    for i in range(count):
        start = struct.unpack_from('<I', data, 6 + 8 * i)[0] & 0xffffff
        length = struct.unpack_from('<I', data, 10 + 8 * i)[0]
        frames.append(data[start:start + length])
    return frames


def overlays(path, anim):
    """The weapon overlays of the animation in a wpnovlay.dat (a flex): each
    overlay type's, one after the other."""
    data = Path(path).read_bytes()
    offset, size = struct.unpack_from('<II', data, 0x80 + 8 * anim)
    return data[offset:offset + size]


def animation(path, shape, anim):
    """The animation's header and its frames (8 bytes each), None when the
    shape has none of that number."""
    data = Path(path).read_bytes()
    table = struct.unpack_from('<i', data, shape * 4)[0]
    if table == 0:
        return None
    offset = struct.unpack_from('<256i', data, table)[anim]
    if offset == 0:
        return None
    info = struct.unpack_from('<I', data, offset)[0]
    directions = 16 if (info >> 30) & 1 else 8
    entries = [data[offset + 4 + 8 * i:offset + 12 + 8 * i] for i in range((info & 0xff) * directions)]
    return info, entries


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument('regret')
    parser.add_argument('remorse')
    args = parser.parse_args()
    anims = {}
    for anim in ANIMS:
        found = animation(static_file(args.regret, 'anim.dat'), SHAPE, anim)
        if found is None:
            sys.exit(f'import_anims: No Regret has no animation {anim:#x}')
        if animation(static_file(args.remorse, 'anim.dat'), SHAPE, anim) is not None:
            sys.exit(f'import_anims: No Remorse has animation {anim:#x} already')
        if any(struct.unpack_from('<H', e)[0] & HIGH_BIT for e in found[1]):
            sys.exit(f'import_anims: animation {anim:#x} sets the bit its frames\' 13th takes')
        anims[anim] = found
    regret = shape_frames(static_file(args.regret, 'shapes.flx'), SHAPE)
    first = len(shape_frames(static_file(args.remorse, 'shapes.flx'), SHAPE))
    used = sorted({struct.unpack_from('<H', e)[0] & 0xfff for _, entries in anims.values() for e in entries})
    if first + len(used) > 0x2000:
        sys.exit('import_anims: the frames do not fit the animation frames\' 13 bits')
    number = {frame: first + i for i, frame in enumerate(used)}
    out = bytearray(b'IMPA')
    out += struct.pack('<HH', SHAPE, len(anims))
    for anim, (info, entries) in anims.items():
        renumbered = []
        for entry in entries:
            word = struct.unpack_from('<H', entry)[0]
            new = number[word & 0xfff]
            word = (word & ~(0xfff | HIGH_BIT)) | (new & 0xfff) | (HIGH_BIT if new & 0x1000 else 0)
            renumbered.append(struct.pack('<H', word) + entry[2:])
        out += struct.pack('<HIH', anim, info, len(renumbered))
        out += b''.join(renumbered)
        table = overlays(static_file(args.regret, 'wpnovlay.dat'), anim)
        per_type = len(renumbered) * 4
        if len(table) % per_type != 0:
            sys.exit(f"import_anims: the overlays are not animation {anim:#x}'s")
        out += struct.pack('<H', len(table) // per_type) + table
    out += struct.pack('<HH', first, len(used))
    out += b''.join(struct.pack('<I', len(regret[f])) for f in used)
    out += b''.join(regret[f] for f in used)
    target = static_file(args.remorse, 'anim.dat').with_name('IMPORTED.DAT')
    target.write_bytes(out)
    print(f'import_anims: {target}: {len(anims)} animations, {len(used)} frames from {first}')


if __name__ == '__main__':
    main()
