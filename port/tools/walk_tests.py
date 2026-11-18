#!/usr/bin/env python3
"""The pointer mode's click-to-walk (common/src/modern/pathwalk.cpp) in No Remorse's mission 1 (-warp 1, the
1280x720 frame), traced (REGRET_WALK_TRACE) and scored. Each case clicks once: the CASES walk to a map point he
can stand on (REGRET_WALK_GOAL), the CLICKS to a point of the frame, most of them out of his reach from the
teleport pad (behind doors and fences), where he should go as near as he can and stop. For each: whether he
arrived, the ticks, how far from the goal he ended, the straight distance and the distance walked, the times he
came back to a 32-unit block he had left (retreads) and the plans made; a picture of each walk in map units.
It needs the game in cd/noremorse and the port built in build/port; enhanced.cfg is left as it was.

usage: walk_tests.py OUTDIR [CASE...]"""
import os, re, subprocess, sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
GAME = ROOT / 'cd/noremorse'
EXE = ROOT / 'build/port/crusader-port.exe'
CASES = {
    'lowleft': (30398, 30622), 'eastwalk': (31132, 29908), 'eastlow': (31084, 30112), 'leftmid': (30378, 30410),
    'midlow': (30592, 30300), 'walkway': (30931, 29796), 'walkdown': (30864, 30008), 'nearwalk': (30652, 29812),
}
CLICKS = {
    'c_walkway': (1100, 420), 'c_leftroom': (150, 470), 'c_bottom': (700, 650), 'c_topright': (1000, 150),
    'c_barrels': (400, 330), 'c_rightlow': (900, 560), 'c_farleft': (60, 300), 'c_behindpad': (520, 120),
}
TICKS = 1800

def run(name, gx, gy, out):
    x, y = (gx, gy) if name in CLICKS else (900, 400)
    script = out / f'{name}.txt'
    lines = ['400 key 01', '401 release 81', '700 key 01', '701 release 81', '1000 key 01', '1001 release 81',
             f'2900 mouse {x*2} {y} 0', f'3000 mouse {x*2} {y} 1', f'3004 mouse {x*2} {y} 0',
             f'{3000+TICKS} screen {(out / (name + ".png")).as_posix()}', f'{3000+TICKS+5} quit']
    script.write_text('\n'.join(lines) + '\n')
    env = dict(os.environ, REGRET_SCRIPT=script.as_posix(), CRUSADER_GAME='remorse', REGRET_FRAME='1280x720',
               REGRET_WALK_TRACE='1')
    if name not in CLICKS:
        env['REGRET_WALK_GOAL'] = f'{gx},{gy}'
    r = subprocess.run([str(EXE), '-warp', '1', '-skill', '2'], cwd=GAME, env=env, capture_output=True,
                       encoding='latin-1', timeout=600)
    return r.stdout + r.stderr

def cheb(a, b):
    return max(abs(a[0] - b[0]), abs(a[1] - b[1]))

def score(text):
    at, plans, outcome, end = [], 0, 'timeout', None
    for line in text.splitlines():
        m = re.search(r'walk at (\d+) (-?\d+) (-?\d+) goal (-?\d+) (-?\d+)', line)
        if m:
            at.append(tuple(int(v) for v in m.groups()))
        elif 'walk plan' in line:
            plans += 1
        elif 'walk arrived' in line and outcome == 'timeout':
            outcome, end = 'arrived', int(line.split()[-1])
        elif 'walk blocked' in line and outcome == 'timeout':
            outcome, end = 'gave up', int(line.split()[-1])
    if not at:
        return None, []
    start, goal = (at[0][1], at[0][2]), (at[0][3], at[0][4])
    pts = [(a[1], a[2]) for a in at]
    travelled = sum(cheb(pts[i], pts[i + 1]) for i in range(len(pts) - 1))
    cells, retreads, last = set(), 0, None
    for p in pts:
        c = (p[0] // 32, p[1] // 32)
        if c != last:
            if c in cells:
                retreads += 1
            cells.add(c)
            last = c
    ticks = (end or at[-1][0]) - at[0][0]
    return dict(outcome=outcome, ticks=ticks, left=cheb(pts[-1], goal), straight=cheb(start, goal),
                travelled=travelled, retreads=retreads, plans=plans, steps=len(pts)), pts

def plot(pts, goal, path):
    from PIL import Image, ImageDraw
    xs = [p[0] for p in pts] + [goal[0]]; ys = [p[1] for p in pts] + [goal[1]]
    lo_x, lo_y = min(xs) - 64, min(ys) - 64
    span = max(max(xs) - lo_x, max(ys) - lo_y) + 64
    k = 400 / span
    im = Image.new('RGB', (400, 400), 'black'); d = ImageDraw.Draw(im)
    tr = lambda p: ((p[0] - lo_x) * k, (p[1] - lo_y) * k)
    for i in range(len(pts) - 1):
        d.line([tr(pts[i]), tr(pts[i + 1])], fill=(80 + 175 * i // max(1, len(pts)), 200, 255), width=2)
    g = tr(goal); d.ellipse([g[0] - 5, g[1] - 5, g[0] + 5, g[1] + 5], outline='red', width=2)
    s = tr(pts[0]); d.rectangle([s[0] - 4, s[1] - 4, s[0] + 4, s[1] + 4], outline='lime', width=2)
    im.save(path)

def main():
    out = Path(sys.argv[1]); out.mkdir(parents=True, exist_ok=True)
    everything = {**CASES, **CLICKS}
    names = sys.argv[2:] or list(everything)
    cfg = GAME / 'enhanced.cfg'; saved = cfg.read_bytes()
    try:
        total = {}
        for name in names:
            text = run(name, *everything[name], out)
            (out / f'{name}.log').write_text(text, encoding='latin-1')
            s, pts = score(text)
            if s is None:
                print(f'{name:10} no walk'); continue
            total[name] = s
            plot(pts, (int(re.search(r'goal (-?\d+) (-?\d+)', text).group(1)), int(re.search(r'goal (-?\d+) (-?\d+)', text).group(2))), out / f'{name}_map.png')
            print(f"{name:10} {s['outcome']:8} ticks {s['ticks']:5} left {s['left']:5} straight {s['straight']:5} "
                  f"travelled {s['travelled']:5} retreads {s['retreads']:3} plans {s['plans']:3}")
    finally:
        if cfg.read_bytes() != saved:
            cfg.write_bytes(saved); print('(enhanced.cfg restored)')

main()
