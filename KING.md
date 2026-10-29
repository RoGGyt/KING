# KING

The feature set takes ideas from BestClient. Everything here is implemented independently on top of TClient; no BestClient code is used.

KING is a DDNet client for KoG (King of Gores), based on [TClient](https://github.com/sjrc6/taterclient-ddnet) and DDNet.

## What KING adds

* **Input modes** (`kg_input_mode`):
  * `0` Custom: the TClient input settings are used as they are.
  * `1` Classic TClient: the old TClient fast input from before the Feb 2026 rewrite. `kg_classic_ticks` (1–5) works like the old `tc_fast_input`: your tee is shown that many whole ticks (20 ms each) ahead with your newest input.
  * `2` KING (default): classic input plus auto prediction margin. Fast input amount: `kg_fast_input_amount` (20 by default).
  * `3` Delta: a fixed offset in hundredths of a tick (`kg_delta_amount` 0–500, where 100 = 1 tick). It can sit between whole ticks; `kg_delta_others` shifts other tees too.
  * `4` Ping Comp: fast input is `kg_ping_comp`% of your measured ping, capped at 100 ms. It moves in 2 ms steps and uses auto margin.
  * `5` Smooth: KING mode, but when the server corrects your own tee, the correction fades out over `kg_smooth_ms` instead of snapping. This is visual only: inputs and physics are not delayed. Teleports (more than 2 tiles) still snap.
* **Input helpers**:
  * Snap Tap (`kg_snap_tap`, `kg_snap_tap_delay`): when left and right are both held, the key pressed last wins instead of standing still.
  * 45° aim: `+kg_45` while held, or the `kg_aim_45` toggle. The cursor snaps to 45° steps and keeps its distance; the real cursor keeps moving underneath, so releasing returns to it.
  * Small sensitivity: `+kg_small_sens` while held, or the `kg_small_sens` toggle, set to `kg_small_sens_factor`% of the normal sensitivity.
* **Auto prediction margin** (`kg_auto_margin`, `kg_auto_margin_min/max/safety`). For every input, the server reports how early the input arrived. KING sets the margin to the measured spread plus a safety value. The margin goes up quickly when inputs start arriving late and comes back down slowly (1 ms/s). On a stable wired connection this settles around 4–7 ms instead of the fixed 10 ms. Servers without the `SyncWeaponInput` capability force 10 ms, and the HUD shows this as "locked by server".
* **Network HUD** (`kg_net_hud`): ping, jitter, current margin, late inputs, input mode.
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
