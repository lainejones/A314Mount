# A314Mount

Mount and unmount **ADF** (floppy) and **HDF** (hard-disk) images in the
**A314**'s emulated drives (`PD0:`–`PD3:`) **from the Amiga**.

Swapping a virtual disk on the [A314](https://github.com/niklasekstrom/a314)
normally means getting to the Raspberry Pi and hand-typing at its disk daemon:

```
echo insert 0 -rw /home/pi/Workbench.adf | nc localhost 23890
```

A314Mount does it from the Amiga instead — from a Shell, or from Workbench with
a file browser that lists the Pi's own directories:

```
A314Mount INSERT DRIVE 1 /home/pi/games.adf RW
A314Mount EJECT  DRIVE 1
```

It **modifies nothing on the Pi** — no patched daemon, no extra service. It
talks to the stock a314 disk daemon over `bsdsocket.library`, and the GUI's
browser lists Pi directories by running the stock `pi` command.

Ships as a CLI (`A314Mount`) and a GadTools GUI (`A314MountGUI`), built with
amiga-gcc — pure NDK, no MUI/ReAction. Verified on a real A1200 (OS 3.2).

## How it works

The a314 disk service on the Pi (see Niklas Ekström's
[`Software/disk`](https://github.com/niklasekstrom/a314/tree/main/Software/disk))
runs a tiny line-based TCP server (default `localhost:23890`) that understands
(unit first):

    insert <drive> <pi-path>        mount an image read-only
    insert <drive> -rw <pi-path>    mount writable
    eject  <drive>                  unmount

`<drive>` is `0`–`3` (`PD0:`–`PD3:`). The image can be an **ADF** (exactly
901120 bytes → floppy geometry) or an **HDF** (≥1 MB, a multiple of 16 kB → hard-
disk geometry). The service infers geometry from the file size; see **ADF vs
HDF** below.

A314Mount reaches it over **`bsdsocket.library`** (provided by **a314bsd**, so
socket ops run Pi-side and `127.0.0.1` *is* the Pi). It opens a socket, sends
one line, and reports the result. **Verified end-to-end on a real A1200** (CLI
and GUI both insert/eject `PD0:`/`PD1:`).

## ADF vs HDF

Both go into the same `PD0:`–`PD3:` drives via the same `INSERT`/`EJECT` — the
only difference is the **mount-list geometry** the drive was created with:

* **ADF** — all floppies are 901120 bytes, so the stock `PD0:`–`PD3:`
  descriptors (`install/PD0`…`PD3`, floppy geometry) work for any ADF. Nothing
  extra to do.
* **HDF** — geometry is per-image (`Surfaces=1, BlocksPerTrack=32,
  HighCyl = size/16384 − 1`), and the Amiga mount entry **must match the image**
  or the filesystem reads the wrong blocks. Generate a correct entry with the
  bundled helper (runs on the Pi or host, pure Python):

      python3 install/mkhdfmount.py /home/pi/work.hdf 2 WORK   > WORK
      # or, without the file handy:  mkhdfmount.py --size 4194304 2 WORK

  Copy the result to `DEVS:DOSDrivers/WORK` on the Amiga, `Mount WORK:`, then
  `A314Mount INSERT DRIVE 2 /home/pi/work.hdf RW`. Use a drive **unit not already
  claimed by a floppy** — a unit's geometry locks to the first image inserted
  after each reboot. HDFs are almost always mounted `RW`. If the HDF is
  FFS-formatted, add the `FileSystem`/`DosType` lines the helper notes.

## Requirements

A314Mount **modifies nothing on the Pi** — it uses only stock a314 components.
But it isn't a bare drop-in either: the A1200 must already have a normal, working
a314 setup. On that machine you need:

* **a314bsd** — provides a `bsdsocket.library` where `127.0.0.1` routes to the
  Pi. A314Mount reaches the disk daemon through it; a plain TCP stack won't do,
  because the daemon lives on the Pi's own localhost.
* The a314 **disk driver**: `a314disk.device` in `DEVS:`, and the `PD0:`–`PD3:`
  drives mounted (the `Install` script adds these mount-list entries). The disk
  daemon on `:23890` is *on-demand* — it only starts once the Amiga **opens** a
  drive, so a drive must exist and have been accessed at least once (`Mount PD0:`
  then e.g. `dir PD0:`). If it isn't up, A314Mount reports *"Cannot reach the
  disk daemon"*.
* The **`pi` command** (`C:pi`) + the **picmd** service — **only** for the GUI's
  directory browser; insert/eject don't need it. Both are standard a314.
* AmigaOS **3.0+** (v37 `intuition`/`gadtools`/`graphics`).

None of this is patched or custom — it's what a full a314 install already
provides. **To deploy A314Mount itself:** copy `A314Mount` (CLI) and
`A314MountGUI` + `A314MountGUI.info` (see **Install**). No Pi-side changes of
any kind.

## Layout

    src/a314disk.c / .h   bsdsocket engine + command builders
    src/cli.c             command-line front end
    src/gui.c             GadTools GUI front end
    build.sh              build both with amiga-gcc
    tools/mkpkg.py        stage the release drawer in out/release/
    tools/mock_daemon.py  a safe stand-in daemon for testing
    install/Install       AmigaDOS installer (Execute Install)
    install/Install.info  double-click icon (WBPROJECT, DefaultTool IconX)
    install/PD0..PD3      DEVS:DOSDrivers descriptors for the floppy drives
    install/mkhdfmount.py generate a matching-geometry mount entry for an HDF
    install/A314Mount.guide       AmigaGuide manual (open with MultiView)
    install/A314Mount.guide.info  document icon (WBPROJECT, DefaultTool MultiView)
    install/A314Mount.info        drawer icon for the whole package (WBDRAWER)
    install/A314MountGUI.info     the GUI's tool icon (WBTOOL)

## Documentation

On the Amiga, double-click **`A314Mount.guide`** (its icon opens it via
`SYS:Utilities/MultiView`) for the full manual — install, CLI, GUI, ADF vs HDF,
and troubleshooting, all cross-linked. This README mirrors the same material.

The `.info` icons were generated with the author's own icon tools (not part of
this repo) and are committed ready-made in `install/`: the floppy GUI icon, the
green install arrow, the package drawer, and the guide's document icon.

## Install

The release archive unpacks to an `A314Mount` drawer (with `A314Mount.info`
beside it) holding `A314Mount`, `A314MountGUI` + `A314MountGUI.info`,
`Install` + `Install.info`, `A314Mount.guide` + `.info`, `PD0`–`PD3`,
`mkhdfmount.py`, `README.md` and `LICENSE`. Copy the drawer onto the Amiga
(AmigaOS 3.0+). Use the **`.lha`** — a `.zip` can't carry AmigaDOS protection
bits, so after unpacking a `.zip` run `protect A314Mount +e` and
`protect A314MountGUI +e` in the drawer (the installer also sets `+e` on what it
copies). Then either:

* **Double-click `Install`** from Workbench — its icon (`Install.info`) is a
  WBPROJECT whose *Default Tool* is `IconX`, so Workbench runs the script and
  installs to `SYS:Utilities`; or
* **From a Shell** in the drawer:

      Execute Install                 ; GUI -> SYS:Utilities, CLI -> C:
      Execute Install SYS:Tools       ; ...or name your own drawer (Shell only)

The installer drops **`A314MountGUI` + its icon** into the chosen drawer so you
can double-click it from Workbench, copies the **`A314Mount`** CLI to `C:`, and
— if `DEVS:a314disk.device` is present — installs the `PD0:`–`PD3:` drive
descriptors to `DEVS:DOSDrivers/`. It stops without installing anything on
an OS older than 3.0. After that, `Mount PD0:` (or a reboot) brings
the drive up; the first access to it starts the Pi daemon.

To install by hand instead: copy `A314MountGUI` and `A314MountGUI.info` together
into any drawer (keep the `.info` next to the executable — that's the icon
Workbench needs to launch it), and copy `A314Mount` to `C:`.

To rebuild the release drawer from source: `sh build.sh` (in WSL), then
`python3 tools/mkpkg.py` — it stages `out/release/A314Mount/` and
`out/release/A314Mount.info`.

## Build

    wsl -e bash -lc 'cd /mnt/c/projects/A314Mount && sh build.sh'

Note: build.sh strips at link time (`gcc -s`). Do **not** post-strip with
`m68k-amigaos-strip` — it corrupts the hunk relocations and the binary crashes
at startup.

## CLI usage

    A314Mount INSERT [DRIVE n] <adf> [RW] [HOST h] [PORT p] [SHOW]
    A314Mount EJECT  [DRIVE n]        [HOST h] [PORT p] [SHOW]

* `INSERT` / `EJECT` — pick one.
* `DRIVE` — `0` = `PD0:` (default) … `3` = `PD3:`.
* `<adf>` — **Pi-side** path of the ADF or HDF (e.g. `/home/pi/Workbench.adf`).
* `RW` — mount writable (default is read-only).
* `HOST` / `PORT` — daemon target (default `127.0.0.1` / `23890`). HOST must be
  a numeric IP.
* `SHOW` — print the command that *would* be sent, don't send it (dry run).

Examples

    A314Mount INSERT /home/pi/Workbench.adf          ; PD0:, read-only
    A314Mount INSERT DRIVE 1 /home/pi/games.adf RW   ; PD1:, writable
    A314Mount EJECT                                  ; eject PD0:
    A314Mount EJECT DRIVE 1

## GUI usage

**Double-click `A314MountGUI`** from Workbench (its `.info` icon is the green-
badged floppy), or run it from a Shell. Pick the **Drive** (PD0:–PD3:), set the
**Image**, tick **Read-write** if needed, and click **Insert** or **Eject**.
**Host** / **Port** default to `127.0.0.1` / `23890`. The status line shows the
result (e.g. `PD0: inserted`). The window is **resizable** (drag the size
gadget) — it stretches horizontally and the string fields grow with it.

Set the **Image** two ways: type the Pi-side path, **or click the folder button**
at the end of the Image row (clicking Insert with an empty field opens it too).
That opens a **built-in Pi directory browser** and shows the directory in a
scrolling list (subdirectories first with a trailing `/`, then `.adf`/`.hdf`/
`.adz` files). Select an entry and **Open** to descend into a directory or pick a
file; **Parent** goes up. Picking a file drops its **absolute Pi path** straight
into the Image field — it can reach **anywhere on the Pi** you can read, not just
the a314 share. It starts at **`A314_BROWSE_ROOT`** — a `#define` at the top of
`src/gui.c`; **change it to wherever you keep your images** (it ships as
`/home`) and rebuild. Wherever it starts, **Parent**
walks up, so nothing is out of reach.

> **Fully standalone — no a314 software is modified.** The browser lists Pi
> directories by running the stock **`pi`** command (`C:pi ls -1Ap <dir>`, the
> a314 `picmd` service) and parsing its output, so it needs only what a normal
> a314 install already has (`pi` in `C:` + the `picmd` service). `pi` refuses a
> non-interactive stdin, so the browser hands it an `AUTO` console handle (which
> stays hidden). No disk-daemon patch is required for anything.

The icon is a dual-format `.info` (classic 4-colour planar + OS 3.5 colour),
committed as `install/A314MountGUI.info`.

## Testing

* **Command construction** is verified on the host under vamos with `SHOW`
  (no socket needed):

      wsl -e bash -lc 'cd /mnt/c/projects/A314Mount && ~/amitools-venv/bin/vamos out/A314Mount INSERT /home/pi/wb.adf SHOW'

* **The live socket** needs the real A1200 + a314bsd. To test safely first, run
  `tools/mock_daemon.py <port>` on the Pi (a spare port), point A314Mount at that
  HOST/PORT, and watch it log the exact command — no real floppy swaps.

## Notes (confirmed on the real rig)

* **Argument order is unit-first** (`insert <unit> [-rw] <path>`); the engine
  auto-retries the `-rw`-first form if a daemon ever rejects it.
* **`127.0.0.1` is the Pi** — a314bsd runs socket ops Pi-side, so localhost is
  correct and no LAN IP is needed.
* The disk daemon has **no directory-listing command**, which is why the GUI's
  browser shells out to the stock `pi` command instead of asking the daemon —
  and why A314Mount needs no changes to any a314 software.
* A314Mount prints `PDn: inserted/ejected` but usually *not* the daemon's reply
  text: a314bsd doesn't honour `SO_RCVTIMEO`, so the engine (by design, to never
  hang) skips the blocking read. The operation still succeeds.

## Possible future work

* Read icon **ToolTypes** (HOST/PORT/DRIVE/ADF) so the Workbench icon can carry
  defaults instead of typing them each launch.
* Expose the disk daemon as a native `a314.device` service to drop the
  `bsdsocket` dependency entirely.
