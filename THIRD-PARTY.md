# Upstream components

- [system-config-printer](https://github.com/OpenPrinting/system-config-printer),
  GPL-2.0-or-later. esyuprint imports the installed, unmodified `cupspk.Connection`
  PolicyKit adapter and `cupshelpers.missingExecutables`. It does not vendor or
  patch their source, GTK UI, driver tables, or troubleshooters.
- [pycups](https://github.com/OpenPrinting/pycups), GPL-2.0-or-later. Installed
  system Python bindings supply printer, job, PPD, and administration APIs.
- [CUPS](https://github.com/OpenPrinting/cups), Apache-2.0 with LLVM exception
  and upstream notices. The existing system server remains authoritative.
- Qt 6, under its applicable LGPL/GPL/commercial licenses, dynamically linked
  from system packages. See the distribution's Qt license files.
- [GTK 3](https://www.gtk.org/), LGPL-2.1-or-later, dynamically linked from the
  system package. The Print portal uses its non-UI printing SDK and serialized
  settings; it does not copy or replace GTK's printer backends.

The frontend and adapter were written for esyuprint. No esyuscan source was copied or
modified; its unmodified-SDK approach informed the integration boundary.
