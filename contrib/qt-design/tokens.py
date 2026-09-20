"""Design tokens for the DigiWage desktop wallet: the single source of truth.

build_assets.py turns these into src/qt/res/styles/{dark,light}.ini (read at
runtime and substituted into the QSS templates), the themed indicator SVGs and
the contrast report in docs/design.
"""

# 8pt spacing rhythm with 4pt half-steps, radii and type scale (px).
SPACING = {"xs": 4, "sm": 8, "md": 12, "lg": 16, "xl": 24, "xxl": 32}
RADIUS = {"s": 8, "m": 12, "l": 16}
TYPE = {"caption": 11, "body": 13, "label": 12, "title": 20, "headline": 28, "hero": 40}
MOTION_MS = {"fast": 120, "base": 180, "slow": 240}

DARK = {
    "bg": "#131316", "surface": "#1c1c21", "surface-2": "#24242b", "surface-3": "#2c2c34",
    "border": "rgba(255,255,255,18)", "border-strong": "rgba(255,255,255,40)",
    "text": "#f5f5f7", "text-2": "rgba(255,255,255,158)", "text-3": "rgba(255,255,255,96)",
    "accent": "#f0c454", "accent-hover": "#f4d074", "accent-pressed": "#d9ae3f",
    "on-accent": "#17140b", "accent-text": "#f0c454", "accent-soft": "rgba(240,196,84,31)",
    "success": "#3dd598", "success-soft": "rgba(61,213,152,31)",
    "danger": "#ff6b6b", "danger-soft": "rgba(255,107,107,31)", "warning": "#ffb547",
    "hover": "rgba(255,255,255,13)", "pressed": "rgba(255,255,255,26)",
    "selection": "rgba(240,196,84,46)", "scrim": "rgba(0,0,0,150)",
}
LIGHT = {
    "bg": "#f5f5f7", "surface": "#ffffff", "surface-2": "#f0f0f3", "surface-3": "#e7e7ec",
    "border": "rgba(0,0,0,26)", "border-strong": "rgba(0,0,0,56)",
    "text": "#16161a", "text-2": "rgba(22,22,26,178)", "text-3": "rgba(22,22,26,120)",
    "accent": "#f0c454", "accent-hover": "#e8b93f", "accent-pressed": "#d4a52e",
    "on-accent": "#17140b", "accent-text": "#7a5a00", "accent-soft": "rgba(240,196,84,72)",
    "success": "#0b7f58", "success-soft": "rgba(11,127,88,26)",
    "danger": "#c9302c", "danger-soft": "rgba(201,48,44,26)", "warning": "#8f5c00",
    "hover": "rgba(0,0,0,10)", "pressed": "rgba(0,0,0,22)",
    "selection": "rgba(240,196,84,96)", "scrim": "rgba(20,20,26,120)",
}
MODES = {"dark": DARK, "light": LIGHT}
