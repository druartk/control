# LILYGO EPD47 vendored driver

These files are the minimal native ESP-IDF display and font subset vendored
from the official `Xinyuan-LilyGO/LilyGo-EPD47` `esp32s3` branch at
commit `5b1adc7100cce95529ace84278fcdc3aa0439c85` (library release lineage 1.0.1).

The original GPL-3.0 license is retained in `LICENSE.upstream`. The Arduino-only
touch source is intentionally excluded; `main/touch` contains the native
ESP-IDF GT911 implementation used by this project.

The display uses the factory driver's legacy RMT API, supported by the pinned
ESP-IDF 5.4.2. It copies each pulse into RMT memory and starts it before
returning. Frame-start STV deasserts after that start without waiting for the
pulse to finish, matching the factory sequence. Row latching and frame shutdown
still wait for CKV and LCD DMA. Pulse durations, power sequence and grayscale
lookup tables remain unchanged. Do not combine this with the new RMT driver.
The factory settling delay and the waits between grayscale passes are retained.

Frame start runs from IRAM. Its 10 us high / 10 us low CKV symbol is prepared
before a short critical section, which only starts RMT through register writes
and releases STV. Completion is polled after leaving the critical section;
the normal RMT driver resumes for the remaining pulses. This keeps Wi-Fi
preemption and flash-cache stalls out of the STV transition without calling
FreeRTOS semaphore APIs with interrupts disabled.

The row feeder runs on the core that initialized LCD/RMT. The legacy RMT ISR
wakes its waiter before finishing channel cleanup; keeping them on the same
core prevents the next row from racing that cleanup on another CPU.

All display pads select digital GPIO and normal drive strength before the
LCD/RMT signals are attached. The full application was observed leaving the
eight data pads at drive setting 0; peripheral routing alone does not reset
their strength or release RTC routing. Direct GPIO writes handle both S3
register banks, including STH on GPIO40, without an out-of-range shift.

Regression checks: `python3 tools/dev/test-epd-gpio.py`,
`python3 tools/dev/test-epd-synchronization.py`,
`python3 tools/dev/test-epd-timing.py`, `python3 tools/dev/test-epd-regions.py`,
and `python3 tools/dev/test-display-errors.py`.
