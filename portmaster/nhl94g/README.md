## Notes

NHL '94 Genesis C-port on Overbar for PortMaster (RG35XX H700 / muOS first).

This port is a derivative of [NHL94Genesis](https://github.com/abdulahmad/NHL94Genesis) by **abdulahmad**, with **chaos**, **StaticEck**, **McMarkis (Mhopkinsinc)** and **Drezz**, and special thanks to **smozoma** and **kingraph**. Host overlay/scaler: Overbar (PulseMan MIT). Carry this credit in any fork or release.

Private zip first. Do not upload to portmaster.games until Abdul says the catalogue is allowed (EA trademarks).

The ROM is never in the zip. Copy your 1 MB US/Europe dump to `roms/nhl94.gen`.

## Controls

Play uses SDL GameController. On an RG35XX with Nintendo face labels: **Y** pass, **B** shoot, **A** dump/hold, **Start** pause, **Select** overlay, **Select+Start** quit. **L1** held rewinds, **R1** held fast-forwards.

## Handheld

60 Hz, vsync on, overlay only (no menu bar), SDL_Renderer / KMSDRM. No desktop OpenGL.

## Compile

Cross-compile in the PortMaster aarch64 builder from this repository (`main` or this Overbar branch). `make nhl94`, strip, rename to `nhl94g.aarch64`, `ldd` into `libs.aarch64/`. `portmaster/pack.sh` assembles the zip.
