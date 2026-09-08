#!/usr/bin/env python3
"""Offline structural checks for the SF32LB52 Xbox/DS5/NS2 USB personas."""

import pathlib
import re
import sys


ROOT = pathlib.Path(__file__).resolve().parents[1]
USB_C = ROOT / "usb" / "sf32lb52_usb_device.c"
HDR = ROOT / "include" / "sf32lb52_usb_device.h"
DEFAULT_DEFINES = {"SF32LB52_USB_USE_CHERRYUSB"}


def evaluate_defined_condition(expression):
    """Evaluate #if expressions made only from defined(), !, && and ||."""
    expression = re.sub(
        r"defined\((\w+)\)",
        lambda match: "True" if match.group(1) in DEFAULT_DEFINES else "False",
        expression,
    )
    expression = expression.replace("&&", " and ").replace("||", " or ")
    expression = re.sub(r"!(?!=)", " not ", expression)
    if not re.fullmatch(r"[\s()TrueFalsandornot]+", expression):
        return None
    return bool(eval(expression, {"__builtins__": {}}, {}))


def preprocess_default(text):
    """Keep the normal CherryUSB build and discard the optional mouse smoke path."""
    output = []
    keep_stack = [True]

    for line in text.splitlines():
        stripped = line.strip()
        match = re.fullmatch(r"#if defined\((\w+)\)", stripped)
        if match:
            keep_stack.append(keep_stack[-1] and match.group(1) in DEFAULT_DEFINES)
            continue
        match = re.fullmatch(r"#if !defined\((\w+)\)", stripped)
        if match:
            keep_stack.append(keep_stack[-1] and match.group(1) not in DEFAULT_DEFINES)
            continue
        if stripped.startswith("#ifdef "):
            name = stripped[7:].strip()
            keep_stack.append(keep_stack[-1] and name in DEFAULT_DEFINES)
            continue
        if stripped.startswith("#ifndef "):
            name = stripped[8:].strip()
            keep_stack.append(keep_stack[-1] and name not in DEFAULT_DEFINES)
            continue
        if stripped.startswith("#if "):
            condition = evaluate_defined_condition(stripped[4:].strip())
            keep_stack.append(
                keep_stack[-1] if condition is None else keep_stack[-1] and condition
            )
            continue
        if stripped == "#else":
            parent = keep_stack[-2]
            keep_stack[-1] = parent and not keep_stack[-1]
            continue
        if stripped == "#endif":
            keep_stack.pop()
            continue
        if keep_stack[-1]:
            output.append(line)

    if len(keep_stack) != 1:
        raise SystemExit("unterminated preprocessor conditional")
    return "\n".join(output)


def parse_numeric_defines(*texts):
    values = {}
    for text in texts:
        for name, value in re.findall(
            r"^\s*#define\s+(\w+)\s+([+-]?(?:0x[0-9a-fA-F]+|\d+))[uUlL]*\s*$",
            text,
            re.M,
        ):
            values[name] = int(value, 0)
    return values


def parse_array_body(text, name):
    match = re.search(
        r"static const uint8_t\s+" + re.escape(name) +
        r"\[\]\s*=\s*\{(?P<body>.*?)\};",
        text,
        re.S,
    )
    if not match:
        raise SystemExit(f"missing array {name}")
    body = re.sub(r"/\*.*?\*/", "", match.group("body"), flags=re.S)
    body = re.sub(r"//.*", "", body)
    return body


def parse_array_tokens(text, name):
    body = parse_array_body(text, name)
    return [token.strip() for token in body.replace("\n", " ").split(",") if token.strip()]


def descriptor_len(tokens):
    return sum(2 if token.startswith("U16_LE(") else 1 for token in tokens)


def resolve_scalar(token, defines, array_lengths):
    token = token.strip()
    if token in defines:
        return defines[token]
    if re.fullmatch(r"[+-]?(?:0x[0-9a-fA-F]+|\d+)[uUlL]*", token):
        return int(token.rstrip("uUlL"), 0)
    if re.fullmatch(r"'.'", token, re.S):
        return ord(token[1])
    match = re.fullmatch(r"sizeof\((\w+)\)", token)
    if match and match.group(1) in array_lengths:
        return array_lengths[match.group(1)]
    raise ValueError(f"cannot resolve token {token!r}")


def tokens_to_bytes(tokens, defines, array_lengths):
    result = []
    for token in tokens:
        match = re.fullmatch(r"U16_LE\((.*)\)", token)
        if match:
            value = resolve_scalar(match.group(1), defines, array_lengths)
            result.extend((value & 0xFF, (value >> 8) & 0xFF))
        else:
            result.append(resolve_scalar(token, defines, array_lengths) & 0xFF)
    return bytes(result)


def parse_config(data):
    if len(data) < 9:
        raise ValueError("configuration descriptor shorter than header")
    records = []
    offset = 0
    while offset < len(data):
        length = data[offset]
        if length == 0 or offset + length > len(data):
            raise ValueError(f"invalid descriptor length {length} at offset {offset}")
        records.append(data[offset:offset + length])
        offset += length
    return records


def has_subsequence(data, sequence):
    return bytes(sequence) in bytes(data)


def main():
    raw_usb_text = USB_C.read_text(encoding="utf-8")
    usb_text = preprocess_default(raw_usb_text)
    hdr_text = HDR.read_text(encoding="utf-8")
    defines = parse_numeric_defines(usb_text, hdr_text)

    array_names = [
        "xbox_device_descriptor",
        "dualsense_device_descriptor",
        "dualsense_edge_device_descriptor",
        "nintendo_device_descriptor",
        "manager_hid_report_descriptor",
        "nintendo_hid_report_descriptor",
        "dualsense_hid_report_descriptor",
        "dualsense_edge_hid_report_descriptor",
        "xbox_configuration_descriptor",
        "dualsense_configuration_descriptor",
        "nintendo_configuration_descriptor",
        "msosv2_wcid_descriptor",
    ]
    tokens = {name: parse_array_tokens(usb_text, name) for name in array_names}
    array_lengths = {name: descriptor_len(value) for name, value in tokens.items()}
    arrays = {
        name: tokens_to_bytes(value, defines, array_lengths)
        for name, value in tokens.items()
    }

    checks = []

    role_expectations = {
        "xbox": {
            "vid": 0x045E,
            "pid": 0x028E,
            "device": "xbox_device_descriptor",
            "config": "xbox_configuration_descriptor",
            "total_macro": "XBOX_USB_CONFIG_TOTAL_LEN",
            "interfaces": 2,
        },
        "dualsense": {
            "vid": 0x054C,
            "pid": 0x0CE6,
            "device": "dualsense_device_descriptor",
            "config": "dualsense_configuration_descriptor",
            "total_macro": "DUALSENSE_USB_CONFIG_TOTAL_LEN",
            "interfaces": 4,
        },
        "nintendo": {
            "vid": 0x057E,
            "pid": 0x2069,
            "device": "nintendo_device_descriptor",
            "config": "nintendo_configuration_descriptor",
            "total_macro": "NINTENDO_USB_CONFIG_TOTAL_LEN",
            "interfaces": 2,
        },
    }

    parsed_configs = {}
    for role, expected in role_expectations.items():
        device = arrays[expected["device"]]
        config = arrays[expected["config"]]
        total = config[2] | (config[3] << 8)
        vid = device[8] | (device[9] << 8)
        pid = device[10] | (device[11] << 8)
        try:
            parsed_configs[role] = parse_config(config)
            walk_ok = True
        except ValueError as error:
            parsed_configs[role] = []
            walk_ok = False
            checks.append((False, f"{role} descriptor walk: {error}"))

        checks.extend([
            (len(device) == 18, f"{role} device descriptor is {len(device)} bytes"),
            (vid == expected["vid"] and pid == expected["pid"],
             f"{role} VID/PID is {vid:04x}:{pid:04x}"),
            (total == len(config) == defines[expected["total_macro"]],
             f"{role} config total is {total} bytes"),
            (config[4] == expected["interfaces"],
             f"{role} interface count is {config[4]}"),
            (walk_ok, f"{role} configuration descriptor walk reaches the end"),
        ])

    xbox_config = arrays["xbox_configuration_descriptor"]
    xbox_records = parsed_configs["xbox"]
    xbox_interfaces = [record for record in xbox_records if len(record) >= 9 and record[1] == 0x04]
    xbox_endpoints = [record for record in xbox_records if len(record) >= 7 and record[1] == 0x05]
    checks.extend([
        (len(xbox_interfaces) == 2, "Xbox exposes XUSB plus management interfaces"),
        (xbox_interfaces and xbox_interfaces[0][5:8] == bytes((0xFF, 0x5D, 0x01)),
         "Xbox primary interface is ff/5d/01 XUSB"),
        (len(xbox_interfaces) > 1 and xbox_interfaces[1][5] == 0x03,
         "Xbox management interface is HID"),
        (has_subsequence(
            xbox_config,
            (0x11, 0x21, 0x00, 0x01, 0x01, 0x25, 0x81, 20,
             0, 0, 0, 0, 0x13, 0x02, 8, 0, 0),
         ), "Xbox class descriptor advertises 20-byte IN and 8-byte OUT"),
        (any(ep[2] == 0x81 and (ep[4] | ep[5] << 8) == 32 for ep in xbox_endpoints),
         "Xbox interrupt IN endpoint is present"),
        (any(ep[2] == 0x02 and (ep[4] | ep[5] << 8) == 32 for ep in xbox_endpoints),
         "Xbox interrupt OUT endpoint is present"),
        (any(ep[2] == 0x85 and (ep[4] | ep[5] << 8) == 64 for ep in xbox_endpoints),
         "Xbox manager uses a separate SiFli IN endpoint"),
    ])

    ds_config = parsed_configs["dualsense"]
    ds_interfaces = [record for record in ds_config if len(record) >= 9 and record[1] == 0x04]
    ds_endpoints = [record for record in ds_config if len(record) >= 7 and record[1] == 0x05]
    checks.extend([
        (arrays["dualsense_device_descriptor"][16] == 3 and
         arrays["dualsense_edge_device_descriptor"][16] == 3,
         "Sony personas expose adapter-only serial strings"),
        (any(intf[2] == 3 and intf[5] == 0x03 for intf in ds_interfaces),
         "DualSense persona exposes HID interface 3"),
        (sum(1 for intf in ds_interfaces if intf[5:7] == bytes((0x01, 0x02))) == 4,
         "DualSense exposes alternate 0/1 for both UAC streaming interfaces"),
        (any(ep[2] == 0x02 and ep[3] == 0x09 and
             (ep[4] | ep[5] << 8) == 392 for ep in ds_endpoints),
         "DualSense speaker/haptics OUT is adaptive isochronous 392 bytes"),
        (any(ep[2] == 0x86 and ep[3] == 0x05 and
             (ep[4] | ep[5] << 8) == 196 for ep in ds_endpoints),
         "DualSense microphone IN is asynchronous isochronous 196 bytes"),
        (any(ep[2] == 0x85 and (ep[4] | ep[5] << 8) == 64 for ep in ds_endpoints),
         "DualSense interrupt IN endpoint is 64 bytes"),
        (any(ep[2] == 0x03 and (ep[4] | ep[5] << 8) == 64 for ep in ds_endpoints),
         "DualSense interrupt OUT endpoint is 64-byte capable"),
        (not ({ep[2] & 0x7f for ep in ds_endpoints if ep[2] & 0x80} &
              {ep[2] & 0x7f for ep in ds_endpoints if not ep[2] & 0x80}),
         "DualSense does not share endpoint numbers between IN and OUT"),
    ])

    ns_config = parsed_configs["nintendo"]
    msosv2 = arrays["msosv2_wcid_descriptor"]
    ns_interfaces = [record for record in ns_config if len(record) >= 9 and record[1] == 0x04]
    ns_endpoints = [record for record in ns_config if len(record) >= 7 and record[1] == 0x05]
    checks.extend([
        (len(ns_interfaces) == 2 and ns_interfaces[0][5] == 0x03 and ns_interfaces[1][5] == 0xFF,
         "Nintendo exposes HID plus vendor interfaces"),
        (any(ep[2] == 0x81 and (ep[4] | ep[5] << 8) == 64 for ep in ns_endpoints),
         "Nintendo interrupt IN endpoint is 64 bytes"),
        (any(ep[2] == 0x02 and ep[3] == 0x03 and
             (ep[4] | ep[5] << 8) == 64 for ep in ns_endpoints),
         "Nintendo interrupt OUT endpoint is 64 bytes"),
        (any(ep[2] == 0x86 and ep[3] == 0x02 for ep in ns_endpoints),
         "Nintendo vendor bulk IN uses a SiFli IN endpoint"),
        (any(ep[2] == 0x03 and ep[3] == 0x02 for ep in ns_endpoints),
         "Nintendo vendor bulk OUT uses a separate SiFli OUT endpoint"),
        ("NINTENDO_VENDOR_ITF_NUM, 0x00, U16_LE(0x00a0)" in usb_text,
         "Nintendo MS OS 2.0 function subset targets its vendor interface"),
        ("U16_LE(0x0008), U16_LE(0x0001)" in usb_text,
         "Nintendo MS OS 2.0 descriptor contains a configuration subset"),
        (".msosv1_descriptor" not in usb_text,
         "Nintendo avoids CherryUSB's simultaneous MS OS 1.0/2.0 dispatch bug"),
        (len(msosv2) == 178 and msosv2[8:10] == bytes((178, 0)),
         "Nintendo MS OS 2.0 descriptor set is 178 bytes"),
        (msosv2[10:18] == bytes((8, 0, 1, 0, 0, 0, 168, 0)),
         "Nintendo MS OS 2.0 configuration subset is well formed"),
        (msosv2[18:26] == bytes((8, 0, 2, 0, 1, 0, 160, 0)),
         "Nintendo MS OS 2.0 function subset binds interface 1"),
    ])

    manager_report = arrays["manager_hid_report_descriptor"]
    nintendo_report = arrays["nintendo_hid_report_descriptor"]
    dualsense_report = arrays["dualsense_hid_report_descriptor"]
    dse_device = arrays["dualsense_edge_device_descriptor"]
    dse_report = arrays["dualsense_edge_hid_report_descriptor"]
    manager_pattern = bytes((0x85, 0x7F, 0x09, 0x01, 0x15, 0x00,
                             0x26, 0xFF, 0x00, 0x75, 0x08, 0x95, 0x3F, 0xB1, 0x02))
    checks.extend([
        (manager_pattern in manager_report, "standalone manager HID has feature report 0x7f/63"),
        (has_subsequence(nintendo_report, (0x85, 0x05, 0x95, 0x3F, 0x09, 0x01, 0x81, 0x02)),
         "Nintendo input report is ID 0x05 with 63-byte payload"),
        (has_subsequence(nintendo_report, (0x85, 0x02, 0x95, 0x3F, 0x09, 0x01, 0x91, 0x02)),
         "Nintendo output report is ID 0x02 with 63-byte payload"),
        (has_subsequence(nintendo_report, (0x85, 0x7F, 0x95, 0x3F)),
         "Nintendo keeps feature report 0x7f"),
        (len(dualsense_report) == 321, f"DualSense HID report descriptor is {len(dualsense_report)} bytes"),
        (has_subsequence(dualsense_report, (0x85, 0x01)) and
         has_subsequence(dualsense_report, (0x95, 0x34, 0x81, 0x02)),
         "DualSense input report is ID 0x01 and totals 64 bytes"),
        (has_subsequence(dualsense_report, (0x85, 0x02, 0x09, 0x23, 0x95, 0x2F, 0x91, 0x02)),
         "DualSense output report is ID 0x02 and totals 48 bytes"),
        (not has_subsequence(dualsense_report, (0x85, 0x7F)),
         "DualSense keeps its authentic report descriptor"),
        (has_subsequence(dualsense_report, (0x85, 0xF6, 0x09, 0x37, 0x95, 0x3F, 0xB1, 0x02)),
         "DualSense uses native dongle feature report 0xf6 for management"),
        (len(dse_device) == 18 and
         (dse_device[8] | dse_device[9] << 8) == 0x054C and
         (dse_device[10] | dse_device[11] << 8) == 0x0DF2,
         "DualSense Edge identity is 054c:0df2"),
        (len(dse_report) == 445,
         f"DualSense Edge HID report descriptor is {len(dse_report)} bytes"),
        (has_subsequence(dse_report, (0x85, 0x02, 0x09, 0x23, 0x95, 0x3F, 0x91, 0x02)),
         "DualSense Edge output report is ID 0x02 and totals 64 bytes"),
        (has_subsequence(dse_report, (0x85, 0x70, 0x09, 0x48)) and
         has_subsequence(dse_report, (0x85, 0x7B, 0x09, 0x53)),
         "DualSense Edge exposes profile feature reports 0x70-0x7b"),
    ])

    checks.extend([
        (defines.get("SF32LB52_USB_XBOX_INPUT_REPORT_SIZE") == 20 and
         defines.get("SF32LB52_USB_XBOX_OUTPUT_REPORT_SIZE") == 8,
         "Xbox public wire sizes are 20/8"),
        (defines.get("SF32LB52_USB_DS5_INPUT_REPORT_SIZE") == 64 and
         defines.get("SF32LB52_USB_DS5_OUTPUT_REPORT_SIZE") == 48,
         "DualSense public wire sizes are 64/48"),
        (defines.get("SF32LB52_USB_DSE_OUTPUT_REPORT_SIZE") == 64,
         "DualSense Edge public wire sizes are 64/64"),
        (defines.get("SF32LB52_USB_NINTENDO_INPUT_REPORT_SIZE") == 64 and
         defines.get("SF32LB52_USB_NINTENDO_OUTPUT_REPORT_SIZE") == 64,
         "Nintendo public wire sizes are 64/64"),
        (raw_usb_text.count(".audio_capable = false") == 2 and
         raw_usb_text.count(".audio_capable = true") == 2,
         "only the Sony roles declare USB audio capability"),
        ("usbd_deinitialize(0)" in raw_usb_text and
         "usb_register_current_role()" in raw_usb_text and
         "usb_phy_disconnect()" in raw_usb_text and
         "usb_phy_connect()" in raw_usb_text,
         "runtime role switch performs disconnect/deinit/register/reconnect"),
        ("USB_ROLE_SWITCH_REPLY_GRACE_MS" in raw_usb_text and
         "role_switch_requested_ms" in raw_usb_text,
         "runtime role switch leaves time for the manager ACK"),
        ("usbd_hid_init_intf(0, &manager_hid_interface" in raw_usb_text,
         "Xbox registers only its management HID descriptor"),
        ("usbd_hid_init_intf(0, &gamepad_hid_interface" in raw_usb_text and
         "active_hid_report_descriptor" in raw_usb_text,
         "HID roles register the active report descriptor"),
    ])

    failed = False
    for ok, message in checks:
        print(("OK   " if ok else "FAIL ") + message)
        failed = failed or not ok
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
