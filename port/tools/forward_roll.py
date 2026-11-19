#!/usr/bin/env python3
"""Writes No Remorse's static/fwdroll.dat from No Regret's data: the
Silencer's forward roll (animation 5 of shape 1 in anim.dat), which No
Remorse's anim.dat has not, and the frames of shape 1 it is drawn with, which
No Remorse's shape has not, and the weapon overlays drawn over it (No
Regret's wpnovlay.dat's for the animation, of each overlay type: the two
games' overlay frames are numbered the same) (common/src/modern/imported.h
reads it). The frames
become the shape's past No Remorse's own (3960 on), the animation's frames
numbered so; the two games' palettes are the same, so the frames' bytes are
kept as they are.

usage: forward_roll.py NO_REGRET_DIR NO_REMORSE_DIR"""
import argparse
import struct
import sys
from pathlib import Path

SHAPE = 1
ANIM = 5


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
    roll = animation(static_file(args.regret, 'anim.dat'), SHAPE, ANIM)
    if roll is None:
        sys.exit('forward_roll: No Regret has no forward roll')
    if animation(static_file(args.remorse, 'anim.dat'), SHAPE, ANIM) is not None:
        sys.exit('forward_roll: No Remorse has a forward roll already')
    info, entries = roll
    regret = shape_frames(static_file(args.regret, 'shapes.flx'), SHAPE)
    first = len(shape_frames(static_file(args.remorse, 'shapes.flx'), SHAPE))
    used = sorted({struct.unpack_from('<H', e)[0] & 0xfff for e in entries})
    if first + len(used) > 0x1000:
        sys.exit('forward_roll: the frames do not fit the animation frames\' 12 bits')
    number = {frame: first + i for i, frame in enumerate(used)}
    renumbered = []
    for entry in entries:
        word = struct.unpack_from('<H', entry)[0]
        word = (word & ~0xfff) | number[word & 0xfff]
        renumbered.append(struct.pack('<H', word) + entry[2:])
    out = bytearray(b'FWDR')
    out += struct.pack('<HHIH', SHAPE, ANIM, info, len(renumbered))
    out += b''.join(renumbered)
    out += struct.pack('<HH', first, len(used))
    out += b''.join(struct.pack('<I', len(regret[f])) for f in used)
    out += b''.join(regret[f] for f in used)
    directions = 16 if (info >> 30) & 1 else 8
    table = overlays(static_file(args.regret, 'wpnovlay.dat'), ANIM)
    per_type = (info & 0xff) * directions * 4
    if per_type == 0 or len(table) % per_type != 0:
        sys.exit("forward_roll: the overlays are not the animation's")
    out += b'OVLY' + struct.pack('<H', len(table) // per_type) + table
    target = static_file(args.remorse, 'anim.dat').with_name('FWDROLL.DAT')
    target.write_bytes(out)
    print(f'forward_roll: {target}: {len(used)} frames from {first}, {len(renumbered)} animation frames')


if __name__ == '__main__':
    main()
