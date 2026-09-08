import os
import sys
import rtconfig

SIFLI_SDK = os.getenv("SIFLI_SDK")
if not SIFLI_SDK:
    print("Please run export.ps1 in root folder of SiFli SDK to set environment.")
    exit(1)

sys.path.insert(0, os.path.join(Dir("#").abspath, "tools"))
from local_objects import install

install(Dir("#").abspath, SIFLI_SDK)

from building import *

PrepareEnv(board="sf32lb52-nano_n16r16")

AddBootLoader(SIFLI_SDK, rtconfig.CHIP)
SifliEnv()

TARGET = rtconfig.OUTPUT_DIR + rtconfig.TARGET_NAME + "." + rtconfig.TARGET_EXT
objs = PrepareBuilding(None)
env = GetCurrentEnv()

DoBuilding(TARGET, objs)
AddFTAB(SIFLI_SDK, rtconfig.CHIP)
GenDownloadScript(env)
