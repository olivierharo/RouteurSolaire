import os
import re
Import("env")

SOURCECODE = "./routeursolaire/routeursolaire_defines.h"

# Search Version
version = ""
file1 = open(SOURCECODE, 'r')
Lines = file1.readlines()

count = 0
# Strips the newline character
for line in Lines:
    match = re.search(r"^#define\s+ROUTEURSOLAIRE_VERSION\s+.(.*).\s*$", line)
    if match:
        version = match.group(1)
        file1.close
        break

if version == "":
    raise Exception(f"No version found in {SOURCECODE}")

# Rename binary according to environnement/board
env.Replace(ESP8266_FS_IMAGE_NAME=f"RouteurSolaire{version}_filesystem")
env.Replace(PROGNAME=f"RouteurSolaire{version}_firmware")