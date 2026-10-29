# KING

The feature set takes ideas from BestClient. Everything here is implemented independently on top of TClient; no BestClient code is used.

KING is a DDNet client for KoG (King of Gores), based on [TClient](https://github.com/sjrc6/taterclient-ddnet) and DDNet.

## What KING adds

* **Input modes** (`kg_input_mode`):
  * `0` Custom: the TClient input settings are used as they are.
  * `1` Classic (manual margin, exactly like TClient): the old TClient fast input from before the Feb 2026 rewrite. `kg_classic_ticks` (1–5) works like the old `tc_fast_input`: your tee is shown that many whole ticks (20 ms each) ahead with your newest input.
  * `2` KING (default): one tick of fast input (`kg_fast_input_amount`, 20 ms), the safe auto margin, and light correction smoothing: small server corrections of your tee glide out over 40 ms instead of jumping (`kg_king_smooth`).
  * `3` Delta: a fixed offset in hundredths of a tick (`kg_delta_amount` 0–500, where 100 = 1 tick). It can sit between whole ticks; `kg_delta_others` shifts other tees too.
  * `4` Ping Comp: fast input is `kg_ping_comp`% of your measured ping, capped at 100 ms. It follows the ping slowly (at most 5 ms per second, 1 ms at a time) and uses auto margin.
  * `6` Best: offset (`kg_best_offset`, 0.01 ticks), latency comp (`kg_best_latency_comp`, % of ping), correction smoothing (`kg_best_smoothing`) and blend curve (`kg_best_interpolation`: linear/cubic/smooth). With `kg_best_preset 1` (auto) the offset stays at exactly one tick and only the smoothing time follows your measured jitter.
  * `5` Smooth: KING mode, but when the server corrects your own tee, the correction fades out over `kg_smooth_ms` instead of snapping. This is visual only: inputs and physics are not delayed. Teleports (more than 2 tiles) still snap.
* **Input helpers**:
  * Snap Tap (`kg_snap_tap`, `kg_snap_tap_delay`): when left and right are both held, the key pressed last wins instead of standing still.
  * 45° aim: `+kg_45` while held, or the `kg_aim_45` toggle. The cursor snaps to 45° steps and keeps its distance; the real cursor keeps moving underneath, so releasing returns to it.
  * Small sensitivity: `+kg_small_sens` while held, or the `kg_small_sens` toggle, set to `kg_small_sens_factor`% of the normal sensitivity.
* **Auto prediction margin** (`kg_auto_margin`, `kg_auto_margin_min/max/safety`). For every input, the server reports how early the input arrived. KING never goes below `kg_auto_margin_min` (10 ms, the stock DDNet value). It only adds margin when the network needs it: the robust worst case of the last ~10 s (98th percentile, or the worst sample after repeated late inputs) plus `kg_auto_margin_safety`, held for 20 s and then released slowly. The applied margin is ramped (+25 ms/s up, −2 ms/s down) because a sudden margin change shifts the prediction clock and moves your tee. Servers without the `SyncWeaponInput` capability force 10 ms, and the HUD shows this as "locked by server".
* **KING look**: a minimal dark menu with the KING wordmark, flat buttons and one gold accent (`kg_menu_style`, set to 0 for DDNet map themes).
* **Network HUD** (`kg_net_hud`): ping, jitter, current margin (and where auto margin is heading), input mode, and this session's late inputs, corrections, stutters and snapshot gaps.
* **Lag log** (`kg_lag_log`, `kg_lag_stats`): `king_lag.log` in `%APPDATA%\DDNet` gets one line per lag event with its likely cause: late inputs (how late, margin, ping), server corrections of your tee (size, and whether a late input, a stutter, a snapshot gap, a nearby player or your hook on a player happened within one round trip), frames longer than 25 ms, snapshot gaps over 150 ms, prediction clock resets, and a summary every minute. It starts a new file after 1 MB.
* **Music player** (`kg_music_hud` and `kg_music_*`): when something plays on the PC (Spotify, a browser, Yandex Music...), the race timer sits in a pill with the cover art on the left and a visualizer on the right. Settings: color mode (cover / KING gold / white), visualizer (off / bottom / center), text scale, number of bars, rounding (cube / soft), and synced lyrics under the timer (beta, from lrclib.net). The visualizer listens to the PC output through WASAPI loopback; on Windows 10 2004+ KING's own sounds are excluded. Binds: `kg_music_toggle`, `kg_music_next`, `kg_music_prev`.
* **Interface size** (`kg_ui_scale`, `kg_scoreboard_scale`): like Minecraft's GUI scale. Menus (all settings, server browser, main menu) can be made bigger or smaller (80-130% in KING → Display, 50-150% in the console), and the scoreboard (TAB) has its own size. Chat size is in the same section.
* **Player menu in TAB** (click a player): friend, mute chat, mute emoticons, add to the Team list (green) or War list (red) of TClient, and buttons: Invite (when you are in a team and they are not, sends `/invite`), Spectate, Profile (ddnet.org page), Whisper (opens chat with `/w name `), Copy Skin (to the tee you control), Vote Kick, Find Hours (total hours from ddstats.tw and points from ddnet.org, printed to chat), Clip Name (copies the name), /Swap.
* **Optimizer** (KING → Performance, `kg_optimizer` and `kg_opt_*`): no particles, no map quads, no high detail layers, and FPS fog (map tiles farther than `kg_opt_fps_fog_radius` tiles from the camera are not drawn and fade into fog). Only visuals change.
* **Process priority** (Windows, `kg_high_priority`, `kg_lower_discord`): KING with high priority, Discord below normal while KING runs (restored on exit).
* **Settings autosave**: settings are written about two seconds after a change while you are in the menus, so a crash does not lose them.
* **Aspect ratio** (`kg_aspect_ratio`, `kg_aspect_apply`, `kg_aspect_custom_w/h`): a stretched ratio like 4:3 on a 16:9 screen. Apply "Full" stretches everything in game (world, HUD, menus); "World only" keeps the HUD native. Custom takes width:height (for example 1440:1080).
* **KoG preset** (`kg_kog_preset`, also a button in Settings → KING → KING): KING input, antiping on, vsync off, uncapped update rate. It is applied once on first start.

* **One-click update** (Windows): the Update button in the main menu or the KING tab downloads the release zip. It shows progress, then closes KING. A small PowerShell script waits for the client to exit, unpacks the zip over the install folder with robocopy (config in `%APPDATA%\DDNet` is not touched) and starts KING again. If something fails, the reason is shown in the KING tab on the next start. CI tests this script on every build.
* **Update check** (`kg_update_check`, `kg_check_update`): on start, KING requests `https://api.github.com/repos/RoGGyt/KING/releases/latest` and compares the release tag with `KING_VERSION`. When a newer version exists, the main menu shows "KING x.y.z is out!" with a Download button that opens the `-win64.zip` link directly. The status and a "Check now" button are in Settings → KING → KING. Drafts and pre-releases are ignored.

## Releasing an update

1. Bump `KING_VERSION` in `src/game/version.h`, for example to `"1.0.2"`.
2. Push to `main`. The workflow builds the client and, if release `v1.0.2` does not exist yet, creates it with the `-win64.zip` attached.
3. Clients on older versions show "KING v1.0.2 is out!" in the main menu.

## Build

The Windows build runs on GitHub Actions (`.github/workflows/king-windows.yml`). Push to the repository, then download `KING-win64` from the run's artifacts. Pushing a tag `v*` also attaches the zip to a release.

To build locally, follow DDNet's normal build steps (see README.md). The executable is called `KING`.
