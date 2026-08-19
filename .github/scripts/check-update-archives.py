#!/usr/bin/env python3
"""Refuse to publish a Windows archive the in-app updater cannot install safely.

A release zip without manifest.txt makes the updater fall back to guessing at
the file list, which is exactly what emptied people's installs on every update
before 1.0.4. Shipping portable.txt in a normal build is the other way to ruin
someone's day: it moves the data folder into the install directory. Both are
cheap to check and worth failing a release over.

Run from the directory holding the packaged release files.
"""

import glob
import sys
import zipfile

REQUIRED = ["manifest.txt", "prismlauncher.exe"]
# Only the MSVC archives are ever handed to the in-app updater, so that is where
# the second stage updater has to be present.
REQUIRED_MSVC = ["prismlauncher_updater.exe"]


def main() -> int:
    archives = sorted(glob.glob("PortalLauncher-Windows-*.zip"))
    if not archives:
        print("::error::No Windows archives were produced")
        return 1

    failed = False
    for path in archives:
        with zipfile.ZipFile(path) as zf:
            # Compress-Archive on PowerShell 5.1 stores backslash separators,
            # so never compare raw entry names.
            names = {n.replace("\\", "/") for n in zf.namelist()}

        required = list(REQUIRED)
        if "MSVC" in path:
            required += REQUIRED_MSVC

        missing = [f for f in required if f not in names]
        if missing:
            print(f"::error::{path} is missing {', '.join(missing)}")
            failed = True

        if "portable.txt" in names and "Portable" not in path:
            print(f"::error::{path} ships portable.txt, which moves the data folder into the install")
            failed = True

        print(f"{path}: {len(names)} entries, manifest {'present' if 'manifest.txt' in names else 'MISSING'}")

    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
