# Handover: lxhieu sprinkler controller (valve stuck-open / zone-switch chaos)

Status as of 2026-09-27: **unresolved**. Multiple fixes landed and each one
solved the specific symptom reported, but the user still isn't satisfied —
switching between zones is still "rối loạn" (chaotic/messed up). Needs a
fresh, more rigorous session, ideally with real device logs instead of more
static-analysis guessing.

## Hardware

- ESP8266-01S (`esp01_1m`, 1MB flash — tight for OTA).
- On-board 4-channel relay board (LC Technology, STC15L101EW-driven). The 4
  relays are **not** wired to GPIOs — the ESP talks to the board's own MCU
  over its one hardware UART (`GPIO1`/`GPIO3`, 115200 baud) with 4-byte frames:
  `[0xA0, relay_no, state, checksum]`, `checksum = (0xA0+relay_no+state)&0xFF`.
- Relay 1 = Valve 1 (zone 1), Relay 2 = Valve 2 (zone 2), Relay 3 = Pump,
  Relay 4 = unrelated manual light switch.
- Because TX0/RX0 are dedicated to this UART link, `logger:` is UART-disabled
  (`baud_rate: 0`). Logs are only reachable via MQTT's default `log_topic`
  (no `api:` — see below for why).

## Files

- [esphome/lxhieu.yaml](../../esphome/lxhieu.yaml) — the live device config.
  Pure ESPHome YAML (switches + `script:`), **not** using the custom
  `sprinkler:` component.
- [components/sprinkler](../sprinkler) — the original custom C++ component.
  No longer used by lxhieu, but still in the repo (other devices might use
  it, or it could be revisited later). A speculative fix was applied to it
  (see below) but **never verified** — no compile or hardware test.

## How we got here

1. User's original `lxhieu.yaml` used the `sprinkler:` component
   (`components/sprinkler`) to drive 2 valves + 1 shared pump with
   start/stop delays. Symptom: pump turns off correctly on zone shutdown,
   but the valve never physically closes (stuck open indefinitely) — both
   on manual "Zone" switch-off and on natural cycle completion. Also:
   starting zone 2 while zone 1 was stuck caused relay chatter.

2. Static-read of `components/sprinkler/sprinkler.cpp` (diffed against the
   upstream ESPHome `sprinkler` component via the pip-installed package —
   no meaningful divergence found there) turned up one **real, confirmed**
   logic bug: the controller's own state-machine timer
   (`fsm_transition_from_shutdown_` / `fsm_transition_from_valve_run_` /
   the `STARTING` case in `fsm_transition_()`) computed its duration from
   `run_duration()` alone, while `SprinklerValveOperator`'s own internal
   completion check uses `start_delay_ + run_duration_` — a real few-second
   race between the two clocks. Applied a fix: added `+ this->start_delay_`
   to all three computations, and made `fsm_transition_from_valve_run_()`
   always call `vo.stop()` on completion (not just in the "interrupted"
   branch). **This fix was committed to `components/sprinkler/sprinkler.cpp`
   but never compiled or tested on hardware** (PlatformIO couldn't run in
   the sandbox this was done in — permissions issue unrelated to the code).
   If anyone revisits the `sprinkler:` component, start by actually
   verifying this fix on real hardware before trusting it.

3. Given no serial log was available and debugging a 2-layer custom C++
   FSM blind was unproductive, the user decided to **stop using the
   `sprinkler:` component** and reimplement zone sequencing directly in
   ESPHome YAML (`script:` + `delay:`), on the reasoning that it's simpler
   to read/debug top-to-bottom. This produced `esphome/lxhieu.yaml`.

4. The YAML rewrite then went through its own string of bugs, **all traced
   to the same underlying cause**: this specific cheap relay board's
   onboard MCU does not reliably handle two UART command frames sent with
   no real time gap between them — whichever command doesn't get a real
   gap after it can be dropped or otherwise not take physical effect, even
   though the ESPHome-side software has already updated its own switch
   `.state` and considers the command "sent". Confirmed independently: a
   flat/manual test config with 4 plain switches and no automatic
   sequencing (so commands are always human-click-paced, i.e. always
   several hundred ms to seconds apart) turns every relay on/off reliably
   every time — ruling out a hardware/relay defect.

   Chronological fixes (see commit log below for exact diffs):
   - `run_zone`'s leading "defensive cleanup" sent OFF unconditionally to
     pump/relay1/relay2, so a normal zone start fired OFF immediately
     followed by ON to the *same* relay → dropped ON → valve never opened.
     Fixed: only send OFF where `.state` is actually true.
   - `stop_watering` unconditionally turned off relay1 *then* relay2 back
     to back regardless of which was active → same class of drop. Fixed
     the same way (state-guarded).
   - `run_cycle`'s zone1→zone2 auto-advance chained zone 1's very last
     action (`relay1.turn_off()`) directly into zone 2's first action
     (`relay2.turn_on()`) with **zero** delay between two separate script
     instances. Added a delay at the top of `run_zone` to guarantee a gap
     after whatever preceded it.
   - Testing then showed the real settle time needed after a valve-off
     command is more like 5–10s, not the 300ms first tried — bumped to a
     real 10s delay (this is a guess with margin, not a measured minimum).
   - Direct zone-switch collision (flipping Zone 2 on while Zone 1 is still
     running) needed the exact same "close down, wait, then open" handling
     — added a state-guarded close-everything step before opening the new
     valve.
   - That guarded close step itself repeated the original mistake: it
     packed `pump_relay`/`relay1`/`relay2`'s `turn_off()` calls into **one
     lambda**, which still sends the frames back to back with zero gap.
     Split into separate sequential `if: / switch.turn_off: / delay:`
     steps — one relay change per step, always followed by a real delay,
     never more than one `turn_off()`/`turn_on()` call per lambda.
   - Finally consolidated all the "turn off whatever's on, in order, with
     real delays between each" logic that had been duplicated 3 times
     (`run_zone`'s lead-in, `run_zone`'s own end-of-cycle close,
     `stop_watering`) into one shared `close_all` script that the other
     two just `script.execute` + `script.wait` on. This was meant to also
     fix a related display bug (a zone's front-end switch staying "on"
     forever after a handoff even though its relay had actually turned
     off) since there is now exactly one place that ever sets
     `active_zone = -1`.

5. **Latest user report after all of the above**: switching between zones
   is still "rối loạn" (messed up) — not yet reproduced/diagnosed in
   detail before the handover was requested. This is the open problem for
   the next session.

## Commits (chronological, all on `main`)

```
e6a059f Add lxhieu sprinkler config using plain scripts instead of sprinkler: component
9ae6131 Fix valve not turning on: avoid back-to-back OFF+ON UART frames to same relay
844741b Drop api: to shrink firmware back under the OTA size budget
3124144 stop_watering: only send OFF to the valve that's actually open
63aa14f run_zone: add 300ms gap before opening a new valve
17cbd36 Give valve 1 a real 10s settle window before the next zone can open
edb83f4 run_zone: never overlap two valves on a direct zone-switch handoff
08ba226 run_zone: sequence pump/valve OFF one at a time, not from one lambda
18dfaeb Consolidate shutdown sequencing into one close_all script
```

The `components/sprinkler/sprinkler.cpp` timer-race fix landed inside the
`9ae6131` commit (bundled with the OTA-frame fix — worth its own commit if
this component gets revisited).

## What's genuinely unverified / weak in the current design

- **No hardware confirmation of any of this.** Every fix in this session
  was applied from the user's verbal description of symptoms and static
  reasoning about ESPHome codegen + UART timing, then pushed for the user
  to flash and test themselves. This session's sandbox cannot compile
  (PlatformIO permissions) or flash/observe the real board.
- **The 10s settle delay is a guess, not a measurement.** Nobody has timed
  how long this valve/relay board combo actually needs between an OFF
  command and it being safe to send the next command. It may need more,
  it may need less (wasting real time on every zone transition when it's
  more than necessary is itself a UX complaint — see "hơi phản cảm về cảm
  giác điều khiển" from the user).
- **No logs have actually been captured and read this session.** MQTT's
  default `log_topic` should already be emitting ESPHome's log lines
  wirelessly (this was confirmed by reading `components/mqtt/__init__.py`
  in the pip-installed esphome package: `log_topic` defaults to enabled
  unless explicitly turned off) — but nobody has subscribed to it and
  actually watched real timing during a zone-switch test. This is very
  likely the fastest way to make real progress instead of more guessing:
  add a few `logger.log:` lines with `millis()` timestamps around each
  UART-triggering action, have the user reproduce the "rối loạn" scenario,
  and read back the actual sequence/timing of what happened.
- **The core theory (back-to-back UART frames get dropped/mishandled by
  this specific board) has explained every symptom seen so far and hasn't
  been contradicted, but it's also never been directly confirmed with a
  logic analyzer or even just consistent, deliberate spacing experiments.**
  It's the leading hypothesis, not a certainty.

## Suggested starting point for the next session

1. Ask the user for the **exact** repro steps and observed behavvior for
   "rối loạn khi chuyển zone" (which switch, what happens on the display,
   what happens physically, in what order, roughly how many seconds
   between each observation) — the last few rounds of this session made
   real progress specifically when working from a precise, literal
   description of what was pressed and what happened, not from guessing.
2. Strongly consider adding `logger.log:` breadcrumbs (with `millis()`) at
   every relay/pump/UART transition point in `close_all`/`run_zone`, since
   MQTT logging is already available for free, and reading a real captured
   sequence beats another round of static-analysis speculation.
3. If the 10s delays turn out to be the actual UX complaint (not a
   correctness bug), consider making the settle time user-tunable (a
   `number:` entity) rather than a hardcoded constant, once a reasonable
   real minimum is known.
