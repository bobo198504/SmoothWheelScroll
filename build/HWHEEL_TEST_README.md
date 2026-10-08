# Smooth Wheel Scroll — horizontal wheel test build

## What this is

A **test build** with horizontal wheel support (tilt wheel, MX Master thumb wheel).
Your patch's two ideas are both in it, but implemented differently — see the notes at the bottom.

**This is NOT a release build.** There is no release yet; I want your result first.

---

## Install

1. Close REAPER.
2. Put `reaper_smoothwheelscroll-x64-DEV.dll` into `REAPER\UserPlugins\`.
   (Delete or move any other `reaper_smoothwheelscroll*.dll` from that folder first —
   two copies at once would install two hooks and animate every wheel twice.)
3. Start REAPER.

## What to test

1. **Thumb wheel** — your HorizWheel binding (you said 977) and Ctrl+HorizWheel (979).
   Does it glide now, instead of jumping?
2. **Tilt wheel**, if your mouse has one.
3. **Touchpad sideways swipe** — it should stay *native* (not smoothed). That is intended:
   the plugin deliberately leaves touchpads alone, same as for the vertical wheel.
4. Normal vertical scrolling should feel exactly as before.

## If anything is wrong, send me this file

```
REAPER\UserPlugins\SmoothWheelScroll_wheel_log.txt
```

It is written next to the DLL. **It contains no project paths, track names, media or
REAPER preferences** — only wheel values, key state, window class names and the settings,
so it is safe to share.

The `axis` column is the point: **`V`** = ordinary vertical wheel, **`H`** = horizontal one.
If your thumb wheel produces **no `H` rows at all**, that is itself the finding — tell me.

It is rewritten at the end of each gesture and when you close REAPER, and holds only the
last 50 messages, so: **scroll a bit right before you send it.**

---

## Notes on your patch (why it is not exactly what went in)

Both of your changes were needed, and both are in — thank you, the diagnosis was right.

**1. The table rows.** You made the table path also read the action name for "mousewheel".
I gave the table rows `relativeAction = true` directly instead, because the real bug was that
*the same action got two different answers* depending on whether the table or the name rule
matched it first — the table is tried first, so its `false` won. Declaring it in the row removes
the disagreement at its source and costs no name lookup on the wheel path.

**2. `WM_MOUSEHWHEEL`.** Kept, and it is required — the first change alone is not enough,
because the latch could never be armed without it.

Two details I could not verify and you can:

- **the delta a thumb wheel actually reports.** Your log showed `val=15 relmode=1` for the
  action, but not the raw `WM_MOUSEHWHEEL` delta. The device classifier judges by the raw
  value, and **the horizontal wheel's sign convention is the opposite of the vertical one**
  (right is positive; for the vertical wheel, up is positive). The classification code was
  copied from the vertical path, and nobody has checked it against real hardware.
- **whether a touchpad's sideways swipe is correctly passed through** rather than smoothed.

The `H` rows in the log answer both.
