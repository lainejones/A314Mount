#!/usr/bin/env python3
"""
mkhdfmount - generate a DEVS:DOSDrivers / MountList entry for an HDF image so
it can be mounted on an a314 virtual drive (PD0:-PD3:) with the *correct*
geometry.

Why this exists: the a314 disk service infers an HDF's geometry from its size --
Surfaces=1, BlocksPerTrack=32, HighCyl = size/16384 - 1 -- and (per Niklas's
disk README) the Amiga mount list geometry MUST match the image, or the
filesystem reads the wrong blocks. ADF floppies are all one size so the stock
PD0:-PD3: entries work; HDFs are per-image, hence this.

    mkhdfmount.py <hdf-file> <unit> [MountName]      # reads size from the file
    mkhdfmount.py --size <bytes> <unit> [MountName]  # or give the size directly

Prints the mount entry to stdout. Redirect it to DEVS:DOSDrivers/<MountName>
(no suffix) on the Amiga, then `Mount <MountName>:`. Pure Python, no deps --
runs on the Pi or the host.
"""
import os, sys

SECTOR = 512
CYL_BYTES = 16384                 # 16 kB per "cylinder" for a314 HDF geometry

def emit(name, unit, size):
    if size < (1 << 20):
        sys.exit("error: HDF must be at least 1 MB (got %d bytes)" % size)
    if size % CYL_BYTES != 0:
        sys.exit("error: HDF size must be a multiple of 16 kB (got %d bytes)" % size)
    cyls = size // CYL_BYTES      # geometry (Surfaces=1, BlocksPerTrack=32)
    high = cyls - 1
    print("/* %s: -- a314disk.device unit %d, %d bytes (%d cyls) */" % (name, unit, size, cyls))
    print("%-15s= a314disk.device" % "Device")
    print("%-15s= %d" % ("Unit", unit))
    print("%-15s= 1" % "Flags")
    print("%-15s= 1" % "Surfaces")
    print("%-15s= 32" % "BlocksPerTrack")
    print("%-15s= 2" % "Reserved")
    print("%-15s= 0" % "Interleave")
    print("%-15s= 0" % "LowCyl")
    print("%-15s= %d" % ("HighCyl", high))
    print("%-15s= 30" % "Buffers")
    print("%-15s= 0" % "BufMemType")
    print("%-15s= 0x1fe00" % "MaxTransfer")
    print("%-15s= 0x7ffffffe" % "Mask")
    print("/* An OFS-formatted HDF mounts with the ROM filesystem as-is. For an")
    print("   FFS image, also add:  FileSystem = L:FastFileSystem")
    print("                         DosType    = 0x444F5301")
    print("                         GlobVec    = -1                            */")
    print("#")

def main():
    a = sys.argv[1:]
    if not a or a[0] in ("-h", "--help"):
        print(__doc__); return
    if a[0] == "--size":
        if len(a) < 3: sys.exit("usage: mkhdfmount.py --size <bytes> <unit> [MountName]")
        size = int(a[1], 0); unit = int(a[2]); name = a[3] if len(a) > 3 else ("HDF%d" % unit)
    else:
        if len(a) < 2: sys.exit("usage: mkhdfmount.py <hdf-file> <unit> [MountName]")
        size = os.path.getsize(a[0]); unit = int(a[1]); name = a[2] if len(a) > 2 else ("HDF%d" % unit)
    if not 0 <= unit <= 3:
        sys.exit("error: unit must be 0..3")
    emit(name, unit, size)

if __name__ == "__main__":
    main()
