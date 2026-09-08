#!/usr/bin/env python3
import os
import pathlib
import shutil
import subprocess
import tempfile


ROOT = pathlib.Path(__file__).resolve().parents[1]


def find_compiler():
    for name in ("gcc", "clang"):
        compiler = shutil.which(name)
        if compiler:
            return compiler
    for candidate in (
        pathlib.Path("C:/msys64/ucrt64/bin/gcc.exe"),
        pathlib.Path("C:/msys64/mingw64/bin/gcc.exe"),
        pathlib.Path("C:/Program Files/LLVM/bin/clang.exe"),
    ):
        if candidate.exists():
            return str(candidate)
    return None


def main():
    compiler = find_compiler()
    if not compiler:
        raise SystemExit("A host gcc/clang compiler is required")
    with tempfile.TemporaryDirectory(prefix="sf32lb52_ns2_vendor_") as temp:
        executable = pathlib.Path(temp) / "test_ns2_vendor.exe"
        command = [
            compiler,
            "-std=c11",
            "-Wall",
            "-Wextra",
            "-Werror",
            "-I",
            str(ROOT / "include"),
            str(ROOT / "protocol" / "sf32lb52_ns2_vendor.c"),
            str(ROOT / "tools" / "test_ns2_vendor.c"),
            "-o",
            str(executable),
        ]
        env = os.environ.copy()
        env["PATH"] = str(pathlib.Path(compiler).resolve().parent) + os.pathsep + env.get("PATH", "")
        print("+", " ".join(command), flush=True)
        subprocess.check_call(command, env=env)
        print("+", executable, flush=True)
        subprocess.check_call([str(executable)], env=env)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
