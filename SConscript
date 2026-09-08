import os

Import("SIFLI_SDK")
from building import *

cwd = GetCurrentDir()
objs = []

objs = objs + SConscript(
    os.path.join(SIFLI_SDK, "SConscript"),
    variant_dir="sifli_sdk",
    duplicate=0,
)

for subdir in ["app", "src", "protocol", "usb"]:
    objs = objs + SConscript(os.path.join(cwd, subdir, "SConscript"))

Return("objs")
