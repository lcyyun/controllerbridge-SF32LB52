#!/usr/bin/env python3
import os
import pathlib
import shutil
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[1]
TEST_C = ROOT / "tools" / "test_ns2_protocol.c"
PROTOCOL_C = ROOT / "protocol" / "sf32lb52_ns2_protocol.c"
INCLUDE = ROOT / "include"


def find_exe(names):
    for name in names:
        path = shutil.which(name)
        if path:
            return path
    return None


def run(cmd, env=None):
    print("+", " ".join(str(part) for part in cmd))
    subprocess.check_call(cmd, env=env)


def main():
    with tempfile.TemporaryDirectory(prefix="sf32lb52_ns2_test_") as tmp:
        tmp_path = pathlib.Path(tmp)
        host_cc = find_exe(["gcc", "clang", "cl"])
        if not host_cc:
            for fallback in [
                pathlib.Path("C:/msys64/ucrt64/bin/gcc.exe"),
                pathlib.Path("C:/msys64/mingw64/bin/gcc.exe"),
            ]:
                if fallback.exists():
                    host_cc = str(fallback)
                    break
        host_env = os.environ.copy()
        if host_cc:
            compiler_dir = str(pathlib.Path(host_cc).resolve().parent)
            host_env["PATH"] = compiler_dir + os.pathsep + host_env.get("PATH", "")
        if host_cc and pathlib.Path(host_cc).name.lower() != "cl.exe":
            exe = tmp_path / "test_ns2_protocol.exe"
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
                str(exe),
            ], env=host_env)
            run([str(exe)], env=host_env)
            print("OK ns2 protocol runtime tests")
            return 0

        if host_cc and pathlib.Path(host_cc).name.lower() == "cl.exe":
            exe = tmp_path / "test_ns2_protocol.exe"
            run([
                host_cc,
                "/nologo",
                "/W4",
                "/WX",
                "/I",
                str(INCLUDE),
                str(PROTOCOL_C),
                str(TEST_C),
                "/Fe:" + str(exe),
            ], env=host_env)
            run([str(exe)], env=host_env)
            print("OK ns2 protocol runtime tests")
            return 0

        arm_cc = find_exe(["arm-none-eabi-gcc"])
        if not arm_cc:
            fallback = pathlib.Path(os.environ.get("RTT_EXEC_PATH", "")) / "arm-none-eabi-gcc.exe"
            if fallback.exists():
                arm_cc = str(fallback)
        if not arm_cc:
            raise SystemExit("No C compiler found for protocol tests")

        obj = tmp_path / "sf32lb52_ns2_protocol.o"
        run([
            arm_cc,
            "-std=c11",
            "-Wall",
            "-Wextra",
            "-Werror",
            "-I",
            str(INCLUDE),
            "-c",
            str(PROTOCOL_C),
            "-o",
            str(obj),
        ])
        print("OK ns2 protocol compile test (runtime skipped: no host C compiler)")
        return 0


if __name__ == "__main__":
    sys.exit(main())
