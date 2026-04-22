# OBD2 — Freematics ONE+ firmware (Amir’s fork)

ESP32 OBD2 / telemetry work based on the **Freematics ONE+** stack, with a richer WiFi portal, runtime network toggles, and other tweaks maintained here for the community.

**Author:** [Amir (@onlyamir007)](https://github.com/onlyamir007)  
**If this build helps you:** [Buy Me a Coffee — @amirtechhub](https://buymeacoffee.com/amirtechhub)

## Upstream (original source)

The core sketches and libraries come from the official **Freematics** project:

- **Repository:** [github.com/stanleyhuangyc/Freematics](https://github.com/stanleyhuangyc/Freematics)  
- **Product info:** [Freematics ONE+](https://freematics.com/products/freematics-one-plus-model-b/)  
- **Original license:** source files are marked *Distributed under BSD license*; this repo adds a full-text **[LICENSE](LICENSE)** (BSD 3-Clause) for clarity. **Do not remove** existing copyright/headers in those files.

This repository is an **independent** public project; it is not affiliated with or endorsed by Freematics. Use the name and trademarks of third parties in a way that does not suggest official endorsement.

## License

- See **[LICENSE](LICENSE)** (BSD 3-Clause).
- Keep all **per-file** copyright and license notices in Freematics-derived (and any third-party) sources when you copy or redistribute.

## What’s in this repo

| Path | Description |
|------|-------------|
| [firmware_v5/datalogger](firmware_v5/datalogger) | Datalogger sketch, WiFi portal, `netcfg` NVS toggles, System tab, etc. |
| [firmware_v5/telelogger](firmware_v5/telelogger) | Telelogger / hub telemetry; same portal patterns where applicable. |

Older trees (`firmware_v4`, `ESPRIT`, `libraries`, `server`) match the upstream layout for reference; primary maintenance here is under **`firmware_v5`**.

## Build notes

- **Arduino/ESP32:** use the Freematics-appropriate board package and this repo’s `firmware_v5/...` sketches.  
- **HTTP portal / `portal.js`:** the escaped portal bundle is large; ensure **`HTTP_BUFFER_SIZE`** in your Arduino `libraries/httpd/httpd.h` is high enough (e.g. 32 KiB) so the portal JS is not truncated.  
- After changing `wifi_portal.cpp` embedded JS sources, you may need to run `embed_js.py` and `patch_portal_cpp.py` in the relevant sketch folder (see comments in that source).

## Contributing

Issues and pull requests are welcome. Keep attribution and the **LICENSE** intact when you submit changes.

## Disclaimer

This software is provided **as is** (see LICENSE). OBD2 and safety decisions are your own; this is not a replacement for a qualified mechanic or a full diagnostic tool.
