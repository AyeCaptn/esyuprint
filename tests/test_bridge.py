from __future__ import annotations

import sys
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import Mock

import pytest

from bridge import (
    Printing,
    dispatch,
    ipp_options,
    make_printing,
    printer_row,
    queue_name,
    supplies,
    uses_system_helper,
)


@pytest.fixture
def connection() -> Mock:
    c = Mock()
    c.getDefault.return_value = "Office"
    c.getPrinters.return_value = {"Office": {"printer-state": 3}}
    c.getClasses.return_value = {}
    c.getJobs.return_value = {9: {"job-state": 4}}
    c.getPrinterAttributes.return_value = {}
    return c


@pytest.fixture
def printing(connection: Mock) -> Printing:
    return Printing(connection, SimpleNamespace(CUPS_PRINTER_CLASS=1, IPPError=RuntimeError))


@pytest.mark.parametrize("name", ["Office", "Office_2", "HP-8020", "Étage"])
def test_valid_queue_name_is_preserved(name: str) -> None:
    assert queue_name(name) == name


@pytest.mark.parametrize("name", ["", "a b", "x/y", "x#y", "x?y", "\n", "x" * 128])
def test_invalid_queue_name_is_rejected(name: str) -> None:
    with pytest.raises(ValueError, match="Queue"):
        queue_name(name)


def test_overview_keeps_ids_numeric(printing: Printing) -> None:
    assert printing.overview()["jobs"] == [{"id": 9, "job-state": 4}]


def test_default_printer_is_marked() -> None:
    assert printer_row("Office", {}, "Office")["default"] is True


def test_empty_supplies_are_not_invented() -> None:
    assert supplies({}) == []


def test_supply_arrays_can_have_missing_entries() -> None:
    assert supplies({"marker-names": ["Black", "Cyan"], "marker-levels": [35]}) == [
        {"name": "Black", "level": 35, "color": ""},
        {"name": "Cyan", "level": -1, "color": ""},
    ]


def test_integer_ipp_values_are_serialized_as_option_strings() -> None:
    assert ipp_options({"print-quality-supported": [3, 4], "print-quality-default": 4}) == [
        {
            "key": "print-quality",
            "label": "Print quality",
            "group": "IPP",
            "value": "4",
            "choices": [{"value": "3", "label": "3"}, {"value": "4", "label": "4"}],
        }
    ]


def test_unknown_capabilities_are_not_offered() -> None:
    assert ipp_options({"sides-supported": "unknown"}) == []


def test_missing_confirmation_refuses_mutation(printing: Printing) -> None:
    with pytest.raises(ValueError, match="Mutation refused"):
        dispatch({"schema": 1, "action": "delete", "name": "Office"}, printing)


def test_confirmation_must_be_true_not_truthy(printing: Printing) -> None:
    with pytest.raises(ValueError, match="Mutation refused"):
        dispatch({"schema": 1, "action": "delete", "name": "Office", "apply": "true"}, printing)


def test_unrecognized_action_is_refused(printing: Printing) -> None:
    with pytest.raises(ValueError, match="Unknown action"):
        dispatch({"schema": 1, "action": "eval", "apply": True}, printing)


def test_missing_schema_is_refused(printing: Printing) -> None:
    with pytest.raises(ValueError, match="schema"):
        dispatch({"action": "overview"}, printing)


def test_read_overview_never_creates_admin_connection(connection: Mock) -> None:
    admin = Mock(side_effect=AssertionError("Must not request privileges"))
    p = Printing(connection, SimpleNamespace(), admin)
    assert dispatch({"schema": 1, "action": "overview"}, p)["jobs"] == [{"id": 9, "job-state": 4}]


def test_delete_targets_only_confirmed_queue(printing: Printing, connection: Mock) -> None:
    dispatch({"schema": 1, "action": "delete", "name": "Office", "apply": True}, printing)
    connection.deletePrinter.assert_called_once_with("Office")


def test_class_member_argument_order_matches_upstream(printing: Printing, connection: Mock) -> None:
    dispatch(
        {
            "schema": 1,
            "action": "class-add",
            "name": "Shared",
            "args": {"member": "Office"},
            "apply": True,
        },
        printing,
    )
    connection.addPrinterToClass.assert_called_once_with("Office", "Shared")


def test_hold_uses_ipp_hold_keyword(printing: Printing, connection: Mock) -> None:
    dispatch({"schema": 1, "action": "job-hold", "job": 9, "apply": True}, printing)
    connection.setJobHoldUntil.assert_called_once_with(9, "indefinite")


def test_release_uses_ipp_release_keyword(printing: Printing, connection: Mock) -> None:
    dispatch({"schema": 1, "action": "job-release", "job": 9, "apply": True}, printing)
    connection.setJobHoldUntil.assert_called_once_with(9, "no-hold")


@pytest.mark.parametrize("job", [0, -1, True, "9"])
def test_invalid_job_id_is_refused(printing: Printing, job: object) -> None:
    with pytest.raises(ValueError, match="positive integer"):
        dispatch({"schema": 1, "action": "job-cancel", "job": job, "apply": True}, printing)


def test_driverless_add_uses_upstream_addprinter_api(printing: Printing, connection: Mock) -> None:
    dispatch(
        {
            "schema": 1,
            "action": "add",
            "name": "New",
            "apply": True,
            "args": {"driver": "everywhere", "uri": "ipp://printer.example/ipp/print"},
        },
        printing,
    )
    connection.addPrinter.assert_called_once_with(
        "New",
        ppdname="everywhere",
        device="ipp://printer.example/ipp/print",
        info="New",
        location="",
    )


def test_boolean_sharing_is_not_coerced_from_strings(printing: Printing) -> None:
    with pytest.raises(ValueError, match="boolean"):
        dispatch(
            {
                "schema": 1,
                "action": "shared",
                "name": "Office",
                "apply": True,
                "args": {"value": "false"},
            },
            printing,
        )


def test_server_only_accepts_reported_setting_keys(printing: Printing, connection: Mock) -> None:
    connection.adminGetServerSettings.return_value = {"_debug_logging": "0"}
    with pytest.raises(ValueError, match="existing server setting"):
        dispatch(
            {
                "schema": 1,
                "action": "server-save",
                "apply": True,
                "args": {"value": {"unknown": "1"}},
            },
            printing,
        )


def test_server_changes_preserve_explicit_string_value(
    printing: Printing, connection: Mock
) -> None:
    connection.adminGetServerSettings.return_value = {"_debug_logging": "0"}
    dispatch(
        {
            "schema": 1,
            "action": "server-save",
            "apply": True,
            "args": {"value": {"_debug_logging": "1"}},
        },
        printing,
    )
    connection.adminSetServerSettings.assert_called_once_with({"_debug_logging": "1"})


def test_arbitrary_options_are_refused(printing: Printing, connection: Mock) -> None:
    connection.getPrinterAttributes.return_value = {"printer-type": 1}
    with pytest.raises(ValueError, match="not reported"):
        dispatch(
            {
                "schema": 1,
                "action": "option",
                "name": "Office",
                "apply": True,
                "args": {"key": "arbitrary", "value": "x"},
            },
            printing,
        )


def test_supported_option_is_forwarded(printing: Printing, connection: Mock) -> None:
    connection.getPrinterAttributes.return_value = {
        "printer-type": 1,
        "sides-supported": ["one-sided", "two-sided-long-edge"],
    }
    dispatch(
        {
            "schema": 1,
            "action": "option",
            "name": "Office",
            "apply": True,
            "args": {"key": "sides", "value": "two-sided-long-edge"},
        },
        printing,
    )
    connection.addPrinterOptionDefault.assert_called_once_with(
        "Office", "sides", "two-sided-long-edge"
    )


def test_backend_errors_are_not_swallowed(printing: Printing, connection: Mock) -> None:
    connection.deletePrinter.side_effect = RuntimeError("Access denied")
    with pytest.raises(RuntimeError, match="Access denied"):
        dispatch({"schema": 1, "action": "delete", "name": "Office", "apply": True}, printing)


def test_add_refuses_to_overwrite_existing_queue(printing: Printing) -> None:
    with pytest.raises(ValueError, match="already exists"):
        dispatch(
            {
                "schema": 1,
                "action": "add",
                "name": "Office",
                "apply": True,
                "args": {"driver": "everywhere", "uri": "ipp://printer.example/ipp/print"},
            },
            printing,
        )


def test_driverless_add_requires_ipp(printing: Printing) -> None:
    with pytest.raises(ValueError, match="IPP Everywhere requires"):
        dispatch(
            {
                "schema": 1,
                "action": "add",
                "name": "New",
                "apply": True,
                "args": {"driver": "everywhere", "uri": "usb://device"},
            },
            printing,
        )


def test_add_rejects_password_in_device_uri(printing: Printing) -> None:
    with pytest.raises(ValueError, match="embed passwords"):
        dispatch(
            {
                "schema": 1,
                "action": "add",
                "name": "New",
                "apply": True,
                "args": {"driver": "vendor", "uri": "smb://user:secret@printer/queue"},
            },
            printing,
        )


def test_missing_ppd_file_is_rejected_before_mutation(printing: Printing, tmp_path: Path) -> None:
    with pytest.raises(ValueError, match="does not exist"):
        dispatch(
            {
                "schema": 1,
                "action": "driver-file",
                "name": "Office",
                "apply": True,
                "args": {"filename": str(tmp_path / "absent.ppd")},
            },
            printing,
        )


@pytest.fixture
def cups_module(monkeypatch: pytest.MonkeyPatch) -> Mock:
    module = Mock()
    module.HTTP_ENCRYPT_IF_REQUESTED = 0
    module.HTTP_ENCRYPT_ALWAYS = 2
    module.getServer.return_value = "/run/cups/cups.sock"
    module.getPort.return_value = 631
    monkeypatch.setitem(sys.modules, "cups", module)
    return module


def test_remote_connection_requires_tls(cups_module: Mock) -> None:
    make_printing({"host": "print.example", "port": 8631})
    cups_module.Connection.assert_called_once_with(host="print.example", port=8631, encryption=2)


def test_local_socket_preserves_local_transport(cups_module: Mock) -> None:
    make_printing({})
    cups_module.Connection.assert_called_once_with(
        host="/run/cups/cups.sock", port=631, encryption=0
    )


def test_missing_password_cancels_instead_of_prompting_in_terminal(cups_module: Mock) -> None:
    make_printing({})
    callback = cups_module.setPasswordCB2.call_args.args[0]
    assert callback("password prompt") is None


@pytest.mark.parametrize("port", [0, 65536, "631", True])
def test_invalid_server_port_is_refused(cups_module: Mock, port: object) -> None:
    with pytest.raises(ValueError, match="Port"):
        make_printing({"port": port})


@pytest.mark.parametrize(
    "host,port", [("/tmp/test-cups.sock", 631), ("localhost", 8631), ("print.example", 631)]
)
def test_non_system_servers_never_use_system_polkit_helper(host: str, port: int) -> None:
    assert uses_system_helper(host, port) is False


@pytest.mark.parametrize("host", ["localhost", "127.0.0.1", "/run/cups/cups.sock"])
def test_system_server_can_use_existing_polkit_helper(host: str) -> None:
    assert uses_system_helper(host, 631) is True


def test_configured_cups_port_is_preserved(cups_module: Mock) -> None:
    cups_module.getServer.return_value = "localhost"
    cups_module.getPort.return_value = 8631
    make_printing({})
    cups_module.Connection.assert_called_once_with(host="localhost", port=8631, encryption=0)


def test_boolean_schema_is_not_a_version(printing: Printing) -> None:
    with pytest.raises(ValueError, match="schema"):
        dispatch({"schema": True, "action": "overview"}, printing)


def test_network_discovery_keeps_results_and_reports_cups_permission_failure(
    connection: Mock, monkeypatch: pytest.MonkeyPatch
) -> None:
    connection.getDevices.side_effect = RuntimeError("Forbidden")
    admin = Mock(side_effect=AssertionError("Plain discovery must not request authorization"))
    printing = Printing(connection, SimpleNamespace(IPPError=RuntimeError), admin)
    monkeypatch.setattr(
        "bridge.subprocess.run",
        lambda *args, **kwargs: SimpleNamespace(
            returncode=0, stdout="ipps://Studio._ipps._tcp.local/\n", stderr=""
        ),
    )
    assert dispatch({"schema": 1, "action": "devices"}, printing) == {
        "devices": [
            {
                "uri": "ipps://Studio._ipps._tcp.local/",
                "device-info": "Studio._ipps._tcp.local",
                "device-class": "network",
            }
        ],
        "warnings": ["CUPS device discovery: Forbidden"],
    }
