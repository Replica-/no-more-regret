#!/usr/bin/env python3
"""Writes a game's static/upscaled.dat: frames of shapes upscaled by an AI
upscaler (Real-ESRGAN's ncnn build, realesrgan-ncnn-vulkan, run on the
graphics card), for the hardware scene's upscaled textures
(common/src/modern/upscaled.h reads it). Each frame of each shape is drawn
from shapes.flx in the palette's colours, upscaled four times by the model,
scaled down to SCALE times smoothly, and kept where the frame has pixels
(the game shows the upscaled pixels only over its own frame's that still
show). Shape 1's (the Silencer's) frames the port adds are upscaled with
its own: those of static/imported.dat (import_anims.py) and turns.dat
(smooth_turns.py), when there.

upscaled.dat: "UPS2", the scale (u16) and the count of frames (u32); each
frame's shape, frame, width and height upscaled (u16 each) and its first
pixel's place among them (u32), in the order of shape and frame; then the
pixels, 0xRRGGBBAA (u32 each, little-endian). The game maps it outside its
arena (files.h's fileHeld).

The frames go to the upscaler in batches of one canvas size (BATCH at
most), whose files go once used, and the file is written as they come: the
whole game is some 30000 frames and a gigabyte.

usage: upscale.py GAME_DIR ESRGAN_EXE SHAPE[,SHAPE...]|all [--model NAME]
       [--work DIR]
SHAPE is a shape's number in hex; all, every shape but the effects' (the
explosions, flashes and fires the hardware scene draws itself)."""
import argparse
import shutil
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

import numpy
from PIL import Image

SCALE = 2
# The upscaler's ncnn build gives noise for many frames of a folder of
# frames of different sizes: each is upscaled on a canvas of one size (its
# top left corner), checked against the frame, and one still unlike it is
# upscaled alone, else doubled as it is.
CANVAS = (256, 128)
UNLIKE = 30
BATCH = 1000


def effect(shape):
    """The effects' shapes (common/src/modern/effects.cpp's isEffect)."""
    return 0x31c <= shape <= 0x327 or 0x438 <= shape <= 0x43c or shape in (0x4cb, 0x50a, 0x518, 0x44a)


def canvas_of(size):
    """The canvas a frame of size is upscaled on: CANVAS, or larger in
    steps of 64."""
    return (max(CANVAS[0], -(-size[0] // 64) * 64), max(CANVAS[1], -(-size[1] // 64) * 64))


def bled(image, size):
    """The frame opaque on a canvas of size, its edge's colours spread out
    over the empty pixels round it (and the rest its mean colour): the
    upscaler sees no black round it, which it would darken its edges with
    (seams between floor tiles)."""
    rgba = numpy.zeros((size[1], size[0], 4), numpy.uint8)
    rgba[:image.size[1], :image.size[0]] = numpy.asarray(image)
    rgb = rgba[..., :3].astype(numpy.int32)
    have = rgba[..., 3] > 0
    if have.any():
        rgb[~have] = rgb[have].mean(0).astype(numpy.int32)
    filled = have.copy()
    for _ in range(8):
        grown = filled.copy()
        total = numpy.zeros_like(rgb)
        count = numpy.zeros(filled.shape, numpy.int32)
        for dy, dx in ((0, 1), (0, -1), (1, 0), (-1, 0)):
            shifted = numpy.roll(filled, (dy, dx), (0, 1))
            colours = numpy.roll(rgb, (dy, dx), (0, 1))
            take = shifted & ~filled
            total[take] += colours[take]
            count[take] += 1
        new = count > 0
        rgb[new] = total[new] // count[new][:, None]
        grown[new] = True
        filled = grown
    return Image.fromarray(rgb.astype(numpy.uint8), 'RGB')


def unlike(original, big):
    """How far the upscaled frame's colours are from the frame's, on
    average over its pixels (0-255)."""
    small = numpy.asarray(original.resize(big.size, Image.NEAREST)).astype(int)
    large = numpy.asarray(big).astype(int)
    where = small[..., 3] > 0
    if not where.any():
        return 0
    return numpy.abs(small[where][:, :3] - large[where][:, :3]).mean()


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
    if len(data) < 6:
        return []
    count = struct.unpack_from('<H', data, 4)[0]
    frames = []
    for i in range(count):
        start = struct.unpack_from('<I', data, 6 + 8 * i)[0] & 0xffffff
        length = struct.unpack_from('<I', data, 10 + 8 * i)[0]
        frames.append(data[start:start + length])
    return frames


def frame_image(frame, palette):
    """A frame (its bytes as the shape file has them: 8 bytes, the header
    and the rows' runs, drawframe.h) as an RGBA picture."""
    frame = frame[8:]
    word = lambda at: struct.unpack_from('<i', frame, at)[0]
    flags, width, height = word(0), word(4), word(8)
    image = Image.new('RGBA', (max(width, 1), max(height, 1)), (0, 0, 0, 0))
    pixels = image.load()
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
                pixels[c, row] = palette[frame[p] if fill else frame[p + c - column]] + (255,)
            p += 1 if fill else length
            column += length
    return image


def added_frames(game, shape):
    """The frames of shape the port adds, by number: imported.dat's and
    turns.dat's (as the shape file holds a frame each)."""
    found = {}
    folder = next(p for p in Path(game).iterdir() if p.name.lower() == 'static')
    files = {p.name.lower(): p for p in folder.iterdir()}
    if 'imported.dat' in files:
        data = files['imported.dat'].read_bytes()
        if data[:4] == b'IMPA' and struct.unpack_from('<H', data, 4)[0] == shape:
            anims = struct.unpack_from('<H', data, 6)[0]
            at = 8
            for _ in range(anims):
                entries = struct.unpack_from('<H', data, at + 6)[0]
                at += 8 + 8 * entries
                types = struct.unpack_from('<H', data, at)[0]
                at += 2 + 4 * entries * types
            first, count = struct.unpack_from('<HH', data, at)
            at += 4
            sizes = struct.unpack_from(f'<{count}I', data, at)
            at += 4 * count
            for i, size in enumerate(sizes):
                found[first + i] = data[at:at + size]
                at += size
    if 'turns.dat' in files:
        data = files['turns.dat'].read_bytes()
        if data[:4] == b'TURN' and struct.unpack_from('<H', data, 4)[0] == shape:
            first, anims = struct.unpack_from('<HH', data, 6)
            at = 10 + anims * (2 + 2 * 48)
            count = struct.unpack_from('<H', data, at)[0]
            at += 2
            sizes = struct.unpack_from(f'<{count}I', data, at)
            at += 4 * count
            for i, size in enumerate(sizes):
                found[first + i] = data[at:at + size]
                at += size
    return found


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument('game')
    parser.add_argument('esrgan')
    parser.add_argument('shapes')
    parser.add_argument('--model', default='realesrgan-x4plus')
    parser.add_argument('--work')
    args = parser.parse_args()
    raw = static_file(args.game, 'gamepal.pal').read_bytes()
    palette = [tuple(min(255, raw[i * 3 + k] * 4) for k in range(3)) for i in range(256)]
    flx = static_file(args.game, 'shapes.flx')
    work = Path(args.work) if args.work else Path(tempfile.mkdtemp(prefix='upscale-'))
    source = work / 'in'
    padded = work / 'padded'
    upscaled = work / 'out'
    alone = work / 'alone'
    alone.mkdir(parents=True, exist_ok=True)
    if args.shapes == 'all':
        count = struct.unpack_from('<I', flx.read_bytes()[:0x58], 0x54)[0]
        shapes = [shape for shape in range(count) if not effect(shape)]
    else:
        shapes = [int(text, 16) for text in args.shapes.split(',')]
    # Every frame first: its shape, number, picture and canvas.
    frames_all = []
    for shape in shapes:
        frames = dict(enumerate(shape_frames(flx, shape)))
        frames.update(added_frames(args.game, shape))
        for number, frame in sorted(frames.items()):
            if len(frame) < 8 + 0x14 or min(struct.unpack_from('<ii', frame, 12)) <= 0:
                continue
            width, height = struct.unpack_from('<ii', frame, 12)
            if width > 2048 or height > 2048:
                continue
            frames_all.append((shape, number, frame, canvas_of((width, height))))
    # The table in the order of shape and frame; the pixels as they come.
    order = sorted(range(len(frames_all)), key=lambda i: (frames_all[i][3], i))
    entries = [None] * len(frames_all)
    target = static_file(args.game, 'anim.dat').with_name('UPSCALED.DAT')
    partial = target.with_name('UPSCALED.NEW')
    header_size = 4 + 6 + (12 * len(frames_all))
    doubled = 0
    written = 0

    def esrgan(folder, out):
        subprocess.run([args.esrgan, '-i', str(folder), '-o', str(out), '-n', args.model, '-s', '4', '-f', 'png'],
                       check=True, cwd=Path(args.esrgan).parent, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

    with open(partial, 'wb') as out:
        out.write(bytes(header_size))
        at = 0
        while at < len(order):
            # A batch: of one canvas size, BATCH at most.
            size = frames_all[order[at]][3]
            batch = []
            while at < len(order) and len(batch) < BATCH and frames_all[order[at]][3] == size:
                batch.append(order[at])
                at += 1
            for folder in (source, padded, upscaled):
                shutil.rmtree(folder, ignore_errors=True)
                folder.mkdir(parents=True)
            for index in batch:
                shape, number, frame, canvas = frames_all[index]
                name = f's{shape:04x}_f{number:04d}.png'
                image = frame_image(frame, palette)
                image.save(source / name)
                bled(image, canvas).save(padded / name)
            esrgan(padded, upscaled)
            for index in batch:
                shape, number, frame, canvas = frames_all[index]
                name = f's{shape:04x}_f{number:04d}.png'
                original = Image.open(source / name).convert('RGBA')
                width, height = original.size[0] * SCALE, original.size[1] * SCALE
                four = (original.size[0] * 4, original.size[1] * 4)
                big = Image.open(upscaled / name).convert('RGBA').crop((0, 0) + four)
                if unlike(original, big) > UNLIKE:
                    one = alone / 'one'
                    shutil.rmtree(one, ignore_errors=True)
                    one.mkdir(parents=True)
                    bled(original, original.size).save(one / name)
                    esrgan(one, alone)
                    big = Image.open(alone / name).convert('RGBA')
                    if unlike(original, big) > UNLIKE:
                        big = original.resize(four, Image.NEAREST)
                        doubled += 1
                big = big.resize((width, height), Image.LANCZOS)
                # Kept where the frame has pixels, opaque there.
                mask = original.getchannel('A').resize((width, height), Image.NEAREST)
                big.putalpha(mask)
                entries[index] = (shape, number, width, height, written)
                # Each pixel's bytes reversed: 0xRRGGBBAA little-endian.
                out.write(numpy.asarray(big)[..., ::-1].tobytes())
                written += width * height
            print(f'upscale: {at} of {len(order)} frames', flush=True)
        out.seek(0)
        header = bytearray(b'UPS2')
        header += struct.pack('<HI', SCALE, len(frames_all))
        for shape, number, width, height, first in sorted(entries):
            header += struct.pack('<HHHHI', shape, number, width, height, first)
        out.write(header)
    for folder in (source, padded, upscaled, alone):
        shutil.rmtree(folder, ignore_errors=True)
    partial.replace(target)
    print(f'upscale: {target}: {len(frames_all)} frames at {SCALE}x, {doubled} only doubled')


if __name__ == '__main__':
    sys.exit(main())
