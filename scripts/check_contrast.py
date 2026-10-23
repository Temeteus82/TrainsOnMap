#!/usr/bin/env python3
"""Contrast check for the colours drawn on the map and the card (UI audit U3-W4).

Reads the tokens straight from qml/Theme.qml and the grounds straight from the
vendored basemap styles, so a token change or a basemap change that drops a
ratio under its WCAG floor fails the build instead of waiting for the next
audit. Round 3 found two such regressions (U3-C1, U3-C3) that the comments in
Theme.qml, written against an older basemap, had hidden.

    python scripts/check_contrast.py          # exit 1 on any failure

Stdlib only. Registered with ctest as `contrast`.
"""
import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
THEME = ROOT / "qml" / "Theme.qml"
STYLES = {"light": ROOT / "resources/styles/positron.json",
          "dark": ROOT / "resources/styles/dark-matter.json"}

# Card surfaces, as Theme.cardBg resolves over the map (round 2's figures).
CARD = {"light": "#fefefe", "dark": "#1c1f24"}

# Graphics on the map: WCAG 1.4.11, >= 3:1 against the basemap's ground.
MAP_TOKENS = ["railColor", "railSidingColor", "ringLate", "ringVeryLate",
              "ringSuspect", "trainStale", "accent", "markerEdge"]
# Main.qml draws the selected route in `accent` at this opacity (U3-W3).
ROUTE_OPACITY = 0.7

# Text on the card: WCAG 1.4.3, >= 4.5:1.
CARD_TEXT_TOKENS = ["textMuted", "textStrong", "delayLate", "delayEarly",
                    "ringLateText", "liveOnText"]


def rgba(c):
    """'#rrggbb' / '#rgb' / 'rgb(a)(r, g, b[, a])' (0-255) -> (r, g, b, a) in 0..1."""
    c = c.strip()
    if c.startswith("rgb"):
        v = [float(x) for x in c[c.index("(") + 1:-1].split(",")]
        return (v[0] / 255, v[1] / 255, v[2] / 255, v[3] if len(v) > 3 else 1.0)
    c = c.lstrip("#")
    if len(c) == 3:
        c = "".join(ch * 2 for ch in c)
    return tuple(int(c[i:i + 2], 16) / 255 for i in (0, 2, 4)) + (1.0,)


def over(fg, bg, alpha=None):
    a = fg[3] if alpha is None else alpha * fg[3]
    return tuple(f * a + b * (1 - a) for f, b in zip(fg[:3], bg[:3]))


def luminance(c):
    f = lambda v: v / 12.92 if v <= 0.03928 else ((v + 0.055) / 1.055) ** 2.4
    return 0.2126 * f(c[0]) + 0.7152 * f(c[1]) + 0.0722 * f(c[2])


def ratio(a, b):
    la, lb = luminance(a), luminance(b)
    return (max(la, lb) + 0.05) / (min(la, lb) + 0.05)


def theme_tokens():
    """name -> {"light": colour, "dark": colour} for `isDark ? X : Y` tokens."""
    text = THEME.read_text(encoding="utf-8")
    value = r'("#[0-9A-Fa-f]{3,8}"|Qt\.rgba\([^)]*\))'
    tokens = {}
    for m in re.finditer(r"property color (\w+):\s*isDark\s*\?\s*" + value + r"\s*:\s*" + value, text):
        def parse(v):
            if v.startswith("Qt.rgba"):
                r, g, b, a = (float(x) for x in v[v.index("(") + 1:-1].split(","))
                return (r, g, b, a)
            return rgba(v.strip('"'))
        tokens[m.group(1)] = {"dark": parse(m.group(2)), "light": parse(m.group(3))}
    return tokens


def main():
    tokens = theme_tokens()
    failures = []

    def check(what, value, floor):
        status = "ok " if value >= floor else "FAIL"
        print(f"  {status} {value:5.2f} (>= {floor})  {what}")
        if value < floor:
            failures.append(what)

    for theme, path in STYLES.items():
        style = json.loads(path.read_text(encoding="utf-8"))
        layers = {l["id"]: l for l in style["layers"]}
        ground = rgba(layers["background"]["paint"]["background-color"])
        water = rgba(layers["water"]["paint"]["fill-color"])
        card = rgba(CARD[theme])
        print(f"{theme}: ground {layers['background']['paint']['background-color']}")

        for name in MAP_TOKENS:
            if name not in tokens:
                failures.append(f"{name}: not found in Theme.qml")
                continue
            check(f"{theme} {name} on ground", ratio(over(tokens[name][theme], ground), ground), 3.0)
        check(f"{theme} selected route (accent @ {ROUTE_OPACITY})",
              ratio(over(tokens["accent"][theme], ground, ROUTE_OPACITY), ground), 3.0)

        for name in CARD_TEXT_TOKENS:
            check(f"{theme} {name} as text on card", ratio(tokens[name][theme][:3], card), 4.5)

        # Every basemap label, against the ground it is drawn on (U3-W5).
        for l in style["layers"]:
            tc = l.get("paint", {}).get("text-color") if l["type"] == "symbol" else None
            if tc is None:
                continue
            cols = [tc] if isinstance(tc, str) else [s[1] for s in tc["stops"]]
            cols = [c for c in cols if c != "transparent"]
            if not cols:
                continue   # a hidden label (CARTO uses transparent text for some)
            under = water if l["id"].startswith("water") else ground
            check(f"{theme} label {l['id']}", min(ratio(rgba(c)[:3], under) for c in cols), 4.5)

    if failures:
        print(f"\n{len(failures)} contrast failure(s)")
        return 1
    print("\nall contrast checks pass")
    return 0


if __name__ == "__main__":
    sys.exit(main())
