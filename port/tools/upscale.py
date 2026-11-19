#!/usr/bin/env python3
"""Writes a game's static/upscaled.dat: frames of shapes upscaled by an AI
upscaler (Real-ESRGAN's ncnn build, realesrgan-ncnn-vulkan, run on the
graphics card), for the hardware scene's upscaled textures
(common/src/modern/upscaled.h reads it). Each frame of each shape is drawn
from shapes.flx in the palette's colours, upscaled four times by the model,
scaled down to SCALE times smoothly, and kept where the frame has pixels
(the game shows the upscaled pixels only over its own frame's that still
show).

upscaled.dat: "UPSC", the scale and the count of frames (u16 each); each
frame's shape, frame, width and height upscaled (u16 each); then each
frame's pixels in turn, 0xRRGGBBAA (u32 each, little-endian).

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
    upscaled = work / 'out'
    for folder in (source, upscaled):
        shutil.rmtree(folder, ignore_errors=True)
        folder.mkdir(parents=True)
    names = []
    for shape in (int(s, 16) for s in args.shapes.split(',')):
        for number, frame in enumerate(shape_frames(flx, shape)):
            name = f's{shape:04x}_f{number:04d}.png'
            frame_image(frame, palette).save(source / name)
            names.append((shape, number, name))
    subprocess.run([args.esrgan, '-i', str(source), '-o', str(upscaled), '-n', args.model, '-s', '4', '-f', 'png'],
                   check=True, cwd=Path(args.esrgan).parent, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    header = bytearray(b'UPSC')
    header += struct.pack('<HH', SCALE, len(names))
    pixels = bytearray()
    for shape, number, name in names:
        original = Image.open(source / name).convert('RGBA')
        width, height = original.size[0] * SCALE, original.size[1] * SCALE
        big = Image.open(upscaled / name).convert('RGBA').resize((width, height), Image.LANCZOS)
        # Kept where the frame has pixels, opaque there.
        mask = original.getchannel('A').resize((width, height), Image.NEAREST)
        big.putalpha(mask)
        header += struct.pack('<HHHH', shape, number, width, height)
        # Each pixel's bytes reversed: 0xRRGGBBAA little-endian.
        pixels += numpy.asarray(big)[..., ::-1].tobytes()
    target = static_file(args.game, 'anim.dat').with_name('UPSCALED.DAT')
    target.write_bytes(bytes(header) + bytes(pixels))
    print(f'upscale: {target}: {len(names)} frames at {SCALE}x')


if __name__ == '__main__':
    sys.exit(main())
