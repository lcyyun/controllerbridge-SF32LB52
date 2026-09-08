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
    ROOT / "protocol" / "sf32lb52_bridge_mapping.c",
    ROOT / "tools" / "test_bridge_mapping.c",
]
NVDS_STUB = ROOT / "tools" / "mapping_nvds_stub"
STORE_SOURCES = [
    *SOURCES[:2],
    ROOT / "src" / "sf32lb52_bridge_mapping_store.c",
    NVDS_STUB / "fake_nvds.c",
    ROOT / "tools" / "test_bridge_mapping_store.c",
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
        raise SystemExit("A host gcc/clang compiler is required for mapping tests")

    with tempfile.TemporaryDirectory(prefix="sf32lb52_mapping_test_") as temp_dir:
        executable = pathlib.Path(temp_dir) / "test_bridge_mapping.exe"
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
        store_executable = pathlib.Path(temp_dir) / "test_bridge_mapping_store.exe"
        store_command = [
            compiler, "-std=c11", "-Wall", "-Wextra", "-Werror",
            "-I", str(INCLUDE), "-I", str(NVDS_STUB),
            *(str(source) for source in STORE_SOURCES),
            "-o", str(store_executable),
        ]
        print("+", " ".join(store_command))
        subprocess.check_call(store_command, env=env)
        print("+", store_executable)
        subprocess.check_call([str(store_executable)], env=env)
    print("OK bridge mapping/command/native-passthrough tests")
    return 0


if __name__ == "__main__":
    sys.exit(main())
