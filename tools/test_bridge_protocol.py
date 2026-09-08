#!/usr/bin/env python3
import pathlib
import os
import shutil
import subprocess
import sys
import tempfile


ROOT = pathlib.Path(__file__).resolve().parents[1]
TEST_C = ROOT / "tools" / "test_bridge_protocol.c"
PROTOCOL_C = ROOT / "protocol" / "sf32lb52_bridge_protocol.c"
INCLUDE = ROOT / "include"


def find_exe(names):
    for name in names:
        path = shutil.which(name)
        if path:
            return path
    return None


def find_host_cc():
    compiler = find_exe(["gcc", "clang", "cl"])
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


def run(command, env=None):
    print("+", " ".join(str(part) for part in command))
    subprocess.check_call(command, env=env)


def main():
    with tempfile.TemporaryDirectory(prefix="sf32lb52_bridge_test_") as temp_dir:
        temp = pathlib.Path(temp_dir)
        host_cc = find_host_cc()

        if host_cc and pathlib.Path(host_cc).name.lower() != "cl.exe":
            host_env = os.environ.copy()
            host_env["PATH"] = (
                str(pathlib.Path(host_cc).parent)
                + os.pathsep
                + host_env.get("PATH", "")
            )
            executable = temp / "test_bridge_protocol.exe"
            run([
                host_cc,
                "-std=c11",
                "-Wall",
                "-Wextra",
                "-Werror",
                "-I",
                str(INCLUDE),
                str(PROTOCOL_C),
                str(TEST_C),
                "-o",
                str(executable),
            ], env=host_env)
            run([str(executable)], env=host_env)
            print("OK bridge protocol runtime tests")
            return 0

        if host_cc and pathlib.Path(host_cc).name.lower() == "cl.exe":
            executable = temp / "test_bridge_protocol.exe"
            run([
                host_cc,
                "/nologo",
                "/std:c11",
                "/W4",
                "/WX",
                "/I",
                str(INCLUDE),
                str(PROTOCOL_C),
                str(TEST_C),
                "/Fe:" + str(executable),
            ])
            run([str(executable)])
            print("OK bridge protocol runtime tests")
            return 0

        arm_cc = find_exe(["arm-none-eabi-gcc"])
        if not arm_cc:
            fallback = pathlib.Path(os.environ.get("RTT_EXEC_PATH", "")) / "arm-none-eabi-gcc.exe"
            if fallback.exists():
                arm_cc = str(fallback)
        if not arm_cc:
            raise SystemExit("No C compiler found for bridge protocol tests")

        protocol_object = temp / "sf32lb52_bridge_protocol.o"
        test_object = temp / "test_bridge_protocol.o"
        common = [
            arm_cc,
            "-std=c11",
            "-Wall",
            "-Wextra",
            "-Werror",
            "-I",
            str(INCLUDE),
            "-c",
        ]
        run(common + [str(PROTOCOL_C), "-o", str(protocol_object)])
        run(common + [str(TEST_C), "-o", str(test_object)])
        print("OK bridge protocol compile tests (runtime skipped: no host C compiler)")
        return 0


if __name__ == "__main__":
    sys.exit(main())
