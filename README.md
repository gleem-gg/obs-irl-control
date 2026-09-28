# OBS IRL Control

A native OBS Studio plugin that automatically switches scenes depending on the health of
your IRL stream, based on [IRL Control](https://github.com/frontpage-ev/irl-control).

Where the original IRL Control is a Node.js app that talks to OBS over obs-websocket, this
plugin runs *inside* OBS: no Node.js, no obs-websocket connection, no extra process. It
polls your [srtrelay](https://github.com/frontpage-ev/srtrelay) or
[Belabox Cloud](https://cloud.belabox.net) stats endpoint and switches between an online
and an offline scene when the stream drops or the RTT gets too high.

## Features

- Polls **srtrelay** (`/sockets`) or **Belabox Cloud** stats every second (configurable).
- Switches to the **offline scene** after the stream has been unhealthy for N seconds and
  back to the **online scene** as soon as it recovers (same state machine as IRL Control).
- Treats an RTT above `Max RTT` as unhealthy, exactly like the Node.js health check.
- Optionally writes `Stream: Connected` / `Stream: Disconnected (12s)` into an **info text
  source** and the raw stats into a **stats text source**.
- **Dock panel** (`Docks -> IRL Control`) with live status, RTT, stats, pause/resume and
  manual online/offline buttons, plus a settings dialog.
- **Hotkeys** for pause, resume, toggle, force online and force offline.
- **obs-websocket vendor requests** so external tools (chat bots, Stream Deck, the old
  IRL Control chat commands) can control the plugin.

## Installation

Source code and releases: <https://github.com/gleem-gg/obs-irl-control>

```bash
git clone https://github.com/gleem-gg/obs-irl-control.git
cd obs-irl-control
```

### OBS Studio from Flathub (Linux)

Prebuilt binary: download `obs-irl-control-<version>-linux-x86_64-flatpak.tar.gz` from the
[releases page](https://github.com/gleem-gg/obs-irl-control/releases) and extract it into
`~/.var/app/com.obsproject.Studio/config/obs-studio/plugins/`, then restart OBS.

To build it yourself without `flatpak-builder`, use the OBS Flatpak's own SDK
and install into the per-user plugin directory of the sandbox:

```bash
flatpak install flathub org.freedesktop.Sdk//25.08   # must match `flatpak info com.obsproject.Studio`
flatpak run --devel --filesystem=host --command=bash com.obsproject.Studio -c '
  cmake -S . -B build-flatpak-dev -G Ninja -DCMAKE_PREFIX_PATH=/app -DCMAKE_BUILD_TYPE=Release -DLINUX_PORTABLE=ON &&
  cmake --build build-flatpak-dev &&
  cmake --install build-flatpak-dev'
```

This installs to `~/.var/app/com.obsproject.Studio/config/obs-studio/plugins/obs-irl-control/`.
Delete that directory to uninstall.

Alternatively build a proper Flatpak extension with the manifest in `flatpak/`:

```bash
./flatpak/build.sh      # needs flatpak-builder
```

### Linux (native OBS package)

Requires the OBS development headers (`obs-studio-devel`, `libobs-dev` or similar), Qt 6
Widgets and libcurl development packages.

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
sudo cmake --install build            # system-wide (/usr/local)
# or, for the current user only:
cmake -S . -B build -DLINUX_PORTABLE=ON && cmake --build build && cmake --install build
```

### Windows / macOS

Build against an OBS Studio checkout or the OBS plugin template's prebuilt dependencies,
pointing `CMAKE_PREFIX_PATH` at the directory containing `libobs`, `obs-frontend-api`,
Qt 6 and libcurl:

```bash
cmake -S . -B build -DCMAKE_PREFIX_PATH="C:/obs-deps;C:/obs-studio/build" -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

The install layout follows OBS conventions (`obs-plugins/64bit` on Windows, an
`obs-irl-control.plugin` bundle on macOS).

## Configuration

Open the **IRL Control** dock (`Docks -> IRL Control`) and click **Settings...**.

| Setting | Description | IRL Control equivalent |
| --- | --- | --- |
| Type | `Belabox Cloud` or `SRT Relay (srtrelay)` | `stats_server.type` |
| URL | `https://stats.srt.belabox.net/XXXXX` or `http://127.0.0.1:34101` | `stats_server.url` |
| Publisher / stream id | Belabox publisher name (`live`) or srtrelay stream id prefix (`publish/test/`) | `stats_server.publisher` |
| Online scene | Scene to show while the stream is healthy | `obs.scenes.normal` |
| Offline scene | Scene to show while the stream is down | `obs.scenes.offline` |
| Info text source | Text source receiving `Stream: Connected` / `Stream: Disconnected (Ns)`; leave empty to disable | `obs.sources.info` |
| Stats text source | Text source receiving the raw stats; leave empty to disable | `obs.sources.stats` |
| Warn RTT | RTT above this is logged as a warning and shown orange in the dock | `health_check.warn_ms_rtt` |
| Max RTT | RTT above this counts as unhealthy | `health_check.max_ms_rtt` |
| Offline threshold | Seconds of unhealthy stream before switching to the offline scene | hard-coded `5` |
| Check interval | Polling interval in milliseconds | hard-coded `1000` |
| Start paused | Do not switch automatically until you press Resume | - |

The plugin does nothing until a stats server URL is configured. Settings are stored in
the OBS module config directory as `obs-irl-control/config.json`.

## Behaviour

Every interval the plugin fetches the stats of the configured publisher:

- **Healthy** (publisher connected and `RTT <= Max RTT`): the offline timer resets, the
  info/stats text sources are updated, and if the stream was previously marked offline
  the online scene is restored.
- **Unhealthy** (publisher missing, not connected, request failed, or `RTT > Max RTT`):
  the offline timer runs; once it reaches the threshold the offline scene is activated
  (once), until the stream is healthy again.

**Pause** stops polling and switching entirely. **Force online/offline** switch the scene
immediately without touching the health check state, matching `!irlc online` /
`!irlc offline` in IRL Control. If automatic switching is still running it will switch
back on the next state change, so pause first if you want to stay on a scene.

## Hotkeys

Configured under `Settings -> Hotkeys`:

- IRL Control: Pause automatic scene switching
- IRL Control: Resume automatic scene switching
- IRL Control: Toggle automatic scene switching
- IRL Control: Switch to online scene
- IRL Control: Switch to offline scene

## obs-websocket API

When obs-websocket (bundled with OBS 28+) is active, the plugin registers the vendor
`irl-control`. Use the `CallVendorRequest` request:

```json
{
  "requestType": "CallVendorRequest",
  "requestData": { "vendorName": "irl-control", "requestType": "GetStatus" }
}
```

| Request | Description |
| --- | --- |
| `GetStatus` | Returns `configured`, `running`, `paused`, `state` (`online`/`offline`/`unknown`), `marked_offline`, `ms_rtt`, `offline_duration`, `stats`, `last_error` |
| `GetConfig` | Returns the current configuration |
| `Pause` / `Resume` / `TogglePause` | Control automatic switching (returns the status) |
| `ForceOnline` / `ForceOffline` | Switch scenes manually (returns the status) |

Vendor events (`VendorEvent` with `vendorName: "irl-control"`):

| Event | Data |
| --- | --- |
| `StreamOffline` | `{ "offline_duration": 5 }` |
| `StreamReconnected` | `{}` |
| `PausedChanged` | `{ "paused": true }` |

This is the replacement for the HTTP API and the `!irlc` Twitch chat commands of the
Node.js app: a chat bot can map `!irlc pause` to the `Pause` vendor request, and so on.
Twitch chat integration itself is intentionally not part of the plugin.

## Testing with the mock stats server

`tools/mock-stats-server.py` simulates both srtrelay and Belabox Cloud with a controllable
stream, so you can test scene switching without a relay or a Belabox. It only needs Python 3.

```bash
python3 tools/mock-stats-server.py            # listens on http://127.0.0.1:18765
```

Then configure the plugin with one of:

| Type | URL | Publisher |
| --- | --- | --- |
| SRT Relay | `http://127.0.0.1:18765` | `publish/test/` |
| Belabox Cloud | `http://127.0.0.1:18765/belabox` | `live` |

Drive the simulation from the terminal the server runs in (`offline`, `online`, `toggle`,
`rtt 2500`, `flap 20`, `http 503`, `status`, `quit`) or from anywhere over HTTP:

```bash
curl http://127.0.0.1:18765/control/offline     # plugin switches to the offline scene after the threshold
curl http://127.0.0.1:18765/control/online      # plugin switches back to the online scene
curl http://127.0.0.1:18765/control/rtt/2500    # RTT above Max RTT counts as unhealthy
curl http://127.0.0.1:18765/control/rtt/120
curl http://127.0.0.1:18765/control/flap/20     # toggle automatically every 20 s (0 stops)
curl http://127.0.0.1:18765/control/http/503    # simulate a broken stats server (200 restores)
curl http://127.0.0.1:18765/control             # current simulated state
```

Options: `--port`, `--host`, `--stream-id`, `--publisher`, `--rtt`, `--offline` (start offline)
and `--verbose` (log every request). The Flatpak build of OBS shares the host network, so
`127.0.0.1` works from inside the sandbox.

## Development notes

- `src/health-check.*` is a direct port of `useHealthCheck.ts`; `src/stats-server.*`
  ports `useStatsServer.ts`; `src/irl-controller.*` replaces the event handlers in `app.ts`.
- JSON is parsed with libobs' `obs_data` (jansson), HTTP with libcurl, so there are no
  extra dependencies beyond what OBS already ships.
- `src/obs-websocket-api.h` is the GPLv2 API header from the obs-websocket project.

## License

Copyright (C) 2026 Anikeen UG (haftungsbeschränkt) & Co. KG

This program is free software; you can redistribute it and/or modify it under the terms of
the GNU General Public License as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version. See [LICENSE](LICENSE) for the full text.

OBS Studio, libobs and the obs-websocket API header are licensed under the GPLv2. The
original [IRL Control](https://github.com/frontpage-ev/irl-control) Node.js app this plugin
is based on is MIT-licensed; no code was copied from it.
