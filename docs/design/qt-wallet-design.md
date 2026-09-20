# DigiWage desktop wallet — design system

The Qt wallet ships exactly two appearances, **Dark** (default) and **Light**. Both are generated from one
token file, so there is no per-theme stylesheet to keep in sync.

## Principles

1. **One hero, calm support.** Each page has a single focal element (the total balance on Overview) and quiet cards around it.
2. **Restrained colour.** Neutral layered surfaces; the gold accent is reserved for the primary action, the active navigation item, focus and key state.
3. **Numbers are the product.** Amounts use a tabular-figure face ("Inter Tabular") so columns and animated changes never jitter.
4. **8pt rhythm.** Spacing 4/8/12/16/24/32, radii 8/12/16, 24px page padding, 12px dialog rhythm.
5. **Depth by layering, not shadow.** Background < surface < surface-2 < surface-3, separated by 1px soft hairlines.
6. **Motion is optional.** Transitions are 120–200ms eased and are skipped entirely when *Reduce motion* is on.
7. **Progressive disclosure.** Coin control, fee tuning and RPC tools stay one click away, never in the default path.
8. **Honest empty states.** An empty list says what will appear there instead of showing a blank box.

## Tokens (source of truth: `contrib/qt-design/tokens.py`)

| Token | Dark | Light |
|---|---|---|
| bg | #131316 | #f5f5f7 |
| surface / -2 / -3 | #1c1c21 / #24242b / #2c2c34 | #ffffff / #f0f0f3 / #e8e8ec |
| border | white 7% | black 10% |
| text / -2 / -3 | #fff / 62% / 36% | #16161a / 70% / 47% |
| accent | #f0c454 (hover #f4d074, text on accent #17140b) | same fill, darker accent text for AA |
| success / danger | #3dd598 / #ff6b6b | AA-adjusted variants |

Type scale: caption 11, label 12, body 13, title 20, headline 28, hero 40 (Inter, weights 400/500/600/700).
All text/background pairs are checked numerically by the generator; the result is in `contrast-report.md` (0 failures; the disabled/placeholder tier is exempt and checked at 3:1).

## Pipeline

```
contrib/qt-design/tokens.py       tokens
contrib/qt-design/build_assets.py -> src/qt/res/styles/{dark,light}.ini, mode SVG indicators,
                                     themed icon set (Lucide, ISC), Inter subsets, contrast report
src/qt/res/styles/templates/*.qss token-templated QSS (@token@ placeholders)
src/qt/styleSheet.{h,cpp}         theme engine: template expansion, palette, fonts, live switch
src/qt/themedicon.{h,cpp}         SVG icon engine recoloured at paint time, DPR aware
```

Switching mode (`StyleSheet::setMode`) rebuilds the palette and application stylesheet, re-applies every registered widget stylesheet,
emits `modeChanged` (cached-colour widgets subscribe) and repaints — no restart. The choice is stored in QSettings (`Appearance`: `dark`/`light`; `ReduceMotion`).
The old multi-theme setting is removed on first start.

## Components

- **Navigation rail**: brand row, icon + label items (active item = accent-soft pill), wallet/network/lock status at the bottom.
- **Header**: quick Light/Dark toggle at the right edge.
- **Buttons**: primary (gold), secondary, outline, ghost, destructive; visible 1px focus ring.
- **Cards**: `card="true"` (surface), `card="hero"` (stronger border), `card="inset"` (surface-2).
- **Chips / captions / numerics** via dynamic properties (`chip`, `caption`, `numeric`, `hero`).
- **Segmented control** for Dark | Light in Options > Display.
- **Dialogs**: shared 24px padding and 12px spacing, text-only button boxes.

## Verification

`contrib/qt-design/screenshots.sh <dir> [scale]` starts the wallet on a throw-away data directory (test chain, read-only peer) and saves every page and dialog in both modes (`-uitour=<dir>`).
Screenshots in `docs/design/screenshots/`.
