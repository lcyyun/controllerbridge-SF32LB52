#!/usr/bin/env python3
"""Exercise real app callback mailboxes without linking hardware sections."""
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
    env = os.environ.copy()
    env["PATH"] = str(pathlib.Path(compiler).parent) + os.pathsep + env.get("PATH", "")
    with tempfile.TemporaryDirectory(prefix="sf32_input_mailbox_") as temp:
        executable = pathlib.Path(temp) / "test_input_mailbox.exe"
        subprocess.run([
            compiler, "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
            "-fwhole-program",
            "-ffunction-sections", "-fdata-sections", "-Wl,--gc-sections",
            "-I", str(ROOT / "include"), str(ROOT / "tools/test_input_mailbox.c"),
            "-o", str(executable),
        ], check=True, env=env, timeout=120)
        subprocess.run([str(executable)], check=True, env=env, timeout=30)

if __name__ == "__main__":
    main()
