# DEV wheel-log build — how to send your wheel's data back

> **中文要点**
>
> * **如果某个设备（无级滚轮 / 触控板）用起来不正常**，可以换用这个 **DEV 版**运行一下，
>   它会把你最近的滚轮参数记录成一个文件，**把那个文件发给作者**即可帮助定位问题。
> * **DEV 版和正式版不建议同时使用，二选一。** 两个 DLL 都装上会让每个滚轮被处理两次。
> * 两个版本的文件名不同，扩展列表里显示的名字也不同，用来区分：
>
>   | | 文件名 | 扩展列表里显示 |
>   |---|---|---|
>   | 正式版 | `reaper_smoothwheelscroll-x64.dll` | `Smooth Wheel Scroll 1.7.1` |
>   | **DEV 版** | `reaper_smoothwheelscroll-x64-DEV.dll` | `Smooth Wheel Scroll 1.7.1 DEV (wheel log)` |

**English summary.** This is a **test build** of Smooth Wheel Scroll. It works like the normal one
and additionally records your last ~50 mouse-wheel messages into a small file, so a wheel the
author has no way to test can be measured on your machine. Use it when a device misbehaves, and send
the file back. **Do not run the DEV and release builds at the same time — pick one.** They have
different file names and different names in the Extensions list (see the table above).

**Who this is for:** anyone with a **free-spinning** wheel (a wheel with no notches) or a
**touchpad**. Those are the two senders whose values cannot be seen on the author's own machine, and
the classifier that tells them apart is built from a guess that has never been checked.

## What to do

1. **Remove the release DLL from `UserPlugins`** (or rename it), then put the DEV DLL there and
   restart REAPER. Only one of the two at a time.
2. **Scroll around with the wheel** you want measured. Nothing special is needed — but the more
   *separate* turns the better, and it helps a lot if you do these in this order, one after another:
   * **slow, single notches** (the wheel turned one click at a time);
   * then **a fast flick / spin**;
   * then, if you have one, the **touchpad**.
3. Close REAPER (or just stop; the file is written after every gesture anyway).
4. A file appears **next to the DLL**:

   ```
   <the folder your plugin DLL is in>\SmoothWheelScroll_wheel_log.txt
   ```

   That is `UserPlugins` — for a portable install `<REAPER>\UserPlugins\`, for a normal install
   `%APPDATA%\REAPER\UserPlugins\`. The plugin writes there and **nowhere else**: the path comes
   from the DLL's own location, so it cannot land in REAPER's install folder or anywhere else.

   Send that file to me (attach it to a forum post or a GitHub issue).

   If **no file appears**, that folder is not writable for your user (this can happen if REAPER
   lives under `Program Files`). The plugin says so in its own log at
   `%TEMP%\SmoothWheelScroll.log` if you happen to have a debug build; otherwise just tell me — it
   is a permissions thing, not a broken plugin.

## What is in it, and what is not

**In it:** for each of the last ~50 wheel messages — how long after the previous one it arrived,
the raw delta the wheel reported, the message's key state and extra-info word, the name of the
window class under the cursor, what the plugin decided the wheel was, and whether it animated the
message. Plus a summary line counting the distinct deltas seen per verdict ("what does this wheel
actually report?").

**Not in it:** no project paths, no track names, no media, no REAPER preferences, no settings of
yours beyond the plugin's own four feel values. The file is written to be safe to share; you can
open it in Notepad and read every line before sending it.

## Notes

* The file is **overwritten** as you scroll (it always holds the most recent messages), and it
  **never grows**.
* It is written into the same folder as the DLL, which is the one place this plugin is allowed to
  write. Delete the file when you are done.
* The DEV build is a diagnostic tool, not a different plugin: it animates exactly like the release.
  The logging code is compiled out of every normal build, so a shipped DLL cannot write anything.
  **When you are finished, put the release DLL back** (or keep the DEV one if you prefer — but only
  one).
