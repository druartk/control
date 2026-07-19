# LILYGO EPD47 vendored driver

These files are the minimal native ESP-IDF display and font subset copied
unchanged from the official `Xinyuan-LilyGO/LilyGo-EPD47` `esp32s3` branch at
commit `5b1adc7100cce95529ace84278fcdc3aa0439c85` (library release lineage 1.0.1).

The original GPL-3.0 license is retained in `LICENSE.upstream`. The Arduino-only
touch source is intentionally excluded; `main/touch` contains the native
ESP-IDF GT911 implementation used by this project.
