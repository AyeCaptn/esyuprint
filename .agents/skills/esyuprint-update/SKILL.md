---
name: esyuprint-update
description: Update esyuprint against upstream CUPS, pycups, and system-config-printer without forking their backends or changing live printers during validation.
---

# Update esyuprint

esyuprint is a Qt Quick frontend over the system's unmodified pycups and
system-config-printer `cupspk.Connection`. Its protocol is `src/bridge.py`.
Do not port the upstream GTK interface, vendor its printer backend, or replace
the installed helper files. Runtime dependencies update through Arch packages.

1. Read `README.md`, `upstream.toml`, project instructions, and checkout/upstream
   status. Ask how to handle existing work before editing. Never install packages,
   change CUPS settings, or publish without approval.
2. Run `mise run check-all` and `build/esyuprint compatibility --json`. Record the
   installed versions. Compatibility does not instantiate a CUPS connection.
3. Compare the recorded upstream refs with the requested releases using `gh` and
   official source. Inspect pycups signatures, cupspk's PolicyKit mappings,
   CUPS release notes, and PPD/IPP changes. Do not merge generated changes blindly.
   `mise run upstream` reports installed versions and the latest upstream tags;
   it does not install anything. Review release branches as well as tag order.
4. Change only the adapter/UI affected by an actual upstream change. Add a
   regression using a fake connection, and exercise the installed bindings with
   read-only queries. Update the recorded refs only after validation.
5. Run `mise run format`, `mise run check-all`, and offscreen QML interaction
   tests. Capture the real UI in `--demo` mode, including light/dark, long strings,
   empty/error states, and relevant dialogs. Demo mode must never contact CUPS.
6. Report changed upstream contracts, checks run, and unvalidated hardware paths.
   Ask before installing the package, changing launcher integration, exercising
   mutations on a real server, committing, pushing, or releasing.

CLI reads JSON from stdin. `esyuprint query --json` is read-only regardless of an
input `apply` value. Only `esyuprint request --apply --json` permits mutations.
Credentials go on stdin, not command arguments, logs, fixtures, or screenshots.
Never use real documents or printer identities in public screenshots.
