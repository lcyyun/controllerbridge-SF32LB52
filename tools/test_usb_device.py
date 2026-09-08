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
    ROOT / "usb" / "sf32lb52_usb_device.c",
    ROOT / "tools" / "test_usb_device.c",
]


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
        raise SystemExit("A host gcc/clang compiler is required for USB tests")

    with tempfile.TemporaryDirectory(prefix="sf32lb52_usb_test_") as temp_dir:
        production_object = pathlib.Path(temp_dir) / "sf32lb52_usb_device.o"
        test_object = pathlib.Path(temp_dir) / "test_usb_device.o"
        executable = pathlib.Path(temp_dir) / "test_usb_device.exe"
        common = [
            compiler,
            "-std=c11",
            "-Wall",
            "-Wextra",
            "-Werror",
            "-I",
            str(INCLUDE),
        ]
        env = os.environ.copy()
        compiler_dir = str(pathlib.Path(compiler).resolve().parent)
        env["PATH"] = compiler_dir + os.pathsep + env.get("PATH", "")

        production_flags = []
        if os.name == "nt" and "gcc" in pathlib.Path(compiler).name.lower():
            # PE/COFF MinGW emits the firmware's weak callbacks as unresolved
            # weak externals instead of resolving their in-TU bodies.  Remove
            # attributes only from this host-test object; firmware builds and
            # the test object's CRT headers retain their normal attributes.
            production_flags.append("-D__attribute__(x)=")

        commands = [
            common + production_flags + [
                "-c", str(SOURCES[0]), "-o", str(production_object)
            ],
            common + ["-c", str(SOURCES[1]), "-o", str(test_object)],
            [compiler, str(production_object), str(test_object),
             "-o", str(executable)],
        ]
        for command in commands:
            print("+", " ".join(command), flush=True)
            subprocess.check_call(command, env=env)
        print("+", executable, flush=True)
        subprocess.check_call([str(executable)], env=env)

    print("OK USB Feature SET FIFO and neutral GET_INPUT tests")
    return 0


if __name__ == "__main__":
    sys.exit(main())
