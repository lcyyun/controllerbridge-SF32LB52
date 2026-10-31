#!/usr/bin/env python3
"""Run the production SiFli BLE adapter against deterministic fake SDK events."""
import os
import pathlib
import shutil
import subprocess
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[1]
cc = os.environ.get("CC") or shutil.which("gcc") or shutil.which("clang")
if not cc:
    for candidate in ("C:/msys64/ucrt64/bin/gcc.exe", "C:/msys64/mingw64/bin/gcc.exe"):
        if pathlib.Path(candidate).is_file():
            cc = candidate
            break
if not cc:
    raise SystemExit("A host C compiler is required; this runtime test cannot be compile-only")
env = os.environ.copy()
env["PATH"] = str(pathlib.Path(cc).resolve().parent) + os.pathsep + env.get("PATH", "")
with tempfile.TemporaryDirectory(prefix="sf32_ble_reconnect_") as tmp:
    exe = pathlib.Path(tmp) / "test_ble_reconnect.exe"
    subprocess.run([cc, "-std=c11", "-Wall", "-Wextra", "-Werror", "-DBLUETOOTH",
                    "-I", str(ROOT / "tools/ble_sdk_stub"), "-I", str(ROOT / "include"),
                    str(ROOT / "tools/test_ble_reconnect.c"),
                    str(ROOT / "protocol/sf32lb52_ns2_gatt.c"), "-o", str(exe)],
                   env=env, check=True)
    subprocess.run([str(exe)], env=env, check=True)
