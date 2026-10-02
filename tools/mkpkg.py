#!/usr/bin/env python3
"""Stage the A314Mount release drawer.

    sh build.sh            # (in WSL) builds out/A314Mount + out/A314MountGUI
    python3 tools/mkpkg.py

Produces out/release/A314Mount/ (everything the README's Install section
lists) plus out/release/A314Mount.info beside it, so the drawer shows on
Workbench. Archive that directory with LhA to keep the protection bits.
"""
import os
import shutil
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "out")
REL = os.path.join(OUT, "release")
DRAWER = os.path.join(REL, "A314Mount")

# (source relative to the repo root, name inside the drawer)
FILES = [
    ("out/A314Mount",               "A314Mount"),
    ("out/A314MountGUI",            "A314MountGUI"),
    ("install/A314MountGUI.info",   "A314MountGUI.info"),
    ("install/Install",             "Install"),
    ("install/Install.info",        "Install.info"),
    ("install/A314Mount.guide",     "A314Mount.guide"),
    ("install/A314Mount.guide.info", "A314Mount.guide.info"),
    ("install/PD0",                 "PD0"),
    ("install/PD1",                 "PD1"),
    ("install/PD2",                 "PD2"),
    ("install/PD3",                 "PD3"),
    ("install/mkhdfmount.py",       "mkhdfmount.py"),
    ("README.md",                   "README.md"),
    ("LICENSE",                     "LICENSE"),
]


def main():
    missing = [s for s, _ in FILES if not os.path.isfile(os.path.join(ROOT, s))]
    if missing:
        sys.exit("missing (run build.sh first?): " + ", ".join(missing))
    if os.path.isdir(REL):
        shutil.rmtree(REL)
    os.makedirs(DRAWER)
    for src, dst in FILES:
        shutil.copyfile(os.path.join(ROOT, src), os.path.join(DRAWER, dst))
    shutil.copyfile(os.path.join(ROOT, "install", "A314Mount.info"),
                    os.path.join(REL, "A314Mount.info"))
    for dirpath, _, names in os.walk(REL):
        for n in sorted(names):
            p = os.path.join(dirpath, n)
            print("%7d  %s" % (os.path.getsize(p), os.path.relpath(p, REL)))


if __name__ == "__main__":
    main()
