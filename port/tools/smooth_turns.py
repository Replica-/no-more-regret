#!/usr/bin/env python3
"""Writes No Remorse's static/turns.dat: the Silencer's stands as he aims
(animations of shape 1 with a frame a direction) in 64 directions, not 16:
three frames between each two of his directions, made by RIFE
(rife-ncnn-vulkan, a frame interpolator, on the graphics card) from his 16
round in turn (common/src/modern/turns.h reads it).

Each animation's 16 frames are drawn on one canvas, their origins at one
point, on the frames' own mean colour, four times as large; RIFE makes the
frames between them round the loop, and the outline (white on black)
apart; each frame made is scaled back, cut out by its outline and given the
palette's nearest of the colours his 16 frames have, and encoded as the
shape file's frames are (drawframe.h).

turns.dat: "TURN", the shape, the first frame's number, the count of
animations; each animation's number and its 16 directions' 3 frames'
numbers (u16 each); the count of frames, each one's size and its bytes,
as the shape file holds a frame (8 bytes, then its ShapeFrame).

usage: smooth_turns.py GAME_DIR RIFE_EXE [--work DIR]"""
import argparse
import shutil
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

import numpy
from PIL import Image

SHAPE = 1
# The stands he aims in: with a small weapon, a large one, kneeling with
# each (each a frame a direction, 16 directions).
ANIMS = [0x04, 0x25, 0x2e, 0x2f]
DIRECTIONS = 16
BETWEEN = 4
FIRST = 6000
UP = 4


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


def animation_frames(path, shape, anim):
    """The animation's frame of each direction, None unless it has one frame
    a direction in 16 directions, none of them mirrored."""
    data = Path(path).read_bytes()
    table = struct.unpack_from('<i', data, shape * 4)[0]
    offset = struct.unpack_from('<256i', data, table)[anim]
    info = struct.unpack_from('<I', data, offset)[0]
    if info & 0xff != 1 or not (info >> 30) & 1:
        return None
    words = [struct.unpack_from('<H', data, offset + 4 + 8 * i)[0] for i in range(DIRECTIONS)]
    if any(w & 0x8000 for w in words):
        return None
    return [w & 0xfff for w in words]


def decode(frame):
    """A frame's palette entries (-1 where it has none) and its origin."""
    frame = frame[8:]
    word = lambda at: struct.unpack_from('<i', frame, at)[0]
    flags, width, height, x_off, y_off = word(0), word(4), word(8), word(12), word(16)
    out = numpy.full((height, width), -1, numpy.int32)
    for row in range(height):
        entry = 0x14 + 4 * row
        p = entry + word(entry)
        column = 0
        while column < width:
            column += frame[p]
            p += 1
            if column >= width:
                break
            length = frame[p]
            p += 1
            fill = False
            if flags & 1:
                fill = length & 1
                length >>= 1
            for c in range(column, min(column + length, width)):
                out[row, c] = frame[p] if fill else frame[p + c - column]
            p += 1 if fill else length
            column += length
    return out, x_off, y_off


def encode(entries, x_off, y_off):
    """Palette entries (-1 for none) as the shape file holds a frame: 8
    bytes for the cache's FrameBlock, the header (no fills), a row's offset
    each (from its own entry) and the rows' runs: a skip, a length, the
    pixels, up to a skip to the end of the row."""
    height, width = entries.shape
    rows = []
    for row in range(height):
        line = entries[row]
        out = bytearray()
        column = 0
        while column < width:
            skip = 0
            while column < width and line[column] < 0 and skip < 255:
                column += 1
                skip += 1
            if column >= width:
                out += bytes([skip])
                break
            run = []
            while column < width and line[column] >= 0 and len(run) < 255:
                run.append(int(line[column]))
                column += 1
            out += bytes([skip, len(run)]) + bytes(run)
            if column >= width:
                out += bytes([0])
        rows.append(bytes(out))
    header = struct.pack('<5i', 0, width, height, x_off, y_off)
    table = bytearray()
    body = bytearray()
    start = 0x14 + 4 * height
    for row, data in enumerate(rows):
        entry = 0x14 + 4 * row
        table += struct.pack('<I', start + len(body) - entry)
        body += data
    return bytes(8) + header + bytes(table) + bytes(body)


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument('game')
    parser.add_argument('rife')
    parser.add_argument('--work')
    args = parser.parse_args()
    raw = static_file(args.game, 'gamepal.pal').read_bytes()
    palette = numpy.array([[min(255, raw[i * 3 + k] * 4) for k in range(3)] for i in range(256)], numpy.int32)
    frames = shape_frames(static_file(args.game, 'shapes.flx'), SHAPE)
    work = Path(args.work) if args.work else Path(tempfile.mkdtemp(prefix='turns-'))
    made = []
    table = bytearray()
    count = 0
    for anim in ANIMS:
        numbers = animation_frames(static_file(args.game, 'anim.dat'), SHAPE, anim)
        if numbers is None:
            sys.exit(f'smooth_turns: animation {anim:#x} is not one frame a direction in 16')
        decoded = [decode(frames[n]) for n in numbers]
        # One canvas: each frame's origin at the same point.
        left = max(x for _, x, _ in decoded) + 2
        top = max(y for _, _, y in decoded) + 2
        right = max(e.shape[1] - x for e, x, _ in decoded) + 2
        bottom = max(e.shape[0] - y for e, _, y in decoded) + 2
        width, height = left + right, top + bottom
        used = sorted({int(v) for e, _, _ in decoded for v in e[e >= 0].ravel()})
        mean = palette[[int(v) for e, _, _ in decoded for v in e[e >= 0].ravel()]].mean(0).astype(numpy.uint8)
        folders = {name: work / f'{anim:02x}' / name for name in ('in', 'mask', 'out', 'maskout')}
        for folder in folders.values():
            shutil.rmtree(folder, ignore_errors=True)
            folder.mkdir(parents=True)
        # Round the loop: the first direction again at its end.
        for k, (entries, x_off, y_off) in enumerate(decoded + decoded[:1]):
            picture = numpy.empty((height, width, 3), numpy.uint8)
            picture[:] = mean
            mask = numpy.zeros((height, width, 3), numpy.uint8)
            h, w = entries.shape
            at_y, at_x = top - y_off, left - x_off
            where = entries >= 0
            picture[at_y:at_y + h, at_x:at_x + w][where] = palette[entries[where]]
            mask[at_y:at_y + h, at_x:at_x + w][where] = 255
            for array, folder in ((picture, folders['in']), (mask, folders['mask'])):
                Image.fromarray(array).resize((width * UP, height * UP), Image.NEAREST).save(folder / f'{k:04d}.png')
        total = DIRECTIONS * BETWEEN + 1
        for source, target in (('in', 'out'), ('mask', 'maskout')):
            subprocess.run([args.rife, '-i', str(folders[source]), '-o', str(folders[target]), '-m', 'rife-v4.6', '-n',
                            str(total)], check=True, capture_output=True, cwd=Path(args.rife).parent)
        outs = sorted(folders['out'].iterdir())
        masks = sorted(folders['maskout'].iterdir())
        if len(outs) != total or len(masks) != total:
            sys.exit(f'smooth_turns: RIFE made {len(outs)} frames, not {total}')
        colours = palette[used]
        table += struct.pack('<H', anim)
        for direction in range(DIRECTIONS):
            for step in range(1, BETWEEN):
                k = direction * BETWEEN + step
                big = numpy.asarray(Image.open(outs[k]).convert('RGB')).astype(numpy.int32)
                small = big.reshape(height, UP, width, UP, 3).mean(axis=(1, 3))
                outline = numpy.asarray(Image.open(masks[k]).convert('L')).astype(numpy.float32)
                outline = outline.reshape(height, UP, width, UP).mean(axis=(1, 3)) > 127
                nearest = ((small[:, :, None, :] - colours[None, None, :, :]) ** 2).sum(-1).argmin(-1)
                entries = numpy.where(outline, numpy.array(used)[nearest], -1)
                ys, xs = numpy.nonzero(entries >= 0)
                if len(ys) == 0:
                    sys.exit(f'smooth_turns: animation {anim:#x} direction {direction} step {step} is empty')
                y0, y1, x0, x1 = ys.min(), ys.max() + 1, xs.min(), xs.max() + 1
                made.append(encode(entries[y0:y1, x0:x1], left - x0, top - y0))
                table += struct.pack('<H', FIRST + count)
                count += 1
    out = bytearray(b'TURN')
    out += struct.pack('<HHH', SHAPE, FIRST, len(ANIMS)) + table
    out += struct.pack('<H', len(made))
    out += b''.join(struct.pack('<I', len(m)) for m in made)
    out += b''.join(made)
    target = static_file(args.game, 'anim.dat').with_name('TURNS.DAT')
    target.write_bytes(bytes(out))
    print(f'smooth_turns: {target}: {len(ANIMS)} animations, {len(made)} frames from {FIRST}')


if __name__ == '__main__':
    main()
