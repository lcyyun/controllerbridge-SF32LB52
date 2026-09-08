#!/usr/bin/env python3
import pathlib
import os
import shutil
import subprocess
import sys
import tempfile


ROOT = pathlib.Path(__file__).resolve().parents[1]
INCLUDE = ROOT / "include"
SOURCES = [
    ROOT / "protocol" / "sf32lb52_bridge_protocol.c",
    ROOT / "protocol" / "sf32lb52_bridge_mapping.c",
    ROOT / "src" / "sf32lb52_bridge_settings.c",
    ROOT / "src" / "sf32lb52_bridge_mapping_store.c",
    ROOT / "src" / "sf32lb52_bridge_runtime.c",
    ROOT / "tools" / "test_bridge_runtime.c",
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
        raise SystemExit("A host gcc/clang compiler is required for runtime tests")

    with tempfile.TemporaryDirectory(prefix="sf32lb52_runtime_test_") as temp_dir:
        executable = pathlib.Path(temp_dir) / "test_bridge_runtime.exe"
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
        stub = ROOT / "tools" / "mapping_nvds_stub"
        nvds_executable = pathlib.Path(temp_dir) / "test_bridge_runtime_nvds.exe"
        nvds_command = [
            compiler, "-std=c11", "-Wall", "-Wextra", "-Werror",
            "-DTEST_MAPPING_NVDS", "-I", str(INCLUDE), "-I", str(stub),
            *(str(source) for source in SOURCES),
            str(stub / "fake_nvds.c"), "-o", str(nvds_executable),
        ]
        print("+", " ".join(nvds_command))
        subprocess.check_call(nvds_command, env=env)
        print("+", nvds_executable)
        subprocess.check_call([str(nvds_executable)], env=env)
    print("OK bridge runtime source-profile/role-matrix/persistence tests "
          "(host and NVDS stub)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
