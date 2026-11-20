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
pixels, 0xRRGGBBAA (u32 each, little-endian). The game holds it outside its
arena, read whole once.

usage: upscale.py GAME_DIR ESRGAN_EXE SHAPE[,SHAPE...] [--model NAME]
       [--work DIR]
SHAPE is a shape's number in hex."""
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
    for folder in (source, padded, upscaled, alone):
        shutil.rmtree(folder, ignore_errors=True)
        folder.mkdir(parents=True)
    names = []
    for shape in (int(s, 16) for s in args.shapes.split(',')):
        frames = dict(enumerate(shape_frames(flx, shape)))
        frames.update(added_frames(args.game, shape))
        for number, frame in sorted(frames.items()):
            if len(frame) < 8 + 0x14 or min(struct.unpack_from('<ii', frame, 12)) <= 0:
                continue
            name = f's{shape:04x}_f{number:04d}.png'
            image = frame_image(frame, palette)
            image.save(source / name)
            canvas = Image.new('RGBA', (max(CANVAS[0], image.size[0]), max(CANVAS[1], image.size[1])), (0, 0, 0, 0))
            canvas.paste(image, (0, 0))
            canvas.save(padded / name)
            names.append((shape, number, name))

    def esrgan(folder, out):
        subprocess.run([args.esrgan, '-i', str(folder), '-o', str(out), '-n', args.model, '-s', '4', '-f', 'png'],
                       check=True, cwd=Path(args.esrgan).parent, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

    esrgan(padded, upscaled)
    doubled = 0
    header = bytearray(b'UPS2')
    header += struct.pack('<HI', SCALE, len(names))
    pixels = bytearray()
    for shape, number, name in names:
        original = Image.open(source / name).convert('RGBA')
        width, height = original.size[0] * SCALE, original.size[1] * SCALE
        four = (original.size[0] * 4, original.size[1] * 4)
        big = Image.open(upscaled / name).convert('RGBA').crop((0, 0) + four)
        if unlike(original, big) > UNLIKE:
            one = alone / 'one'
            shutil.rmtree(one, ignore_errors=True)
            one.mkdir()
            original.save(one / name)
            esrgan(one, alone)
            big = Image.open(alone / name).convert('RGBA')
            if unlike(original, big) > UNLIKE:
                big = original.resize(four, Image.NEAREST)
                doubled += 1
        big = big.resize((width, height), Image.LANCZOS)
        # Kept where the frame has pixels, opaque there.
        mask = original.getchannel('A').resize((width, height), Image.NEAREST)
        big.putalpha(mask)
        header += struct.pack('<HHHHI', shape, number, width, height, len(pixels) // 4)
        # Each pixel's bytes reversed: 0xRRGGBBAA little-endian.
        pixels += numpy.asarray(big)[..., ::-1].tobytes()
    target = static_file(args.game, 'anim.dat').with_name('UPSCALED.DAT')
    target.write_bytes(bytes(header) + bytes(pixels))
    print(f'upscale: {target}: {len(names)} frames at {SCALE}x, {doubled} only doubled')


if __name__ == '__main__':
    sys.exit(main())
