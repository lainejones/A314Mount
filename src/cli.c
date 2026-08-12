/*
 * A314Mount - command line front end
 *
 *   A314Mount INSERT [DRIVE n] <image> [RW] [HOST h] [PORT p] [SHOW]
 *   A314Mount EJECT  [DRIVE n]         [HOST h] [PORT p] [SHOW]
 *
 *   INSERT/EJECT  which operation (pick one).
 *   DRIVE         virtual unit, 0 = PD0: (default) .. 3 = PD3:.
 *   <image>       Pi-side path of the ADF or HDF to insert (e.g.
 *                 /home/pi/wb.adf or /home/pi/work.hdf).
 *   RW            mount writable (default is read-only; usually wanted for HDF).
 *   HOST/PORT     daemon target (default 127.0.0.1 : 23890).
 *   SHOW          print the command that would be sent, don't send it.
 */

#include <proto/exec.h>
#include <proto/dos.h>
#include <dos/dos.h>
#include <string.h>

#include "a314disk.h"

unsigned long __stack = 16000;

static const char verstag[] __attribute__((used)) =
    "$VER: A314Mount 0.1 (07.06.2026)";

#define TEMPLATE "INSERT/S,EJECT/S,DRIVE/N/K,IMAGE,RW/S,HOST/K,PORT/N/K,SHOW/S"
enum { ARG_INSERT, ARG_EJECT, ARG_DRIVE, ARG_ADF, ARG_RW,
       ARG_HOST, ARG_PORT, ARG_SHOW, ARG_COUNT };

int main(void)
{
    struct RDArgs *rd;
    LONG args[ARG_COUNT];
    int  rc = 0;

    memset(args, 0, sizeof args);
    rd = ReadArgs((STRPTR)TEMPLATE, args, NULL);
    if (!rd) { PrintFault(IoErr(), "A314Mount"); return 20; }

    {
        BOOL doInsert = args[ARG_INSERT] ? TRUE : FALSE;
        BOOL doEject  = args[ARG_EJECT]  ? TRUE : FALSE;
        int  drive    = args[ARG_DRIVE] ? (int)*(LONG *)args[ARG_DRIVE] : 0;
        CONST_STRPTR adf  = (CONST_STRPTR)args[ARG_ADF];
        BOOL rw       = args[ARG_RW] ? TRUE : FALSE;
        CONST_STRPTR host = args[ARG_HOST] ? (CONST_STRPTR)args[ARG_HOST]
                                           : (CONST_STRPTR)A314_DEF_HOST;
        UWORD port    = args[ARG_PORT] ? (UWORD)*(LONG *)args[ARG_PORT]
                                       : A314_DEF_PORT;
        char cmd[300];

        if (doInsert == doEject) {
            Printf("A314Mount: specify exactly one of INSERT or EJECT\n");
            FreeArgs(rd); return 20;
        }
        if (drive < 0 || drive > 3) {
            Printf("A314Mount: DRIVE must be 0..3 (PD0:..PD3:)\n");
            FreeArgs(rd); return 20;
        }
        if (doInsert && (!adf || adf[0] == '\0')) {
            Printf("A314Mount: INSERT needs an image path (ADF or HDF)\n");
            FreeArgs(rd); return 20;
        }

        if (doInsert) a314BuildInsert(cmd, (int)sizeof cmd, drive, adf, rw, TRUE);
        else          a314BuildEject (cmd, (int)sizeof cmd, drive);

        if (args[ARG_SHOW]) {
            Printf("would send to %s:%ld  ->  %s\n", (LONG)host, (LONG)port, (LONG)cmd);
        } else {
            char reply[160];
            LONG r = doInsert
                   ? a314Insert(host, port, drive, adf, rw, reply, (int)sizeof reply)
                   : a314SendCommand(host, port, cmd, reply, (int)sizeof reply);
            if (r == A314_OK) {
                Printf("PD%ld: %s\n", (LONG)drive, (LONG)(doInsert ? "inserted" : "ejected"));
                if (reply[0]) Printf("  daemon: %s\n", (LONG)reply);
            } else {
                Printf("A314Mount: %s\n", (LONG)a314ErrText(r));
                rc = 20;
            }
        }
    }

    FreeArgs(rd);
    return rc;
}
