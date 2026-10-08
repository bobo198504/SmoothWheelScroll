# Horizontal wheel — preview build

**This is a preview build, not a release.** `v1.7.4+dev1008`.

Adds the horizontal wheel (tilt wheel, MX Master thumb wheel) to the smoothing.
Confirmed by a forum report that the thumb wheel never engaged; diagnosis and both
required fixes came from that report.

## Download

`reaper_smoothwheelscroll-x64-DEV.dll` — attached to this pre-release.

## Install

1. Close REAPER.
2. Put the DLL into `REAPER\UserPlugins\`.
   Remove any other `reaper_smoothwheelscroll*.dll` from that folder first — two copies
   at once install two hooks and animate every wheel twice.
3. Start REAPER.

## Test

- **Thumb wheel** (HorizWheel binding, e.g. 977) and **Ctrl+HorizWheel** (e.g. 979):
  should glide instead of jumping.
- **Tilt wheel**, if the mouse has one.
- **Touchpad sideways swipe**: should stay *native*, not smoothed. This is deliberate —
  a touchpad is already continuous, and a second easing on top of it is wrong. Same
  treatment the vertical wheel gets.
- **Vertical scrolling**: should feel exactly as before.

## This build logs both wheels

The DEV build writes `SmoothWheelScroll_wheel_log.txt` next to the DLL. It is meant to be
shared: it holds wheel values, key state and window class names — **no project paths,
track names, media or REAPER preferences**.

The `axis` column separates the two wheels: **`V`** = vertical (`WM_MOUSEWHEEL`),
**`H`** = horizontal (`WM_MOUSEHWHEEL`).

It keeps the last 50 messages and is rewritten at the end of each gesture, so **scroll a
little right before copying it**. If it shows **no `H` rows at all**, that is itself a
result worth reporting.

## What is in it

The horizontal wheel is a first-class stream, built to mirror the vertical one:

- **Same device filter**, through a **separate tracker**. The two wheels must not share
  one: the classifier judges by "how many distinct magnitudes appeared recently", so
  feeding both wheels into one window makes each look irregular — i.e. both would be
  misread as a touchpad and neither would be smoothed.
- **Same model**, same delivery rules per action.
- **Separate latch**, so one wheel's message cannot vouch for (or erase) the other's.

Also fixed: the action table rows now declare themselves relative actions instead of
inheriting a default that disagreed with what the name rule says about the same action.
The table is tried first, so its answer won and the horizontal actions demanded a wheel
latch that a horizontal wheel message can never arm.

## Known unknowns

This was developed **without a horizontal-wheel mouse on hand**, so two things are
written to mirror the vertical wheel but are **unverified against hardware**:

- the raw delta a tilt/thumb wheel reports, and the sign convention. **The horizontal
  wheel's sign is the opposite of the vertical one** (right positive; for vertical, up is
  positive), and the classifier was copied from the vertical path.
- whether a touchpad's sideways swipe is correctly identified and passed through.

The `H` rows in the log answer both.
