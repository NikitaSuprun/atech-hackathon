# Roadmap

> **Everything on this page is next, not shipping at the demo.** What ships live at 18:00 is the **full OS with all ten games** on the real 108-pixel matrix, the console board (knobs, rings, color scoreboard, speaker), and a board-to-board link running over a **laptop USB bridge**. The two big post-hackathon bets below — **the game market** and **native WiFi** — are designed, partially in-tree, and deliberately *not* switched on for the demo.

---

## Bet 1 — The game market (the "blade")

Turn a fixed set of built-in games into an **open, growing library** players browse and install — the mechanism behind Arcade Pass and game packs.

**Why it's credible:** the games already share one **dependency-free C++ engine** that compiles unchanged into both the ESP32 firmware and a desktop simulator. A game is already a self-contained unit with a frozen contract (`reset / tick / render / status`). Cartridges generalize that boundary; they don't invent it.

| Phase | Deliverable | What it unlocks |
|---|---|---|
| **v0 (today)** | Games baked into firmware; OS-like menu | The console works, offline, forever |
| **v1 — native OTA** | **Signed manifest + versioned content API.** Console pulls a catalog, downloads new *native* game/ambient modules, self-updates over the air | Ship new titles without reflashing; Arcade Pass becomes real; telemetry on what's played |
| **v2 — cartridges** | **Sandboxed guest runtime — `wasm3` (WebAssembly) or `Berry` scripting** — so games are portable bytecode, not native firmware | Third-party & community titles; a real store; safe install of untrusted code; write once, run on Solo/Duo/Max |

**Design guardrails:** manifest is **versioned and signed** (a version byte already guards the on-wire protocol — same discipline); the content API is **forward-compatible** so an old console degrades gracefully; native OTA (v1) ships value *before* the heavier sandbox (v2). Ambient scenes ride the exact same rails as games.

---

## Bet 2 — WiFi (cut the laptop)

Today the two boards talk over USB through a host relay. The endpoint is a **direct wireless link and a phone that can join as a controller.**

**Why it's credible:** WiFi is **not a rewrite — it's a switch we chose not to flip in a hall full of 2.4 GHz kits.** The proof is already in the codebase:

- A full **WiFi SoftAP + UDP transport exists in-tree** alongside the active USB path.
- The link lives behind a **tiny swap interface (`PongLink`)** from day one — during the hackathon the entire transport was swapped to USB in ~20 minutes when venue RF misbehaved. The same seam swaps *back*, and swaps forward to ESP-NOW.
- The protocol is **already multi-controller**: input packets carry a `controllerId`, and the server learns each controller's address and replies to source. **A second controller — or a phone — joins with zero protocol change.**
- The link is **loss-proof by construction**: absolute (not delta) knob positions and wrapping one-shot event counters mean a dropped or duplicated packet self-heals. Wireless flakiness doesn't desync the game.

| Phase | Deliverable | What it unlocks |
|---|---|---|
| **v0 (today)** | USB serial bridge via host relay; WiFi/UDP present but idle | Rock-solid demo in a hostile RF room |
| **v1 — ESP-NOW two-board** | Swap `PongLink` to **ESP-NOW** (connectionless, low-latency, no router). Same 24-byte payloads; broadcast discovery → unicast | **No laptop.** Two boards pair and play standalone |
| **v2 — phone-join** | Phone joins as `controllerId 1..N` (SoftAP or ESP-NOW) — extra players, remote start, ambient/clock config from your pocket | 3–4 player party modes; setup UI; casual on-ramp |

---

## How the bets ladder into the SKUs

| Roadmap item | Lands in |
|---|---|
| Native OTA + game market (Bet 1 v1) | **Arcade Pass $19/yr**, game packs $2–4 |
| Cartridge sandbox (Bet 1 v2) | Third-party store; community titles across all SKUs |
| ESP-NOW wireless (Bet 2 v1) | Standalone **Arcade Duo**; required for **Arcade Max $399** |
| Phone-join (Bet 2 v2) | Party/multiplayer modes; friction-free gifting & setup |
| Bigger matrix + WiFi hardware | **Arcade Max** — pulls through 24–32 Light Grids |

---

*Sequencing rule: ship the revenue-unlocking, lower-risk half of each bet first — **native OTA before the cartridge sandbox; ESP-NOW before phone-join.** Nothing here blocks selling the Duo; each item deepens the moat and the margin after launch.*
