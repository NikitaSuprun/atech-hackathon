# BOM, Pricing & Margin

> **The core insight in one sentence:** at Atech *retail* prices the hero build's parts cost **~$292**, so no third party could ever sell it for $249 — but at Atech's *internal* cost it's **~$120–175**, which is a healthy-margin flagship. **The economics only close as a first-party / licensed Atech product.**

> ⚠️ **Assumptions flagged.** Retail module prices are from Atech's published pricing. **Atech's internal COGS is estimated** — modeled at ~30–50% of retail module price (Atech manufactures these) plus ~$25–35 enclosure/assembly/packaging. All gross-margin figures below inherit that estimate. Treat COGS as a band, not a point.

---

## 1. Bill of materials — Arcade Duo (the hero, 2-board)

| Qty | Atech module | Retail ea. | Retail line |
|---:|---|---:|---:|
| 2 | Motherboard — 14-port | $49 | $98 |
| 12 | Light Grid (NeoPixel tile) | $9 | **$108** |
| 2 | Knob (rotary + button + LED ring) | $15 | $30 |
| 1 | Screen (ST7735 TFT) | $19 | $19 |
| 1 | Speaker | $19 | $19 |
| 2 | USB-C | $9 | $18 |
| | **Module subtotal (Atech retail)** | | **$292** |
| | + Enclosure / assembly / packaging | | ~$25–55 |
| | **All-in at Atech retail** | | **~$320–350** |
| | **All-in at Atech internal COGS** *(est.)* | | **~$120–175** |

**Read this twice:** the **Light Grid line alone is $108** — the console *is* a Light-Grid pull-through machine. Twelve of a $9 module nobody otherwise buys twelve of.

---

## 2. SKU ladder & pricing

| SKU | Contents | Price | Est. gross margin | Notes |
|---|---|---:|---:|---|
| **Arcade OS** | Firmware + game library (digital) for existing kit owners | **$39** | **~85–95%** | Near-zero COGS; the wedge that converts the installed base |
| **Arcade Solo** | 1 Motherboard, smaller matrix, knobs, screen, speaker | **$149** | **~30–50%** *(est.)* | Single-board handheld; ~half the Duo's module count |
| **Arcade Duo** ⭐ | Full 2-board console, 108-px display (BOM above) | **$249** | **30–52%** | Hero SKU. Margin band = COGS band (see §3) |
| **Arcade Max** | Bigger matrix (24–32 tiles) + WiFi — **roadmap** | **$399** | **~35–50%** *(est.)* | Pulls through *even more* Light Grids |
| **Arcade Pass** | Yearly game/ambient content, or à-la-carte packs | **$19/yr** or **$2–4/pack** | **~95%+** | Recurring "blade" revenue on owned rails |

---

## 3. Margin analysis — Arcade Duo at $249

Because COGS is a band, margin is a band. Same $249 price, three cost scenarios:

| Scenario | All-in COGS *(est.)* | Gross profit / unit | Gross margin |
|---|---:|---:|---:|
| **Optimistic** | $120 | $129 | **52%** |
| **Mid** | ~$148 | ~$101 | **~40%** |
| **Conservative** | $175 | $74 | **30%** |

**Even the conservative case clears 30% hardware margin** — and that's *before* the razor-and-blades layer. Two things compound on top:

- **Pull-through is the real prize.** Every Duo also books the *module* revenue/margin: 2 Motherboards, 12 Light Grids, 2 Knobs, a Screen, a Speaker, 2 USB-C. The console margin and the module margin are the *same sale*.
- **Recurring stacks on installed base.** Arcade Pass ($19/yr, ~95% GM) and packs ($2–4) turn each unit into an annuity. At even 25% Pass attach on the installed base, lifetime value moves well above the one-time hardware margin.

**Why $249 and not lower:** the competitive frame supports it (Playdate is $229 *console-only*; Tidbyt $199 *display-only*), and it's the price that keeps the hero SKU comfortably above the conservative COGS line while undercutting the sum-of-retail-parts ($292) — a gap only a first-party vendor can open.

---

## 4. Competitive anchors

| Product | Price | Category | What it does | What it *doesn't* |
|---|---:|---|---|---|
| **Playdate** | $229 | Handheld console | Real games, **has a game store** | No ambient display; plays solo |
| **Tidbyt** | $179–199 | Ambient LED display | Pretty pixel dashboards | You can't *play* it |
| **Divoom Pixoo-64** | $199 | Ambient LED display | 64×64 pixel art / clock | Loops content; not a console |
| **Arduboy** | $79 | Tiny handheld | 1-bit indie games | Solo, tiny, no display mode |
| **PicoSystem** | $80 | Tiny handheld | Pico-8-style games | Solo, tiny |
| **Thumby** | $30 | Keychain handheld | Micro games | Novelty scale |
| ⚠️ **Ulanzi / Awtrix** | ~$50–60 | Smart pixel clock | Clock + **already runs mini-games** | Toy-grade play, single-player, closed |
| **→ Atech Arcade Duo** | **$249** | **Console + display** | **Console-grade multiplayer play *and* ambient display, on the Atech ecosystem** | — |

**The Ulanzi/Awtrix warning:** a ~$55 pixel clock already does mini-games, so "LEDs that play games" is not, by itself, defensible. **We differentiate on console-grade play, real multiplayer, tactile knob controls, a growing library, and the Atech kit underneath** — not on "it has games."

**The positioning gap we own:** everyone else is *either* a player *or* a display. Atech Arcade is the only one on this list that is credibly **both**, which is exactly what justifies a Playdate-tier price with Tidbyt-tier everyday utility.

---

*Retail prices per Atech published pricing (Motherboard-14port $49, Screen $19, Speaker $19, Knob $15, Light Grid $9, Button $9, USB-C $9; Early-Adopter kits $99/$159). COGS % is an estimate and the single biggest lever on every margin number here — validate against Atech's real transfer cost before quoting externally.*
