/*
 * A314Mount - bsdsocket engine (see a314disk.h)
 *
 * Uses <inline/bsdsocket.h> (Roadshow) for the socket calls; it pulls in the
 * struct headers (netinet/in.h, sys/socket.h, devices/timer.h). We deliberately
 * do NOT use inet_addr/gethostbyname/htons (their prototypes in the NDK
 * arpa/inet.h + netdb.h clash with the bsdsocket API and won't compile): the
 * host must be a dotted-quad IP, which we parse ourselves, and on big-endian
 * m68k network byte order is native so no htons() is needed.
 */

#include <proto/exec.h>
#include <inline/bsdsocket.h>
#include <string.h>

#include "a314disk.h"

struct Library *SocketBase = NULL;   /* used by the inline/bsdsocket.h macros */

/* ------------------------------------------------------------------ */
/* command builders + small helpers (no sprintf under -noixemul)      */
/* ------------------------------------------------------------------ */

static int appendStr(char *buf, int pos, int bufsize, CONST_STRPTR s)
{
    while (*s && pos < bufsize - 1) buf[pos++] = *s++;
    buf[pos] = '\0';
    return pos;
}

static int appendNum(char *buf, int pos, int bufsize, int v)
{
    char tmp[12];
    int i = 0, j;
    if (v == 0) tmp[i++] = '0';
    while (v > 0) { tmp[i++] = (char)('0' + (v % 10)); v /= 10; }
    for (j = i - 1; j >= 0 && pos < bufsize - 1; j--) buf[pos++] = tmp[j];
    buf[pos] = '\0';
    return pos;
}

void a314BuildInsert(char *buf, int bufsize, int drive, CONST_STRPTR adfPath,
                     BOOL rw, BOOL unitFirst)
{
    int p = appendStr(buf, 0, bufsize, (CONST_STRPTR)"insert ");
    if (unitFirst) {                              /* insert <unit> [-rw] <path> */
        p = appendNum(buf, p, bufsize, drive);
        p = appendStr(buf, p, bufsize, (CONST_STRPTR)" ");
        if (rw) p = appendStr(buf, p, bufsize, (CONST_STRPTR)"-rw ");
    } else {                                       /* insert [-rw] <unit> <path> */
        if (rw) p = appendStr(buf, p, bufsize, (CONST_STRPTR)"-rw ");
        p = appendNum(buf, p, bufsize, drive);
        p = appendStr(buf, p, bufsize, (CONST_STRPTR)" ");
    }
    appendStr(buf, p, bufsize, adfPath);
}

void a314BuildEject(char *buf, int bufsize, int drive)
{
    int p = appendStr(buf, 0, bufsize, (CONST_STRPTR)"eject ");
    appendNum(buf, p, bufsize, drive);
}

CONST_STRPTR a314ErrText(LONG code)
{
    switch (code) {
    case A314_OK:       return (CONST_STRPTR)"OK";
    case A314_ERR_LIB:  return (CONST_STRPTR)"No bsdsocket.library (is the TCP stack / a314bsd running?)";
    case A314_ERR_SOCK: return (CONST_STRPTR)"Could not create a socket";
    case A314_ERR_HOST: return (CONST_STRPTR)"Host must be a numeric IP (e.g. 127.0.0.1)";
    case A314_ERR_CONN: return (CONST_STRPTR)"Cannot reach the disk daemon (not listening?)";
    case A314_ERR_SEND: return (CONST_STRPTR)"Send failed";
    default:            return (CONST_STRPTR)"Unknown error";
    }
}

/* dotted-quad "a.b.c.d" -> 32-bit big-endian (== network order on m68k).
 * Returns 0 on any parse error (0.0.0.0 is not a valid target either). */
static ULONG parseIP(CONST_STRPTR s)
{
    ULONG oct[4] = {0,0,0,0};
    int part = 0, digits = 0;
    for (; *s; s++) {
        if (*s >= '0' && *s <= '9') {
            oct[part] = oct[part] * 10 + (ULONG)(*s - '0');
            if (oct[part] > 255) return 0;
            digits++;
        } else if (*s == '.') {
            if (!digits || part >= 3) return 0;
            part++; digits = 0;
        } else {
            return 0;
        }
    }
    if (part != 3 || !digits) return 0;
    return (oct[0] << 24) | (oct[1] << 16) | (oct[2] << 8) | oct[3];
}

/* ------------------------------------------------------------------ */

LONG a314SendCommand(CONST_STRPTR host, UWORD port, CONST_STRPTR cmd,
                     char *reply, int replysize)
{
    struct sockaddr_in sa;
    ULONG addr;
    int sock;
    LONG rc = A314_OK;

    if (reply && replysize > 0) reply[0] = '\0';

    addr = parseIP(host);
    if (addr == 0) return A314_ERR_HOST;

    SocketBase = OpenLibrary((STRPTR)"bsdsocket.library", 4);
    if (!SocketBase) return A314_ERR_LIB;

    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) { rc = A314_ERR_SOCK; goto out_lib; }

    memset(&sa, 0, sizeof sa);
    sa.sin_family = AF_INET;
    sa.sin_port   = port;                 /* big-endian native on m68k */
    sa.sin_addr.s_addr = addr;

    if (connect(sock, (struct sockaddr *)&sa, sizeof sa) < 0) { rc = A314_ERR_CONN; goto out_sock; }

    {
        int len = (int)strlen((const char *)cmd);
        if (send(sock, (APTR)cmd, len, 0) < 0 || send(sock, (APTR)"\n", 1, 0) < 0) {
            rc = A314_ERR_SEND; goto out_sock;
        }
    }

    /* best-effort bounded reply: only read if the recv timeout is honoured,
     * so we never hang if the daemon keeps the connection silent+open. */
    if (reply && replysize > 0) {
        struct timeval tv;
        tv.tv_secs = 2; tv.tv_micro = 0;
        if (setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (APTR)&tv, sizeof tv) == 0) {
            LONG n = recv(sock, reply, replysize - 1, 0);
            if (n < 0) n = 0;
            reply[n] = '\0';
            while (n > 0 && (reply[n-1] == '\n' || reply[n-1] == '\r')) reply[--n] = '\0';
        }
    }

out_sock:
    CloseSocket(sock);
out_lib:
    CloseLibrary(SocketBase);
    SocketBase = NULL;
    return rc;
}

/* TRUE if the daemon reply looks like it couldn't parse the command
 * (a314 disk.py answers "Exception raised..." or "Unknown command '...'"). */
static BOOL replyParseError(const char *r)
{
    const char *needles[2]; int i;
    needles[0] = "Exception"; needles[1] = "Unknown";
    for (i = 0; i < 2; i++) {
        const char *h = r, *n;
        while (*h) {
            const char *hh = h; n = needles[i];
            while (*n && *hh == *n) { hh++; n++; }
            if (!*n) return TRUE;
            h++;
        }
    }
    return FALSE;
}

LONG a314Insert(CONST_STRPTR host, UWORD port, int drive, CONST_STRPTR adfPath,
                BOOL rw, char *reply, int replysize)
{
    char cmd[300];
    LONG r;

    a314BuildInsert(cmd, (int)sizeof cmd, drive, adfPath, rw, TRUE);   /* unit first */
    r = a314SendCommand(host, port, cmd, reply, replysize);

    /* if it connected but the daemon couldn't parse the order, try the other */
    if (r == A314_OK && reply && replyParseError(reply)) {
        a314BuildInsert(cmd, (int)sizeof cmd, drive, adfPath, rw, FALSE);
        r = a314SendCommand(host, port, cmd, reply, replysize);
    }
    return r;
}
