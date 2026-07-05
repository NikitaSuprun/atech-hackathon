# Atech Arcade — Handover (start here)

**Atech Arcade** is an extensible ESP32-S3 retro game console + ambient pixel display,
built from stock [atech.dev](https://atech.dev) modules (no soldering): a **brain** board
runs an on-device OS + 10 games and streams frames to a dumb, glowing **6×18 / 108-pixel**
screen board. It **shipped on 2026-07-05** — the console is live on `main` and flashed on
both ESP32-S3 boards, with a live browser twin. The original standalone Pong is archived
under tag `v1.0-pong`.

This file is just the signpost — the living handover is in-repo:

- **[README.md](README.md)** — what it is, the 10-game menu, quick-start (no hardware), architecture.
- **[docs/HANDOVER.md](docs/HANDOVER.md)** — developer onboarding: repo layout, build/test/run, key modules, invariants, gotchas.
- **[docs/STATE.md](docs/STATE.md)** — current state: what's verified, what's on hardware, what's roadmap.
- **[docs/ARCHITECTURE.md](docs/ARCHITECTURE.md)** — contracts, the frame path, the light engine, design decisions.
- **[docs/HARDWARE.md](docs/HARDWARE.md)** — the two boards, pin map, flash flow, power, board-to-board link.
- **[gtm/](gtm/README.md)** — product/business framing (SKUs, BOM/margin, pitch, roadmap).

Small self-contained commits, no AI co-author trailer. **Powered by Atech.**
