#ifndef A314DISK_H
#define A314DISK_H

/*
 * A314Mount - talk to the A314 floppy-emulation daemon.
 *
 * The a314 disk emulator on the Raspberry Pi exposes a tiny line-based TCP
 * server (default localhost:23890). Commands (one line each, LF-terminated):
 *
 *     insert <drive> <pi-path>        mount an ADF read-only into PDx:
 *     insert -rw <drive> <pi-path>    mount writable
 *     eject <drive>                   unmount
 *
 * We reach it from the Amiga over bsdsocket.library (a314bsd provides one, so
 * socket ops run Pi-side and "127.0.0.1" is the Pi itself).
 */

#include <exec/types.h>

#define A314_DEF_HOST "127.0.0.1"
#define A314_DEF_PORT 23890

/* result codes */
enum {
    A314_OK       =  0,
    A314_ERR_LIB  = -1,   /* bsdsocket.library not available (TCP stack up?) */
    A314_ERR_SOCK = -2,   /* socket() failed                                 */
    A314_ERR_HOST = -3,   /* host could not be resolved                      */
    A314_ERR_CONN = -4,   /* connect() failed (daemon not listening?)        */
    A314_ERR_SEND = -5    /* send() failed                                   */
};

CONST_STRPTR a314ErrText(LONG code);

/* Build a command line (no trailing newline) into buf. drive is 0/1.
 * unitFirst TRUE  -> "insert <unit> [-rw] <path>"  (a314 1.2.3 disk.py)
 * unitFirst FALSE -> "insert [-rw] <unit> <path>"  (Niklas's note)   */
void a314BuildInsert(char *buf, int bufsize, int drive, CONST_STRPTR adfPath,
                     BOOL rw, BOOL unitFirst);
void a314BuildEject(char *buf, int bufsize, int drive);

/* Open bsdsocket, connect to host:port, send cmd + '\n', optionally read a
 * bounded reply into reply[] (NUL-terminated; may be NULL), then close.
 * Returns an A314_* code. */
LONG a314SendCommand(CONST_STRPTR host, UWORD port, CONST_STRPTR cmd,
                     char *reply, int replysize);

/* Insert with automatic argument-order fallback: tries "unit first", and if
 * the daemon replies with a parse error, retries the other order. */
LONG a314Insert(CONST_STRPTR host, UWORD port, int drive, CONST_STRPTR adfPath,
                BOOL rw, char *reply, int replysize);

#endif /* A314DISK_H */
