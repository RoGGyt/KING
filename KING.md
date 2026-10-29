# KING

KING is a DDNet client for KoG (King of Gores), based on [TClient](https://github.com/sjrc6/taterclient-ddnet) and DDNet.

## What KING adds

* **Input modes** (`kg_input_mode`):
  * `0` Custom: the TClient input settings are used as they are.
  * `1` Classic TClient: the old TClient fast input from before the Feb 2026 rewrite. `kg_classic_ticks` (1–5) works like the old `tc_fast_input`: your tee is shown that many whole ticks (20 ms each) ahead with your newest input.
  * `2` KING (default): classic input plus auto prediction margin. Fast input amount: `kg_fast_input_amount` (20 by default).
* **Auto prediction margin** (`kg_auto_margin`, `kg_auto_margin_min/max/safety`). For every input, the server reports how early the input arrived. KING sets the margin to the measured spread plus a safety value. The margin goes up quickly when inputs start arriving late and comes back down slowly (1 ms/s). On a stable wired connection this settles around 4–7 ms instead of the fixed 10 ms. Servers without the `SyncWeaponInput` capability force 10 ms, and the HUD shows this as "locked by server".
* **Network HUD** (`kg_net_hud`): ping, jitter, current margin, late inputs, input mode.
* **KoG preset** (`kg_kog_preset`, also a button in Settings → KING → KING): KING input, antiping on, vsync off, uncapped update rate. It is applied once on first start.

* **Update check** (`kg_update_check`, `kg_check_update`): on start, KING requests `https://api.github.com/repos/RoGGyt/KING/releases/latest` and compares the release tag with `KING_VERSION`. When a newer version exists, the main menu shows "KING x.y.z is out!" with a Download button that opens the `-win64.zip` link directly. The status and a "Check now" button are in Settings → KING → KING. Drafts and pre-releases are ignored.

## Releasing an update

1. Bump `KING_VERSION` in `src/game/version.h`, for example to `"1.0.1"`.
2. Commit, then push a tag with the same version: `git tag v1.0.1 && git push --tags`.
3. The workflow builds `KING-…-win64.zip` and attaches it to the GitHub release. Clients on an older version get the notice.

## Build

The Windows build runs on GitHub Actions (`.github/workflows/king-windows.yml`). Push to the repository, then download `KING-win64` from the run's artifacts. Pushing a tag `v*` also attaches the zip to a release.

To build locally, follow DDNet's normal build steps (see README.md). The executable is called `KING`.
