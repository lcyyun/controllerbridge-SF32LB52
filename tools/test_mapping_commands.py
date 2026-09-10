#!/usr/bin/env python3
"""Compile the real NS2 mapping command path with fail-fast hardware stubs."""
import os
import pathlib
import subprocess
import tempfile

from test_bridge_mapping import find_compiler

ROOT = pathlib.Path(__file__).resolve().parents[1]


def main():
    compiler = find_compiler()
    if not compiler:
        raise SystemExit("A host gcc/clang compiler is required")
    sources = [
        "protocol/sf32lb52_bridge_protocol.c",
        "protocol/sf32lb52_bridge_mapping.c",
        "protocol/sf32lb52_ns2_protocol.c",
        "protocol/sf32lb52_manager_protocol.c",
        "src/sf32lb52_bridge_settings.c",
        "src/sf32lb52_bridge_mapping_store.c",
        "src/sf32lb52_bridge_runtime.c",
        "tools/mapping_nvds_stub/fake_nvds.c",
        "tools/test_mapping_commands.c",
    ]
    env = os.environ.copy()
    env["PATH"] = str(pathlib.Path(compiler).parent) + os.pathsep + env.get("PATH", "")
    with tempfile.TemporaryDirectory(prefix="sf32_mapping_commands_") as temp:
        executable = pathlib.Path(temp) / "test_mapping_commands.exe"
        command = [
            compiler, "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
            "-I", str(ROOT / "include"),
            "-I", str(ROOT / "tools" / "mapping_nvds_stub"),
            *(str(ROOT / source) for source in sources), "-o", str(executable),
        ]
        print("+", " ".join(command), flush=True)
        subprocess.run(command, check=True, env=env, timeout=120)
        subprocess.run([str(executable)], check=True, env=env, timeout=30)


if __name__ == "__main__":
    main()
