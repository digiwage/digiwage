#!/usr/bin/env python3
"""Generate the wallet's design assets from tokens.py.

Run from the repository root:  python3 contrib/qt-design/build_assets.py
Needs: lucide-static (npm, ISC) at LUCIDE_DIR, python3-fonttools, Inter OTFs.
"""
import os, re, sys, json
sys.path.insert(0, os.path.dirname(__file__))
from tokens import *

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
QT = os.path.join(ROOT, "src/qt")
LUCIDE = os.environ.get("LUCIDE_DIR", "/tmp/lu/node_modules/lucide-static/icons")
INTER = "/usr/share/fonts/opentype/inter"

ICONS = {"add": "plus", "add_recipient": "user-plus", "add_token": "circle-plus", "address-book": "book-user",
 "backup_wallet": "save", "chevron": "chevron-right", "clock1": "clock-12", "clock2": "clock-3", "clock3": "clock-6",
 "clock4": "clock-9", "clock5": "clock", "configure": "settings", "connect0": "signal-zero", "connect1": "signal-low",
 "connect2": "signal-medium", "connect3": "signal-high", "connect4": "signal", "contract_input": "arrow-down-to-line",
 "contract_output": "arrow-up-from-line", "delegate": "users", "edit": "pencil", "editcopy": "copy",
 "editpaste": "clipboard-paste", "encrypt": "lock", "export": "download", "eye": "eye", "eye_minus": "eye-off",
 "eye_plus": "scan-eye", "filesave": "save", "fontbigger": "a-arrow-up", "fontsmaller": "a-arrow-down",
 "hd_disabled": "key-round", "hd_enabled": "key-round", "history": "history", "import": "file-up", "ledger_off": "usb",
 "ledger_on": "usb", "lock_closed": "lock", "lock_open": "lock-open", "lock_staking": "lock-keyhole",
 "network_disabled": "wifi-off", "overview": "layout-dashboard", "plus_full": "plus", "proxy": "shield",
 "qrctoken": "coins", "quit": "log-out", "receive": "arrow-down-left", "receive_from": "arrow-down-left",
 "remove": "trash-2", "remove_entry": "x", "request_payment": "hand-coins", "restore": "rotate-ccw",
 "send": "arrow-up-right", "send_to": "arrow-up-right", "show": "eye", "smart_contract": "file-code", "split": "split",
 "staking_off": "zap-off", "staking_on": "zap", "superstake": "rocket", "synced": "circle-check", "token": "coins",
 "transaction0": "clock", "transaction2": "check", "transaction_abandoned": "ban",
 "transaction_conflicted": "triangle-alert", "tx_inout": "arrow-left-right", "tx_input": "arrow-down-left",
 "tx_mined": "pickaxe", "tx_output": "arrow-up-right", "verify": "badge-check", "warning": "triangle-alert"}
# Extra icons for the new shell (sun/moon toggle, search, wallet, ...)
EXTRA = {"sun": "sun", "moon": "moon", "search": "search", "wallet": "wallet", "sidebar": "panel-left",
 "more": "ellipsis", "check_small": "check", "chevron_down": "chevron-down", "chevron_up": "chevron-up",
 "info": "info", "filter": "list-filter", "arrow_right": "arrow-right", "shield_check": "shield-check",
 "network": "network", "help": "circle-help", "book": "book-open", "sliders": "sliders-horizontal"}
NEUTRAL = "#8e8e9a"  # mid-grey: readable on both themes for icons the theme engine cannot recolour

def icon_svg(name, stroke, width=1.75, size=96):
    s = open(f"{LUCIDE}/{name}.svg").read()
    body = re.search(r"<svg[^>]*>(.*)</svg>", s, re.S).group(1)
    body = re.sub(r'\s+class="[^"]*"', "", body)
    return ('<svg xmlns="http://www.w3.org/2000/svg" width="%d" height="%d" viewBox="0 0 24 24" fill="none" '
            'stroke="%s" stroke-width="%s" stroke-linecap="round" stroke-linejoin="round">%s</svg>\n'
            % (size, size, stroke, width, body.strip()))

def hex_to_rgb(h): h = h.lstrip("#"); return tuple(int(h[i:i+2], 16) for i in (0, 2, 4))
def parse(c):
    if c.startswith("#"): return hex_to_rgb(c) + (255,)
    m = re.match(r"rgba\((\d+),(\d+),(\d+),(\d+)\)", c); return tuple(int(x) for x in m.groups())
def over(fg, bg):
    r, g, b, a = parse(fg); br, bg_, bb, _ = parse(bg); a /= 255
    return tuple(round(f * a + b_ * (1 - a)) for f, b_ in zip((r, g, b), (br, bg_, bb)))
def tohex(t): return "#%02x%02x%02x" % t
def lum(t):
    def ch(v):
        v /= 255; return v / 12.92 if v <= 0.03928 else ((v + 0.055) / 1.055) ** 2.4
    r, g, b = t; return 0.2126 * ch(r) + 0.7152 * ch(g) + 0.0722 * ch(b)
def contrast(a, b):
    la, lb = sorted((lum(a), lum(b)), reverse=True); return (la + 0.05) / (lb + 0.05)

def solid(tok, key, over_key="surface"): return tohex(over(tok[key], tok[over_key]))

def write_ini(mode, tok):
    t = dict(tok)
    for k in ("text-2", "text-3", "border", "hover"): t[k + "-solid"] = solid(tok, k)
    g = lambda k: t[k]
    lines = ["[tokens]"] + [f"{k}={v}" for k, v in t.items()]
    lines += [f"radius-{k}={v}" for k, v in RADIUS.items()] + [f"space-{k}={v}" for k, v in SPACING.items()]
    lines += [f"font-{k}={v}" for k, v in TYPE.items()] + [f"motion-{k}={v}" for k, v in MOTION_MS.items()]
    lines += [f"mode={mode}", f"styledir=:/styles/{mode}"]
    sd = f":/styles/{mode}"
    lines += ["", "[appstyle]", f"link-color={g('accent-text')}",
      f"message-critical-icon={sd}/message_critical", f"message-info-icon={sd}/message_info",
      f"message-question-icon={sd}/message_question", f"message-warning-icon={sd}/message_warning",
      "message-icon-height=44", "message-icon-weight=44", "button_text_upper=0",
      "", "[platformstyle]", "version=1", f"single-color={g('text-2-solid')}", f"text-color={g('text')}",
      f"table-color-normal={g('text')}", f"table-color-input={g('accent-text')}", f"table-color-inout={g('success')}",
      f"table-color-output={g('success')}", f"table-color-error={g('danger')}",
      f"multi-states-icon-color1={g('text')}", f"multi-states-icon-color2={g('text-3-solid')}",
      f"multi-states-icon-color3={g('text-3-solid')}", f"menu-color={g('text-2-solid')}",
      "", "[navtoolbutton]", f"color-enabled={g('text-2-solid')}", f"color-pressed={g('accent-text')}",
      f"color-hover={g('text')}", f"color-disabled={g('text-3-solid')}", "sub-icon=", "sub-icon-height=6",
      "sub-icon-width=3", "sub-padding-right=16", "sub-padding-left=0", "sub-alignment=2", "",
      "[navigationbar]", "logo-space=0", "",
      "[tokenviewdelegate]", "token-size=56", "symbol-width=64", "margin=8", f"background-color-selected={g('accent-soft')}",
      f"background-color={g('surface')}", f"hline-color={g('border-solid')}", f"foreground-color={g('text')}", f"amount-color={g('text')}",
      "", "[tknviewdelegate]", f"background-color={g('surface')}", f"hline-color={g('border-solid')}", f"foreground-color={g('text')}",
      "", "[txviewdelegate]", f"background-color-selected={g('accent-soft')}", f"background-color={g('surface')}",
      f"alternate-background-color={g('surface')}", f"foreground-color={g('text')}", f"foreground-color-selected={g('text')}",
      f"amount-color={g('text')}",
      "", "[splashscreen]", f"foreground-color={g('text')}", f"foreground-color-statusbar={g('text-2-solid')}",
      f"foreground-color_statusbar={g('text-2-solid')}", f"background-color={g('bg')}", "logo-frame-color=transparent",
      "", "[unitdisplaystatusbarcontrol]", "menu-margin=8", "icon-height=10", "icon-width=10", f"icon-path={sd}/arrow_down_small",
      "", "[modaloverlay]", f"warning-icon-color={g('warning')}",
      "", "[transactiondesc]", f"item-color={g('text')}", "item-font-bold=false", f"item-name-color={g('text-2-solid')}",
      "", "[tokentransactiondesc]", f"item-color={g('text')}", "item-font-bold=false", f"item-name-color={g('text-2-solid')}",
      "", "[guiconstants]", f"color-unconfirmed={g('text-3-solid')}", f"color-negative={g('danger')}",
      f"color-bareaddress={g('text-2-solid')}", f"color-tx-status-openuntildate={g('accent-text')}",
      f"color-tx-status-danger={g('danger')}", f"color-black={g('text')}"]
    open(f"{QT}/res/styles/{mode}.ini", "w").write("\n".join(lines) + "\n")

def svg_indicators(mode, tok):
    d = f"{QT}/res/styles/{mode}"; os.makedirs(d, exist_ok=True)
    acc, on, bd = tok["accent"], tok["on-accent"], solid(tok, "border-strong", "bg")
    txt2, txt3, bgs = solid(tok, "text-2"), solid(tok, "text-3"), tok["surface-2"]
    fill_off = tok["surface-2"]
    def box(state, hover=False, disabled=False, radio=False):
        rx = 9 if radio else 5
        stroke = tok["accent"] if hover else bd
        f, s = (fill_off, stroke)
        mark = ""
        if state != "unchecked":
            f, s = (acc, acc)
            if state == "checked":
                mark = (f'<circle cx="12" cy="12" r="4" fill="{on}"/>' if radio else
                        f'<path d="M7.2 12.4l3.2 3.2 6.4-6.8" fill="none" stroke="{on}" stroke-width="2.2" stroke-linecap="round" stroke-linejoin="round"/>')
            else:
                mark = f'<path d="M7.5 12h9" stroke="{on}" stroke-width="2.2" stroke-linecap="round"/>'
        op = 0.4 if disabled else 1
        return (f'<svg xmlns="http://www.w3.org/2000/svg" width="48" height="48" viewBox="0 0 24 24" opacity="{op}">'
                f'<rect x="3" y="3" width="18" height="18" rx="{rx}" fill="{f}" stroke="{s}" stroke-width="1.5"/>{mark}</svg>')
    for radio, pre in ((False, "checkbox"), (True, "radiobutton")):
        for st in ("unchecked", "checked") + (() if radio else ("indeterminate",)):
            open(f"{d}/{pre}_{st}.svg", "w").write(box(st, radio=radio))
            open(f"{d}/{pre}_{st}_hover.svg", "w").write(box(st, hover=True, radio=radio))
            open(f"{d}/{pre}_{st}_disabled.svg", "w").write(box(st, disabled=True, radio=radio))
    for nm, ico, col in (("down", "chevron-down", txt2), ("down_hover", "chevron-down", tok["text"]), ("down_disabled", "chevron-down", txt3),
                          ("up", "chevron-up", txt2), ("up_hover", "chevron-up", tok["text"]), ("up_disabled", "chevron-up", txt3),
                          ("right", "chevron-right", txt2), ("arrow_down_small", "chevron-down", txt2)):
        open(f"{d}/{nm.replace('down','down_arrow').replace('up','up_arrow') if nm in ('down','up') else nm}.svg", "w").write(icon_svg(ico, col, 2.2, 48))
    for nm, ico, col in (("message_info", "info", tok["accent-text"]), ("message_warning", "triangle-alert", tok["warning"]),
                          ("message_critical", "circle-x", tok["danger"]), ("message_question", "circle-help", tok["accent-text"])):
        open(f"{d}/{nm}.svg", "w").write(icon_svg(ico, col, 1.6, 96))

def gen_icons():
    icons_dir = f"{QT}/res/icons"
    for name, lu in {**ICONS, **EXTRA}.items():
        open(f"{icons_dir}/{name}.svg", "w").write(icon_svg(lu, NEUTRAL))
    for name in ICONS:
        p = f"{icons_dir}/{name}.png"
        if os.path.exists(p): os.remove(p)

def gen_fonts():
    from fontTools import subset
    from fontTools.ttLib import TTFont
    out = f"{QT}/res/fonts"; os.makedirs(out, exist_ok=True)
    uni = list(range(0x20, 0x7f)) + list(range(0xa0, 0x180)) + [0x2013, 0x2014, 0x2018, 0x2019, 0x201c, 0x201d, 0x2022, 0x2026, 0x2192, 0x2190, 0x20ac, 0x2212]
    for w in ("Regular", "Medium", "SemiBold", "Bold"):
        src = f"{INTER}/Inter-{w}.otf"
        opt = subset.Options(); opt.layout_features = ["kern", "liga", "calt", "ccmp", "locl", "mark", "mkmk", "tnum", "case"]
        f = TTFont(src); s = subset.Subsetter(opt); s.populate(unicodes=uni); s.subset(f); f.save(f"{out}/Inter-{w}.otf")
        if w in ("Medium", "SemiBold"):
            f = TTFont(src); cm = f.getBestCmap(); glyphs = set(f.getGlyphOrder())
            for ch in "0123456789,.:":
                g = cm.get(ord(ch)); tf = (g + ".tf") if g else None
                if tf in glyphs:
                    for t in f["cmap"].tables:
                        if ord(ch) in t.cmap: t.cmap[ord(ch)] = tf
            for rec in f["name"].names:
                if rec.nameID in (1, 16): rec.string = "Inter Tabular"
                if rec.nameID in (4, 6): rec.string = rec.toUnicode().replace("Inter", "Inter Tabular")
            s = subset.Subsetter(opt); s.populate(unicodes=uni); s.subset(f); f.save(f"{out}/InterTabular-{w}.otf")

def contrast_report():
    rows = ["| Mode | Foreground | Background | Ratio | Requirement |", "|---|---|---|---|---|"]
    for mode, tok in MODES.items():
        pairs = [("text", "bg", 4.5), ("text", "surface", 4.5), ("text", "surface-2", 4.5), ("text-2", "surface", 4.5),
                 ("text-2", "bg", 4.5), ("text-2", "surface-2", 4.5), ("accent-text", "surface", 4.5), ("accent-text", "bg", 4.5),
                 ("success", "surface", 4.5), ("danger", "surface", 4.5), ("on-accent", "accent", 4.5), ("text-3", "surface", 3.0)]
        for fg, bg, need in pairs:
            bgc = over(tok[bg], tok["bg"]) if bg not in ("bg",) and tok[bg].startswith("rgba") else parse(tok[bg])[:3]
            fgc = over(tok[fg], tohex(bgc)) if tok[fg].startswith("rgba") else parse(tok[fg])[:3]
            r = contrast(fgc, bgc)
            rows.append(f"| {mode} | {fg} | {bg} | {r:.2f} | {'PASS' if r >= need else 'FAIL'} (>= {need}) |")
    return "\n".join(rows)

if __name__ == "__main__":
    for m, t in MODES.items(): write_ini(m, t); svg_indicators(m, t)
    gen_icons(); gen_fonts()
    os.makedirs(f"{ROOT}/docs/design", exist_ok=True)
    open(f"{ROOT}/docs/design/contrast-report.md", "w").write("# Contrast report (generated)\n\n" + contrast_report() + "\n")
    print("assets generated")
