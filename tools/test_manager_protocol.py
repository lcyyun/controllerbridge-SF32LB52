#!/usr/bin/env python3
import os
import pathlib
import shutil
import subprocess
import sys
import tempfile


ROOT = pathlib.Path(__file__).resolve().parents[1]
INCLUDE = ROOT / "include"
SOURCES = [
    ROOT / "protocol" / "sf32lb52_bridge_protocol.c",
    ROOT / "protocol" / "sf32lb52_manager_protocol.c",
    ROOT / "tools" / "test_manager_protocol.c",
]


def find_compiler():
    for name in ("gcc", "clang"):
        compiler = shutil.which(name)
        if compiler:
            return compiler
    fallback = pathlib.Path("C:/msys64/ucrt64/bin/gcc.exe")
    return str(fallback) if fallback.exists() else None


def main():
    compiler = find_compiler()
    if not compiler:
        raise SystemExit("A host gcc/clang compiler is required for manager tests")

    with tempfile.TemporaryDirectory(prefix="sf32lb52_manager_test_") as temp_dir:
        executable = pathlib.Path(temp_dir) / "test_manager_protocol.exe"
        command = [
            compiler,
            "-std=c11",
            "-Wall",
            "-Wextra",
            "-Werror",
            "-I",
            str(INCLUDE),
            *(str(source) for source in SOURCES),
            "-o",
            str(executable),
        ]
        env = os.environ.copy()
        compiler_dir = str(pathlib.Path(compiler).resolve().parent)
        env["PATH"] = compiler_dir + os.pathsep + env.get("PATH", "")
        print("+", " ".join(command))
        subprocess.check_call(command, env=env)
        print("+", executable)
        subprocess.check_call([str(executable)], env=env)
    print("OK source-aware manager rumble/status tests")
    return 0


if __name__ == "__main__":
    sys.exit(main())
