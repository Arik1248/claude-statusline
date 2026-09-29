#!/usr/bin/env python3
"""Render the real ./statusline output for a few payloads into docs/example.svg
and docs/example.png (PNG needs Google Chrome on macOS).

Run from the repo root after ./build.sh:  python3 tools/render-example.py
"""
import html, os, re, subprocess, sys, time, json

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BIN = os.path.join(ROOT, "statusline")
COLORS = {"0;31": "#f7768e", "0;32": "#9ece6a", "0;33": "#e0af68",
          "0;34": "#7aa2f7", "0;35": "#bb9af7", "0;36": "#7dcfff"}
DEFAULT = "#c0caf5"

def payload(model, effort, ctx, five=None):
    p = {"cwd": ROOT, "model": {"display_name": model},
         "effort": {"level": effort}, "context_window": {"used_percentage": ctx}}
    if five:
        p["rate_limits"] = {"five_hour": {"used_percentage": five[0],
                                          "resets_at": int(time.time()) + five[1]}}
    return json.dumps(p)

SAMPLES = [
    ("Fresh session",          payload("Opus 5", "high", 12, (18, 4 * 3600 + 25 * 60))),
    ("Getting full",           payload("Sonnet 5", "xhigh", 74, (72, 48 * 60))),
    ("Near the limit",         payload("Opus 5 (1m context)", "high", 93, (96, 9 * 60))),
    ("5h window already reset", payload("Opus 5", "high", 40, (95, -60))),
]

def spans(line):
    out, color, pos = [], DEFAULT, 0
    for m in re.finditer(r"\x1b\[([0-9;]*)m", line):
        if m.start() > pos:
            out.append((color, line[pos:m.start()]))
        color = DEFAULT if m.group(1) in ("0", "") else COLORS.get(m.group(1), DEFAULT)
        pos = m.end()
    if pos < len(line):
        out.append((color, line[pos:]))
    return out

CW, LH, PAD = 9.6, 22, 24
rows = []
for label, pl in SAMPLES:
    res = subprocess.run([BIN], input=pl, capture_output=True, text=True, check=True)
    rows.append((label, spans(res.stdout)))

width = int(PAD * 2 + CW * 96)
height = int(PAD * 2 + 34 + len(rows) * (LH * 2 + 6))
svg = [f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}" viewBox="0 0 {width} {height}" font-family="Menlo, Monaco, Consolas, monospace" font-size="16">',
       f'<rect width="{width}" height="{height}" rx="10" fill="#1a1b26"/>',
       '<circle cx="24" cy="20" r="6" fill="#f7768e"/><circle cx="44" cy="20" r="6" fill="#e0af68"/><circle cx="64" cy="20" r="6" fill="#9ece6a"/>']
y = 34 + PAD
for label, sp in rows:
    svg.append(f'<text x="{PAD}" y="{y}" fill="#565f89" font-size="13">{html.escape(label)}</text>')
    y += LH
    line = "".join(f'<tspan fill="{c}">{html.escape(t)}</tspan>' for c, t in sp)
    svg.append(f'<text x="{PAD}" y="{y}" xml:space="preserve" fill="{DEFAULT}">{line}</text>')
    y += LH + 6
svg.append("</svg>")
out = os.path.join(ROOT, "docs", "example.svg")
open(out, "w").write("\n".join(svg))
print("wrote", out)

CHROME = "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome"
if os.path.exists(CHROME):
    png = os.path.join(ROOT, "docs", "example.png")
    subprocess.run([CHROME, "--headless=new", "--disable-gpu", "--hide-scrollbars",
                    "--force-device-scale-factor=2", "--default-background-color=00000000",
                    f"--window-size={width},{height}", f"--screenshot={png}", "file://" + out],
                   check=True, capture_output=True)
    print("wrote", png)
else:
    print("Chrome not found; skipped docs/example.png")
