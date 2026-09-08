#!/usr/bin/env python3
import os
import pathlib
import shutil
import subprocess
import sys
import tempfile


ROOT = pathlib.Path(__file__).resolve().parents[1]
TEST_C = ROOT / "tools" / "test_audio_haptics.c"
PROCESSOR_C = ROOT / "protocol" / "sf32lb52_audio_haptics.c"
INCLUDE = ROOT / "include"


def find_host_cc():
    for name in ("gcc", "clang"):
        path = shutil.which(name)
        if path:
            return path
    for candidate in (
        pathlib.Path("C:/msys64/ucrt64/bin/gcc.exe"),
        pathlib.Path("C:/msys64/mingw64/bin/gcc.exe"),
        pathlib.Path("C:/Program Files/LLVM/bin/clang.exe"),
    ):
        if candidate.exists():
            return str(candidate)
    return None


def main():
    compiler = find_host_cc()
    if not compiler:
        raise SystemExit("No host C compiler found for audio haptics tests")

    with tempfile.TemporaryDirectory(prefix="sf32lb52_audio_haptics_") as temp_dir:
        executable = pathlib.Path(temp_dir) / "test_audio_haptics.exe"
        env = os.environ.copy()
        env["PATH"] = str(pathlib.Path(compiler).parent) + os.pathsep + env.get("PATH", "")
        command = [
            compiler,
            "-std=c11",
            "-Wall",
            "-Wextra",
            "-Werror",
            "-I",
            str(INCLUDE),
            str(PROCESSOR_C),
            str(TEST_C),
            "-o",
            str(executable),
        ]
        print("+", " ".join(command))
        subprocess.check_call(command, env=env)
        subprocess.check_call([str(executable)], env=env)
        print("OK audio haptics runtime tests")
    return 0


if __name__ == "__main__":
    sys.exit(main())
