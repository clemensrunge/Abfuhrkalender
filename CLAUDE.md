# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

An Arduino sketch for an ESP8266 (ESP32 mentioned but code uses ESP8266-specific headers) that fetches a German waste-collection calendar (`.ics` from awsh.de) over HTTPS and lights 3 NeoPixel RGB LEDs in the colour of the garbage can being picked up today/tomorrow. Time is synced via NTP.

The entire program lives in `Abfuhrkalender.ino`. There is no build system, test suite, or linter in the repo — the sketch is built and flashed with the Arduino IDE (no `arduino-cli` installed here). Required external library: `Adafruit_NeoPixel` (installed in the `~/Arduino/libraries` sketchbook). Debug output goes to Serial at 115200 baud.

## Secret files

`CalendarURL.h` and `WifiSecret.h` are committed as templates but are listed in `.gitignore` and marked `git update-index --assume-unchanged`, so local edits (real WiFi credentials in `SSID`/`PSWD`, the real `.ics` URL) never show up in `git status` and must never be committed. Keep it that way — do not run `--no-assume-unchanged` or stage these files.

## Architecture / control flow

- `loop()` runs `updateIcs()` on first boot and on each month change; otherwise it sleeps ~1 minute per iteration, blinking pixel 0 green as a heartbeat.
- `getIcs()` connects to WiFi if needed, does an HTTPS GET (BearSSL, `setInsecure()`, 40 KB receive buffer — heap is tight on ESP8266, hence the `[Free Heap]` debug prints and `yield()` calls to feed the watchdog), reads the response line by line (no streaming API), and calls `stopWifi()` on success to save energy.
- If `replaceYearStringInUrl` is set, `replaceYearInUrl()` substitutes the literal `yyyy` placeholder in `icsURL` with the current year, so the URL rolls over automatically each January.
- `parseIcsLine()` is a small state machine: a `DTSTART;VALUE=DATE:` line arms the trigger and stores the date; the following `SUMMARY:` line matches against `TrashTypeStrings` (German names from the provider) to classify the event into the fixed-size `events[]` array. `getIcs()` also stitches together lines the server wraps mid-date (continuation lines containing only digits) before parsing.
- `updateLeds()` runs once per day change: it scans `events[]` for today/tomorrow, then displays. One event: all 3 pixels in the trash colour for "tomorrow", only the middle pixel for "today". Multiple events: pixel 0 off, pixels 1..2 show one event colour each. Pixel 0 solid red signals that the last calendar fetch failed (stale data still displayed).
- `handleFailure()` distinguishes critical failure (no events cached → block for 10 min blinking red, then retry) from non-critical (old events still present → carry on, retry every loop).
- Years are stored as `uint8_t` offsets from 2000 (`event.year + 100 == tm_year`); dates are compared field-wise via `isEventOnDate()`. `myTimegm()` is a TZ-swapping `mktime` wrapper used to compute "tomorrow" in UTC.
- Trash-type colours are three parallel arrays (`TrashTypeRed/Green/Blue`) indexed by the `TrashType` enum; keep the enum, colour arrays, and `TrashTypeStrings` in sync when adding a type.
