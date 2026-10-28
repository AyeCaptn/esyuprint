# esyuprint guide

Standalone printer management for Omarchy. Qt Quick supplies the interface;
unmodified **pycups**, **CUPS**, and **system-config-printer** helpers do the work.
Colours, light/dark mode, font, and window rounding follow the desktop live.
No webview, backend fork, root GUI, or cloud account. The optional Print portal
starts on demand when an application needs its dialog.

## Use

- Discover USB/network printers, add a queue, choose an installed driver or local
  PPD, and edit its connection, description, location, and server default.
- View status and reported ink/toner levels; pause/resume and accept/reject jobs.
- Configure reported IPP options and every enumerated PPD option.
- Hold, release, cancel, restart, move, or authenticate individual jobs.
- Manage printer classes, sharing, allowed/denied users, banner pages, and policies.
- Connect to local/remote CUPS servers and edit their advertised administration settings.
- Diagnose queue conditions and missing driver executables without changing anything.

Every mutation has a confirmation showing the action, queue/job, arguments, and
server. Test pages and restarted jobs can print immediately. CUPS administration
has no undo. Remote connections require TLS; credentials are held in memory,
sent over stdin rather than command arguments, and never saved by esyuprint.
Local administration uses the installed upstream PolicyKit adapter.

The package intentionally **keeps system-config-printer installed** because esyuprint
reuses its non-UI code. It does not launch its GTK dialogs. esyuprint does not install
vendor driver packages, change firewalls, export Samba shares, or reproduce the
legacy app's distro-specific repair wizard. SMB/LPD/socket queues can be entered
as device URIs with an appropriate installed driver. PPD file imports use the
desktop file chooser portal. Treat PPDs as trusted driver configuration.

Supplies and options depend on what the queue reports. The queue view includes
up to 200 jobs retained by the server. A disconnected server shows an error and
retains the last successful view. Requests time out; after a timed-out mutation,
refresh before retrying because the server may have completed it.
Background polling updates existing rows without dimming controls, moving the
header, or rebuilding unchanged editors.

## Print dialog

esyuprint also supplies `org.freedesktop.impl.portal.Print`: compact print controls,
page ranges, copies, duplex, colour, paper setup, margins, and print-to-file.
GTK's existing printing SDK handles transport; esyuprint supplies the interface.
Print-to-file refuses to overwrite an existing file. Optional preview uses
`evince-previewer` and may require reopening the document viewer.

The package installs the D-Bus activation entry and `esyuprint.portal`. To select it,
add this entry to the existing `[preferred]` section in your desktop's
`~/.config/xdg-desktop-portal/*-portals.conf`, preserving all other entries:

```ini
org.freedesktop.impl.portal.Print=esyuprint
```

Restart the user `xdg-desktop-portal.service` only when no portal dialogs or
screen-sharing sessions are active. To revert, remove that single entry and
restart the service again. File chooser, screen-sharing and secret backends
remain as configured.

On Omarchy/Hyprland, add this to your user Hyprland Lua configuration to keep
only the print dialog floating and centered; the manager remains tiled:

```lua
o.window({ class = "^esyuprint$", initial_title = "^esyuprint · Print$" },
  { float = true, center = true, size = { 760, 700 } })
```

Reload with `hyprctl reload` and check `hyprctl configerrors`. The rule matches
the English window title; adapt it if your translation changes the title.

Only applications that use the Print portal get esyuprint's dialog. Native GTK 3
applications can opt in with `GTK_USE_PORTAL=1` in their launcher environment;
browser-owned and other built-in print interfaces may bypass it. The portal is
not a universal override. It opens a separate window rather than importing the
calling application's window as its parent.

## Develop

System prerequisites: qmake6, make, a C++ compiler, Qt 6 base/declarative/Wayland,
and the runtime packages listed in `pkgbuild/PKGBUILD`. Tested with Qt 6.12.0,
CUPS 2.4.20, pycups 2.0.4, and system-config-printer 1.5.18 on Arch ARM64.

```sh
mise install
mise run setup       # Locked, project-local development tools via uv
mise run build
mise run run
mise run check       # Ruff, formatting, adapter tests
mise run check-all   # Also native build, Qt/QML interaction tests, upstream API check
mise run format
```

The runtime always invokes `/usr/bin/python3`, not the development virtualenv,
so it uses Arch's installed CUPS bindings. Development tools stay in mise/`.venv`.
`make package` builds a local Arch package; `make install` asks makepkg/pacman to
install it. Neither is run automatically by checks.

```sh
./build/esyuprint open --demo    # Simulated printers; refuses all mutations and CUPS queries
./build/esyuprint print --demo   # Preview the print dialog without submitting anything
ESYUPRINT_OFFSCREEN=1 QT_QUICK_BACKEND=software ./build/esyuprint open --demo --screenshot /tmp/esyuprint.png
```

| Key | Action |
| --- | --- |
| Ctrl+R / Space | Refresh |
| Ctrl+N | Add printer |
| ? | Show shortcuts |
| Q | Close, outside text entry |
| Esc | Cancel dialog |

Visible strings use Qt translation hooks and the Omarchy language setting;
partial Dutch/Danish catalogues are included, with English fallback. Translation
coverage is not yet complete. Locale formatting and RTL direction follow Omarchy.

## Agent/CLI access

```sh
echo '{"schema":1,"action":"overview"}' | ./build/esyuprint query --json
echo '{"schema":1,"action":"details","name":"Office"}' | ./build/esyuprint query --json
./build/esyuprint compatibility --json
```

The bridge protocol is documented by the dispatch table in `src/bridge.py`.
Read actions: `overview`, `details`, `devices`, `drivers`, `server`, `diagnose`,
`devices-authorized`, `compatibility`. Protected device discovery may ask for
authorization but does not change settings. Writes require **both** `esyuprint request --apply --json` and an
explicitly chosen write action. `query` refuses mutations even when its input
contains `"apply":true`. Results use `{schema:1,ok:true,data:…}` or
`{schema:1,ok:false,error:…,code:…}` with exit status 0/1; errors also go to stderr.
An optional `server` object accepts `host`, `port`, `user`, and `password` on stdin.

## Upstream updates

Runtime backends remain system packages, so their fixes do not require merging a
fork. `upstream.toml` records the integration baseline. `mise run upstream` compares
installed packages with upstream tags without installing or changing anything.

The repo includes the **esyuprint-update** agent skill in `.agents/skills/`; it directs
an AI agent to inspect release changes, adapt the small boundary, add regression
tests, and validate before proposing installation. `esyuprint skill install` can copy
it to the user's skills directory; it refuses to overwrite an existing skill.
There is no unattended merge, package upgrade, deployment, or publishing job.

## Validation status

Automated tests use fake connections and simulated printers. Read-only queries
have also verified the existing HP OfficeJet Pro 8020's queue, 15 available print
options, supply levels, and diagnostics. Actual administrative mutations,
remote-server authentication, and imported PPDs still need approved end-to-end
checks. The isolated portal suite covers real desktop-portal routing, a GTK
client, cancellation, and generated PDF submission to a private file; it submits
no physical print jobs. The original manager remains available via
`system-config-printer` as a fallback.

GPL-2.0-or-later. Upstream references and licensing: [THIRD-PARTY.md](../THIRD-PARTY.md).
