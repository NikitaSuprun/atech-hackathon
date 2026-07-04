# Business Model Canvas — Atech Arcade

> One-screen model for the strategic thesis: **Atech Arcade only makes sense as a first-party / licensed Atech product** — and *as* one, it's a flagship that pulls hardware through, opens new markets, and adds recurring revenue.

---

## Value proposition

- **For buyers:** the only ~$200-class device that is **both a console and an ambient display** — tactile, multiplayer, screen-free play *and* a pixel-art shelf piece, with a game library that keeps growing. No soldering, clicks together.
- **For Atech:** a **flagship that sells the kit.** One SKU pulls through five module types and twelve Light Grids, opens gamer/gift/classroom aisles, and stacks a razor-and-blades game business on hardware Atech already owns end to end.

---

## Customer segments

| Segment | Entry SKU | Job to be done |
|---|---|---|
| **Gamers / gadget enthusiasts** | Arcade Duo $249 | Couch-competitive, phone-free game night |
| **Gift buyers** | Arcade Duo $249 / Solo $149 | A sub-$250 "wow" object, zero assembly skill |
| **Classrooms / clubs / makerspaces** | Duo + Arcade OS | A finished build that shows what the kit becomes; then remix it |
| **Existing Atech kit owners** | Arcade OS $39 | "Make the modules I already own do something amazing tonight" |
| **Ambient-display shoppers** | Duo / Max | A Tidbyt/Divoom alternative that also plays |

---

## Revenue streams

- **Hardware (one-time):** Arcade **Solo $149**, **Duo $249**, **Max $399** — 30–52% est. gross margin (see [`bom-and-margin.md`](bom-and-margin.md)).
- **Module pull-through (one-time, same sale):** every Duo also books 2 Motherboards, 12 Light Grids, 2 Knobs, a Screen, a Speaker, 2 USB-C — the console margin and module margin are one transaction.
- **Software wedge (one-time):** **Arcade OS $39** to the existing installed base — near-pure margin, near-zero CAC.
- **Recurring (the blades):** **Arcade Pass $19/yr** and **game/ambient packs $2–4** — ~95% GM on rails Atech controls.

---

## Cost structure

- **COGS (est.):** ~$120–175 all-in per Duo — dominated by Atech's own module cost (so largely *internal* transfer, not external spend) + enclosure/assembly/packaging. **COGS % is the single biggest lever and is estimated.**
- **One-time platform:** enclosure tooling/injection molding; the game market backend (manifest/OTA/store); cartridge sandbox (`wasm3`/Berry) R&D.
- **Ongoing:** firmware + game/content development, QA, calibration tooling, support, storefront ops.
- **GTM:** listing/marketing on atech.dev, gift/education channels, seasonal.
- **Cost advantage:** shares Atech's existing supply chain, SDK, and store — **near-zero marginal platform cost**; the modules already exist and are already manufactured.

---

## Channels

atech.dev storefront (primary) · Early-Adopter kit cross-sell & Arcade OS upsell to the installed base · gift/retail and education/reseller channels for finished units · the console itself as a **homepage demo asset** that sells kits by existing.

---

## Key resources & activities

- **Resources:** the working two-board build; one dependency-free engine (firmware *and* simulator from the same source); the loss-proof link protocol + `PongLink` swap seam; the Atech module catalog, SDK, and store.
- **Activities:** grow the game library; ship native OTA then the cartridge sandbox; flip the link to ESP-NOW + phone-join; industrial design for the enclosure.

---

## Unfair advantage

1. **Only Atech can build it.** At Atech *retail* the Duo's parts are **~$292** — no third party can sell it at $249. At Atech's *internal* cost it's **~$120–175**. **First-party COGS is the moat**, and it's Atech's alone.
2. **Vertical control end to end** — silicon, modules, SDK, storefront, and now content. Razor *and* blades on one owned stack.
3. **Pull-through of the un-attachable SKU.** Nobody buys twelve Light Grids — until they buy this. The Duo *needs* exactly twelve; Max needs 24–32.
4. **Working proof + honest economics.** A live prototype and a P&L in Atech's own numbers — not a rendering. (WiFi link and game market are labeled roadmap; see [`roadmap.md`](roadmap.md).)
5. **Category of one.** Every competitor is *either* a console (Playdate) *or* a display (Tidbyt, Divoom). Being credibly **both** is the position — and it's ecosystem-defended, not just feature-defended.

---

*Strategic thesis in one line: **pitch it to Atech as their flagship build-it-yourself console + ambient display** — it sells the kit, expands the market from makers to gamers/gift/classroom, and adds a recurring game business on hardware Atech already owns. Powered by Atech.*
