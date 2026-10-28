"""esyuprint's JSON boundary to unmodified pycups and system-config-printer helpers.

No shell parsing, privileged GUI, copied printer backend, or automatic repair.
Requests arrive on stdin; passwords never appear in process arguments.
"""

from __future__ import annotations

import importlib
import inspect
import json
import re
import subprocess
import sys
from pathlib import Path
from urllib.parse import unquote, urlsplit

SCHEMA = 1
READ_ACTIONS = {
    "overview",
    "details",
    "devices",
    "devices-authorized",
    "drivers",
    "server",
    "diagnose",
    "compatibility",
}
WRITE_ACTIONS = {
    "add",
    "delete",
    "default",
    "enable",
    "disable",
    "accept",
    "reject",
    "info",
    "location",
    "device",
    "shared",
    "error-policy",
    "operation-policy",
    "allow-users",
    "deny-users",
    "job-sheets",
    "option",
    "reset-option",
    "driver",
    "driver-file",
    "class-add",
    "class-remove",
    "class-delete",
    "job-cancel",
    "job-hold",
    "job-release",
    "job-restart",
    "job-move",
    "job-authenticate",
    "test-page",
    "server-save",
}
REQUIRED_METHODS = {
    "getPrinters",
    "getPrinterAttributes",
    "getClasses",
    "getJobs",
    "getDevices",
    "getPPDs",
    "getPPD",
    "addPrinter",
    "deletePrinter",
    "setDefault",
    "enablePrinter",
    "disablePrinter",
    "acceptJobs",
    "rejectJobs",
    "addPrinterOptionDefault",
    "deletePrinterOptionDefault",
    "addPrinterToClass",
    "deletePrinterFromClass",
    "deleteClass",
    "adminGetServerSettings",
    "adminSetServerSettings",
    "setPrinterUsersAllowed",
    "setPrinterUsersDenied",
    "cancelJob",
    "setJobHoldUntil",
    "restartJob",
    "moveJob",
    "authenticateJob",
    "printTestPage",
    "setPrinterInfo",
    "setPrinterLocation",
    "setPrinterDevice",
    "setPrinterShared",
    "setPrinterErrorPolicy",
    "setPrinterOpPolicy",
    "setPrinterJobSheets",
}


def text(value: object, field: str, *, empty: bool = False) -> str:
    if not isinstance(value, str) or "\x00" in value or (not empty and not value.strip()):
        raise ValueError(f"{field} must be {'a' if empty else 'a nonempty'} string without NULs.")
    return value


def queue_name(value: object) -> str:
    name = text(value, "Queue name")
    if not re.fullmatch(r"[^\s/#?\x00-\x1f\x7f]{1,127}", name) or len(name.encode("utf-8")) > 127:
        raise ValueError("Queue names must be 1–127 characters without spaces, /, #, or ?.")
    return name


def list_value(value: object) -> list:
    return list(value) if isinstance(value, (list, tuple)) else [value]


def json_value(value: object) -> object:
    if isinstance(value, dict):
        return {str(k): json_value(v) for k, v in value.items()}
    if isinstance(value, (tuple, list)):
        return [json_value(v) for v in value]
    if isinstance(value, bytes):
        return value.decode("utf-8", errors="replace")
    if value is None or isinstance(value, (str, bool, int, float)):
        return value
    return str(value)


def printer_row(name: str, attrs: dict, default: str | None, is_class: bool = False) -> dict:
    return {
        "name": name,
        "label": attrs.get("printer-info") or name,
        "model": attrs.get("printer-make-and-model", ""),
        "location": attrs.get("printer-location", ""),
        "state": int(attrs.get("printer-state", 3)),
        "message": attrs.get("printer-state-message", ""),
        "reasons": list_value(attrs.get("printer-state-reasons", [])),
        "default": name == default,
        "class": is_class,
        "shared": bool(attrs.get("printer-is-shared", False)),
        "uri": attrs.get("device-uri", ""),
        "queueUri": attrs.get("printer-uri-supported", ""),
    }


def supplies(attrs: dict) -> list[dict]:
    names = list_value(attrs.get("marker-names", []))
    levels = list_value(attrs.get("marker-levels", []))
    colors = list_value(attrs.get("marker-colors", []))
    return [
        {
            "name": name,
            "level": levels[i] if i < len(levels) else -1,
            "color": colors[i] if i < len(colors) else "",
        }
        for i, name in enumerate(names)
    ]


def ipp_options(attrs: dict) -> list[dict]:
    keys = {
        "media": "Paper size",
        "media-source": "Paper tray",
        "media-type": "Paper type",
        "sides": "Two-sided printing",
        "print-color-mode": "Color mode",
        "print-quality": "Print quality",
        "output-bin": "Output tray",
        "orientation-requested": "Orientation",
        "number-up": "Pages per sheet",
    }
    options = []
    for key, label in keys.items():
        values = attrs.get(key + "-supported", [])
        if not isinstance(values, (list, tuple)) or not values:
            continue
        options.append(
            {
                "key": key,
                "label": label,
                "group": "IPP",
                "value": str(attrs.get(key + "-default", "")),
                "choices": [{"value": str(v), "label": str(v)} for v in values],
            }
        )
    return options


def ppd_options(ppd: object) -> list[dict]:
    result = []

    def group_options(group: object) -> None:
        for option in group.options:
            result.append(
                {
                    "key": option.keyword,
                    "label": option.text,
                    "group": group.text,
                    "value": next(
                        (c["choice"] for c in option.choices if c["marked"]), option.defchoice
                    ),
                    "choices": [{"value": c["choice"], "label": c["text"]} for c in option.choices],
                }
            )
        for subgroup in group.subgroups:
            group_options(subgroup)

    for group in ppd.optionGroups:
        group_options(group)
    return result


class Printing:
    def __init__(self, connection: object, cups: object, admin_factory=None):
        self.connection = connection
        self.cups = cups
        self.admin_factory = admin_factory or (lambda: connection)

    def overview(self) -> dict:
        default = self.connection.getDefault()
        printers = self.connection.getPrinters()
        classes = self.connection.getClasses()
        rows = [printer_row(n, a, default) for n, a in sorted(printers.items())]
        for name, members in sorted(classes.items()):
            attrs = self.connection.getPrinterAttributes(name)
            row = printer_row(name, attrs, default, True)
            row["members"] = members
            rows.append(row)
        jobs = [
            {"id": int(job_id), **json_value(attrs)}
            for job_id, attrs in self.connection.getJobs(which_jobs="all", limit=200).items()
        ]
        return {"printers": rows, "jobs": sorted(jobs, key=lambda j: j["id"], reverse=True)}

    def details(self, name: str) -> dict:
        attrs = self.connection.getPrinterAttributes(name)
        options = ipp_options(attrs)
        warnings = []
        if not (int(attrs.get("printer-type", 0)) & self.cups.CUPS_PRINTER_CLASS):
            try:
                path = self.connection.getPPD(name)
            except self.cups.IPPError as error:
                warnings.append(
                    f"PPD options unavailable: {error}. IPP options are shown where reported."
                )
            else:
                try:
                    options += ppd_options(self.cups.PPD(path))
                finally:
                    Path(path).unlink(missing_ok=True)
        return {
            "name": name,
            "attributes": json_value(attrs),
            "options": options,
            "supplies": supplies(attrs),
            "warnings": warnings,
        }

    def diagnose(self, name: str) -> dict:
        details = self.details(name)
        attrs = details["attributes"]
        checks = [
            {"label": "CUPS connection", "ok": True, "detail": "Server responded to IPP."},
            {
                "label": "Queue enabled",
                "ok": attrs.get("printer-state") != 5,
                "detail": attrs.get("printer-state-message") or "No queue message.",
            },
            {
                "label": "Accepting jobs",
                "ok": bool(attrs.get("printer-is-accepting-jobs")),
                "detail": "Accept jobs allows new submissions; it does not resume a paused queue.",
            },
        ]
        for reason in list_value(attrs.get("printer-state-reasons", [])):
            if reason != "none":
                checks.append({"label": "Printer condition", "ok": False, "detail": reason})
        try:
            path = self.connection.getPPD(name)
        except self.cups.IPPError as error:
            checks.append({"label": "Driver check", "ok": False, "detail": str(error)})
        else:
            try:
                helpers = importlib.import_module("cupshelpers")
                missing = helpers.missingExecutables(self.cups.PPD(path))
                checks.append(
                    {
                        "label": "Driver executables",
                        "ok": not missing,
                        "detail": ", ".join(missing)
                        if missing
                        else "No missing driver executables.",
                    }
                )
            finally:
                Path(path).unlink(missing_ok=True)
        return {
            "checks": checks,
            "attributes": attrs,
            "warnings": details["warnings"],
            "note": "Read-only checks. No jobs, settings, services, or firewall rules changed.",
        }

    def read(self, action: str, request: dict) -> object:
        if action == "overview":
            return self.overview()
        if action in {"details", "diagnose"}:
            name = queue_name(request.get("name"))
            return self.details(name) if action == "details" else self.diagnose(name)
        if action in {"devices", "devices-authorized"}:
            devices, warnings = [], []
            try:
                source = self.admin_factory() if action == "devices-authorized" else self.connection
                devices = [
                    {"uri": uri, **json_value(attrs)}
                    for uri, attrs in source.getDevices(timeout=10).items()
                ]
            except self.cups.IPPError as error:
                warnings.append(f"CUPS device discovery: {error}")
            # driverless returns usable IPP/IPPS URIs, unlike opaque DNS-SD backend URIs.
            try:
                result = subprocess.run(
                    ["/usr/bin/driverless"], capture_output=True, text=True, timeout=12, check=False
                )
            except (OSError, subprocess.TimeoutExpired) as error:
                warnings.append(f"Driverless discovery: {error}")
            else:
                if result.returncode:
                    warnings.append("Driverless discovery: " + result.stderr.strip())
                for uri in result.stdout.splitlines():
                    if urlsplit(uri).scheme in {"ipp", "ipps"}:
                        devices.insert(
                            0,
                            {
                                "uri": uri,
                                "device-info": unquote(urlsplit(uri).netloc),
                                "device-class": "network",
                            },
                        )
            return {"devices": devices, "warnings": warnings}
        if action == "drivers":
            return [
                {"id": name, "label": attrs.get("ppd-make-and-model", name)}
                for name, attrs in sorted(self.connection.getPPDs().items())
            ]
        if action == "server":
            return json_value(self.connection.adminGetServerSettings())
        raise ValueError(f"Unknown read action: {action}")

    def write(self, action: str, request: dict) -> object:
        # Validate the whole command before creating the privileged connection.
        args = request.get("args", {})
        if not isinstance(args, dict):
            raise ValueError("args must be an object.")
        name = queue_name(request.get("name")) if not action.startswith(("job-", "server-")) else ""
        value = args.get("value")
        method, positional, keywords = "", [], {}
        simple = {
            "delete": "deletePrinter",
            "default": "setDefault",
            "enable": "enablePrinter",
            "disable": "disablePrinter",
            "accept": "acceptJobs",
            "reject": "rejectJobs",
            "class-delete": "deleteClass",
            "test-page": "printTestPage",
        }
        setters = {
            "info": "setPrinterInfo",
            "location": "setPrinterLocation",
            "device": "setPrinterDevice",
            "error-policy": "setPrinterErrorPolicy",
            "operation-policy": "setPrinterOpPolicy",
        }
        if action in simple:
            method, positional = simple[action], [name]
        elif action in setters:
            if action == "device":
                uri = urlsplit(text(value, "Device URI"))
                if not uri.scheme or uri.password is not None:
                    raise ValueError("Device URI needs a scheme and must not embed a password.")
            method, positional = (
                setters[action],
                [name, text(value, "Value", empty=action in {"info", "location"})],
            )
        elif action == "shared":
            if not isinstance(value, bool):
                raise ValueError("Shared must be a boolean.")
            method, positional = "setPrinterShared", [name, value]
        elif action in {"allow-users", "deny-users"}:
            if not isinstance(value, list) or not value:
                raise ValueError(
                    "Supply a nonempty list of users (use ['all'] for unrestricted access)."
                )
            users = [text(user, "User") for user in value]
            method = (
                "setPrinterUsersAllowed" if action == "allow-users" else "setPrinterUsersDenied"
            )
            positional = [name, users]
        elif action == "job-sheets":
            if not isinstance(value, list) or len(value) != 2:
                raise ValueError("Supply start and end banner names.")
            method, positional = "setPrinterJobSheets", [name, *[text(v, "Banner") for v in value]]
        elif action in {"add", "driver", "driver-file"}:
            if action == "driver-file" or args.get("filename"):
                filename = Path(text(args.get("filename"), "PPD filename"))
                if not filename.is_file():
                    raise ValueError("The selected PPD file does not exist.")
                self.cups.PPD(str(filename))
                keywords = {"filename": str(filename)}
            else:
                driver = text(args.get("driver"), "Driver")
                keywords = {"ppdname": driver}
            if action == "add":
                if name in self.connection.getPrinters() or name in self.connection.getClasses():
                    raise ValueError("This queue already exists. Choose a different name.")
                uri = text(args.get("uri"), "Device URI")
                if not urlsplit(uri).scheme:
                    raise ValueError(
                        "Device URI must include a scheme, for example ipp:// or usb://."
                    )
                if urlsplit(uri).password is not None:
                    raise ValueError("Do not embed passwords in device URIs.")
                if args.get("driver") == "everywhere" and urlsplit(uri).scheme not in {
                    "ipp",
                    "ipps",
                }:
                    raise ValueError("IPP Everywhere requires an ipp:// or ipps:// device URI.")
                keywords.update(
                    device=uri,
                    info=text(args.get("info", name), "Description", empty=True),
                    location=text(args.get("location", ""), "Location", empty=True),
                )
            method, positional = "addPrinter", [name]
        elif action in {"option", "reset-option"}:
            key = text(args.get("key"), "Option")
            if not re.fullmatch(r"[A-Za-z0-9_.-]+", key):
                raise ValueError("Invalid option keyword.")
            if action == "option":
                option_value = text(value, "Option value")
                choices = {
                    o["key"]: {c["value"] for c in o["choices"]}
                    for o in self.details(name)["options"]
                }
                if key not in choices or option_value not in choices[key]:
                    raise ValueError(
                        "Option/value is not reported by this printer. Refresh its capabilities."
                    )
                method, positional = "addPrinterOptionDefault", [name, key, option_value]
            else:
                method, positional = "deletePrinterOptionDefault", [name, key]
        elif action in {"class-add", "class-remove"}:
            member = queue_name(args.get("member"))
            method = "addPrinterToClass" if action == "class-add" else "deletePrinterFromClass"
            positional = [member, name]
        elif action.startswith("job-"):
            job_id = request.get("job")
            if type(job_id) is not int or job_id < 1:
                raise ValueError("Job ID must be a positive integer.")
            methods = {
                "job-cancel": "cancelJob",
                "job-hold": "setJobHoldUntil",
                "job-release": "setJobHoldUntil",
                "job-restart": "restartJob",
                "job-move": "moveJob",
                "job-authenticate": "authenticateJob",
            }
            method, positional = methods[action], [job_id]
            if action in {"job-hold", "job-release"}:
                positional.append("indefinite" if action == "job-hold" else "no-hold")
            if action == "job-move":
                target = queue_name(args.get("target"))
                attrs = self.connection.getPrinterAttributes(target)
                positional, keywords = (
                    [],
                    {"job_id": job_id, "job_printer_uri": attrs["printer-uri-supported"]},
                )
            if action == "job-authenticate":
                if not isinstance(value, list) or not value:
                    raise ValueError(
                        "Supply authentication values in the printer's auth-info-required order."
                    )
                positional.append([text(v, "Credential", empty=True) for v in value])
        elif action == "server-save":
            if not isinstance(value, dict) or not value:
                raise ValueError("Server settings must be a nonempty object.")
            current = self.connection.adminGetServerSettings()
            if any(k not in current or not isinstance(v, str) for k, v in value.items()):
                raise ValueError(
                    "Only existing server setting keys with string values may be changed."
                )
            method, positional = "adminSetServerSettings", [value]
        else:
            raise ValueError(f"Unknown write action: {action}")
        connection = self.admin_factory()
        result = getattr(connection, method)(*positional, **keywords)
        return {"action": action, "result": json_value(result)}


def uses_system_helper(host: str, port: int) -> bool:
    # cups-pk-helper operates on the system server, not an arbitrary supplied socket/port.
    return host in {"/run/cups/cups.sock", "/var/run/cups/cups.sock"} or (
        host in {"localhost", "127.0.0.1", "::1"} and port == 631
    )


def make_printing(server: dict) -> Printing:
    import cups

    host = text(server.get("host", cups.getServer()), "Server")
    port = server.get("port", cups.getPort())
    if type(port) is not int or not 1 <= port <= 65535:
        raise ValueError("Port must be an integer between 1 and 65535.")
    local = host.startswith("/") or host in {"localhost", "127.0.0.1", "::1"}
    # Remote credentials must never be sent over plaintext IPP.
    encryption = cups.HTTP_ENCRYPT_IF_REQUESTED if local else cups.HTTP_ENCRYPT_ALWAYS
    if server.get("user"):
        cups.setUser(text(server["user"], "User"))
    password = text(server.get("password", ""), "Password", empty=True)
    cups.setPasswordCB2(lambda *_: password or None)
    connection = cups.Connection(host=host, port=port, encryption=encryption)

    def admin():
        if not uses_system_helper(host, port):
            return connection
        # Reuse upstream's unmodified PolicyKit adapter, not its GTK GUI.
        sys.path.insert(0, "/usr/share/system-config-printer")
        from cupspk import Connection

        return Connection(host, port, encryption)

    return Printing(connection, cups, admin)


def compatibility() -> dict:
    import cups
    import cupshelpers

    sys.path.insert(0, "/usr/share/system-config-printer")
    from cupspk import Connection

    missing = sorted(REQUIRED_METHODS - set(dir(cups.Connection)))
    if list(inspect.signature(Connection).parameters)[:3] != ["host", "port", "encryption"]:
        missing.append("cupspk.Connection(host, port, encryption)")
    if not callable(getattr(cupshelpers, "missingExecutables", None)):
        missing.append("cupshelpers.missingExecutables")
    for constant in [
        "HTTP_ENCRYPT_IF_REQUESTED",
        "HTTP_ENCRYPT_ALWAYS",
        "CUPS_PRINTER_CLASS",
        "IPP_NOT_AUTHORIZED",
        "IPP_FORBIDDEN",
        "IPP_AUTHENTICATION_CANCELED",
    ]:
        if not hasattr(cups, constant):
            missing.append(constant)
    # cupspk forwards methods absent on its wrapper to the actual pycups connection.
    helper = Path("/usr/share/system-config-printer/cupspk.py")
    versions = subprocess.run(
        [
            "/usr/bin/pacman",
            "-Q",
            "cups",
            "libcups",
            "python-pycups",
            "system-config-printer",
            "cups-pk-helper",
        ],
        capture_output=True,
        text=True,
        check=False,
    )
    if missing:
        raise RuntimeError("Upstream API mismatch: " + ", ".join(missing))
    return {
        "schema": SCHEMA,
        "methods": sorted(REQUIRED_METHODS),
        "helper": str(helper),
        "helperClass": Connection.__name__,
        "packages": versions.stdout.splitlines(),
        "packageWarnings": versions.stderr.strip(),
    }


def dispatch(request: dict, printing: Printing | None = None) -> object:
    if (
        not isinstance(request, dict)
        or type(request.get("schema")) is not int
        or request.get("schema") != SCHEMA
    ):
        raise ValueError("Expected a JSON object with schema: 1.")
    action = request.get("action")
    if action not in READ_ACTIONS | WRITE_ACTIONS:
        raise ValueError(f"Unknown action: {action}")
    if action in WRITE_ACTIONS and request.get("apply") is not True:
        raise ValueError("Mutation refused. Explicit confirmation (apply: true) is required.")
    if action == "compatibility":
        return compatibility()
    printing = printing or make_printing(request.get("server", {}))
    if action in READ_ACTIONS:
        return printing.read(action, request)
    return printing.write(action, request)


def main() -> int:
    try:
        result = dispatch(json.load(sys.stdin))
        print(json.dumps({"schema": SCHEMA, "ok": True, "data": result}, ensure_ascii=False))
        return 0
    except Exception as error:
        # The process boundary must return structured failures, including missing dependencies.
        code = "error"
        try:
            import cups

            if isinstance(error, cups.IPPError) and error.args[0] in {
                cups.IPP_NOT_AUTHORIZED,
                cups.IPP_FORBIDDEN,
                cups.IPP_AUTHENTICATION_CANCELED,
            }:
                code = "authentication"
        except ImportError:
            pass
        message = str(error)
        print(json.dumps({"schema": SCHEMA, "ok": False, "error": message, "code": code}))
        print(message, file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
