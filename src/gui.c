/*
 * A314Mount - GadTools GUI front end (AmigaOS 3.x, no external deps)
 *
 *  +------------------------------------------------+
 *  |  Drive  [ PD0: v]        Read-write [x]         |
 *  |  Image  [/home/pi/Workbench.adf____________] [] |
 *  |  [ Insert ] [ Eject ]  Ready                    |
 *  |  Host [127.0.0.1______]  Port [23890__]        |
 *  +------------------------------------------------+
 *
 * Mounts an ADF (floppy) or HDF (hard-disk) image on one of the a314 virtual
 * drives PD0:-PD3:. Insert/Eject send one line to the a314 disk daemon over
 * bsdsocket (a314bsd), then report the result in the status line.
 *
 * Menus (right mouse button):
 *   Project:  About...  |  Quit (A-Q)
 *   Disk:     Insert (A-I), Eject (A-E), Choose image... (A-O)  |
 *             Drive PD0:..PD3: (A-0..A-3)  |  Read-write (A-W)
 *   Settings: Connection...  Drive and browser...  |
 *             Snapshot window, Unsnapshot window
 *
 * ---- settings ------------------------------------------------------------
 * Where each value comes from, lowest first:
 *   1. the built-in defaults (setsDefault): drive PD0:, read-only, host
 *      127.0.0.1, port 23890, browse from /home, the default window;
 *   2. the settings file A314Mount.prefs: ENV:A314Mount.prefs, else
 *      ENVARC:A314Mount.prefs. A settings page's Save writes both (kept),
 *      Use writes ENV: only (lasts until the next reboot). Plain text, one
 *      KEY=value a line:
 *        HOST=127.0.0.1   PORT=23890   DRIVE=0..3   RW=0|1   BROWSE=/home
 *        WINDOW=x,y,w,h   (Settings > Snapshot window; none = default place)
 * A314MountGUI reads no tooltypes, so there are none for the file to
 * override; should tooltypes be added, the file is applied on top of them.
 * The main window's fields start from these values; changing a field in the
 * window does not change the settings.
 */

#include <exec/types.h>
#include <exec/lists.h>
#include <exec/nodes.h>
#include <exec/memory.h>
#include <intuition/intuition.h>
#include <libraries/gadtools.h>
#include <graphics/text.h>
#include <dos/dos.h>
#include <dos/dostags.h>

#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <proto/gadtools.h>

#include <string.h>

#include "a314disk.h"
#include "amigui.h"

#define GUI_NAME    "A314Mount"
#define GUI_VERSION "0.2"
#define PREFS_NAME  "A314Mount.prefs"

/* The custom Pi browser starts here unless BROWSE= says otherwise (Settings >
 * Drive and browser). Unlike an ASL requester (which only sees Amiga volumes),
 * it asks the Pi to list real Pi directories, so it returns absolute Pi paths. */
#define DEF_BROWSE  "/home"
#define DEF_PORT    "23890"

#define DEF_WIN_W   440      /* a classic window: never wider than MAX_WIN_W */
#define MAX_WIN_W   600

unsigned long __stack = 16000;

static const char verstag[] __attribute__((used)) =
    "$VER: A314MountGUI " GUI_VERSION " (30.10.2026)";

struct IntuitionBase *IntuitionBase = NULL;
struct GfxBase       *GfxBase       = NULL;
struct Library       *GadToolsBase  = NULL;

static struct TextAttr topaz8 = { (STRPTR)"topaz.font", 8, FS_NORMAL, FPF_ROMFONT };

/* ---- settings ------------------------------------------------------------ */

struct Settings {
    char host[64];
    char port[8];
    WORD drive;                   /* 0..3 = PD0:..PD3: */
    BOOL rw;
    char browse[256];             /* the Pi directory the browser starts in */
    LONG winx, winy, winw, winh;  /* the snapshot; winw 0 = none */
};
static struct Settings g_set;

static void setStr(char *dst, int size, const char *v)
{
    strncpy(dst, v, size - 1); dst[size - 1] = '\0';
}

/* a port number 1..65535, else 0 */
static UWORD portOf(const char *s)
{
    LONG v = 0;
    if (!s || !*s) return 0;
    while (*s >= '0' && *s <= '9') { v = v * 10 + (*s++ - '0'); if (v > 65535) return 0; }
    return (UWORD)(*s ? 0 : v);
}

static LONG getNum(char **pp)
{
    char *p = *pp; LONG v = 0; int neg = 0;
    while (*p == ' ') p++;
    if (*p == '-') { neg = 1; p++; }
    while (*p >= '0' && *p <= '9') v = v * 10 + (*p++ - '0');
    *pp = p;
    return neg ? -v : v;
}

static char *putNum(char *p, LONG v)
{
    char t[12]; int i = 0;
    if (v < 0) { *p++ = '-'; v = -v; }
    do { t[i++] = (char)('0' + v % 10); v /= 10; } while (v);
    while (i) *p++ = t[--i];
    *p = '\0';
    return p;
}

static void setsDefault(struct Settings *s)
{
    memset(s, 0, sizeof *s);
    strcpy(s->host, A314_DEF_HOST);
    strcpy(s->port, DEF_PORT);
    strcpy(s->browse, DEF_BROWSE);
}

static void setsLoad(struct Settings *s)
{
    char buf[800], *line, *next;
    setsDefault(s);
    if (amg_env_read(PREFS_NAME, buf, sizeof buf) < 0) return;
    for (line = buf; line && *line; line = next) {
        char *v;
        if ((next = strchr(line, '\n'))) *next++ = '\0';
        if ((v = strchr(line, '\r'))) *v = '\0';
        if (!(v = strchr(line, '='))) continue;
        *v++ = '\0';
        if      (!strcmp(line, "HOST"))   { if (*v) setStr(s->host, sizeof s->host, v); }
        else if (!strcmp(line, "PORT"))   { if (portOf(v)) setStr(s->port, sizeof s->port, v); }
        else if (!strcmp(line, "DRIVE"))  { if (v[0] >= '0' && v[0] <= '3' && !v[1]) s->drive = (WORD)(v[0] - '0'); }
        else if (!strcmp(line, "RW"))     s->rw = (v[0] == '1') ? TRUE : FALSE;
        else if (!strcmp(line, "BROWSE")) { if (v[0] == '/') setStr(s->browse, sizeof s->browse, v); }
        else if (!strcmp(line, "WINDOW")) {
            LONG n[4]; int k;
            for (k = 0; k < 4 && *v; k++) {
                n[k] = getNum(&v);
                if (*v == ',') v++;
            }
            if (k == 4 && n[2] > 0 && n[3] > 0) {
                s->winx = n[0]; s->winy = n[1]; s->winw = n[2]; s->winh = n[3];
            }
        }
    }
}

static void addLine(char *buf, const char *key, const char *val)
{
    strcat(buf, key); strcat(buf, "="); strcat(buf, val); strcat(buf, "\n");
}

/* keep = Save (ENV: + ENVARC:), else Use (ENV: only) */
static BOOL setsWrite(const struct Settings *s, BOOL keep)
{
    char buf[800], n[48], *p;
    buf[0] = '\0';
    addLine(buf, "HOST", s->host);
    addLine(buf, "PORT", s->port);
    n[0] = (char)('0' + s->drive); n[1] = '\0';
    addLine(buf, "DRIVE", n);
    addLine(buf, "RW", s->rw ? "1" : "0");
    addLine(buf, "BROWSE", s->browse);
    if (s->winw > 0) {
        p = putNum(n, s->winx); *p++ = ',';
        p = putNum(p, s->winy); *p++ = ',';
        p = putNum(p, s->winw); *p++ = ',';
        putNum(p, s->winh);
        addLine(buf, "WINDOW", n);
    }
    return amg_env_write(PREFS_NAME, buf, strlen(buf), keep) ? TRUE : FALSE;
}

/* ---- the main window ----------------------------------------------------- */

enum { GAD_DRIVE = 1, GAD_RW, GAD_ADF, GAD_INSERT, GAD_EJECT, GAD_STATUS,
       GAD_HOST, GAD_PORT, GAD_BROWSE };

struct Gui {
    struct Screen *scr;
    APTR           vi;
    struct Window *win;
    struct Gadget *glist;
    struct Gadget *gDrive, *gRW, *gAdf, *gInsert, *gEject, *gStatus, *gHost, *gPort;
    struct Menu   *menu;
    BOOL           strip;     /* the menu strip is set on win */

    struct TextAttr *ta;      /* the gadget font: the screen's, or Topaz 8 if too big */
    struct TextFont *font;
    struct RastPort  mrp;     /* for measuring text in that font */
    WORD  lab, cyw, bw, pw;   /* label column, cycle, button and port widths */
    WORD  minIn, innerH;      /* the narrowest inner width; the inner height */

    struct Requester blk;     /* blocks the main window while a page is open */
    BOOL  blocked;

    char  status[80];
    char  saveAdf[210];
    char  saveHost[64];
    char  savePort[16];
    WORD  drive;          /* 0 = PD0: .. 3 = PD3: */
    BOOL  rw;
    WORD  prevW;          /* last window width, to ignore no-op NEWSIZE */
};

static STRPTR driveLabels[] = { (STRPTR)"PD0:", (STRPTR)"PD1:",
                                (STRPTR)"PD2:", (STRPTR)"PD3:", NULL };

/* A small 24x11 folder image for the "browse" button on the Image row.
 * (planar, depth 2 - generated by tools; see project notes) */
#define FOLDER_W 24
#define FOLDER_H 11
static UWORD folderImageData[] = {
    0x0000, 0x0100, 0x0000, 0x0100, 0x0fc0, 0x0100, 0x0fc0, 0x0100,
    0x1fff, 0xf900, 0x1fff, 0xf900, 0x1fff, 0xf900, 0x1fff, 0xf900,
    0x1fff, 0xf900, 0x1fff, 0xf900, 0x7fff, 0xff00, 0xffff, 0xfe00,
    0x8000, 0x0000, 0x8000, 0x0000, 0x8fc0, 0x0000, 0x8000, 0x0000,
    0x8fff, 0xf000, 0x8fff, 0xf000, 0x8fff, 0xf000, 0x8fff, 0xf000,
    0x8000, 0x0000, 0x8000, 0x0000,
};
static struct Image folderImage = {
    0, 0, FOLDER_W, FOLDER_H, 2, folderImageData, 0x3, 0x0, NULL
};
static struct Gadget browseGad = {
    NULL, 0, 0, FOLDER_W, FOLDER_H,
    GFLG_GADGIMAGE | GFLG_GADGHCOMP, GACT_RELVERIFY, GTYP_BOOLGADGET,
    (APTR)&folderImage, NULL, NULL, 0, NULL, GAD_BROWSE, NULL
};

/* ---- menus ---------------------------------------------------------------- */

enum { MN_ABOUT = 1, MN_QUIT, MN_INSERT, MN_EJECT, MN_BROWSE,
       MN_PD0, MN_PD1, MN_PD2, MN_PD3, MN_RW,
       MN_SET_CONN, MN_SET_DRIVE, MN_SNAPSHOT, MN_UNSNAPSHOT };

/* Disk menu item positions (bars count): Insert 0, Eject 1, Choose 2, bar 3,
 * PD0: 4 .. PD3: 7 - each drive excludes the other three. */
static struct NewMenu amiNewMenu[] = {
    { NM_TITLE, (STRPTR)"Project",              NULL,        0, 0, NULL },
    { NM_ITEM,  (STRPTR)"About...",             NULL,        0, 0, (APTR)MN_ABOUT },
    { NM_ITEM,  NM_BARLABEL,                    NULL,        0, 0, NULL },
    { NM_ITEM,  (STRPTR)"Quit",                 (STRPTR)"Q", 0, 0, (APTR)MN_QUIT },

    { NM_TITLE, (STRPTR)"Disk",                 NULL,        0, 0, NULL },
    { NM_ITEM,  (STRPTR)"Insert",               (STRPTR)"I", 0, 0, (APTR)MN_INSERT },
    { NM_ITEM,  (STRPTR)"Eject",                (STRPTR)"E", 0, 0, (APTR)MN_EJECT },
    { NM_ITEM,  (STRPTR)"Choose image...",      (STRPTR)"O", 0, 0, (APTR)MN_BROWSE },
    { NM_ITEM,  NM_BARLABEL,                    NULL,        0, 0, NULL },
    { NM_ITEM,  (STRPTR)"Drive PD0:",           (STRPTR)"0", CHECKIT | CHECKED, 0xE0, (APTR)MN_PD0 },
    { NM_ITEM,  (STRPTR)"Drive PD1:",           (STRPTR)"1", CHECKIT, 0xD0, (APTR)MN_PD1 },
    { NM_ITEM,  (STRPTR)"Drive PD2:",           (STRPTR)"2", CHECKIT, 0xB0, (APTR)MN_PD2 },
    { NM_ITEM,  (STRPTR)"Drive PD3:",           (STRPTR)"3", CHECKIT, 0x70, (APTR)MN_PD3 },
    { NM_ITEM,  NM_BARLABEL,                    NULL,        0, 0, NULL },
    { NM_ITEM,  (STRPTR)"Read-write",           (STRPTR)"W", CHECKIT | MENUTOGGLE, 0, (APTR)MN_RW },

    { NM_TITLE, (STRPTR)"Settings",             NULL,        0, 0, NULL },
    { NM_ITEM,  (STRPTR)"Connection...",        NULL,        0, 0, (APTR)MN_SET_CONN },
    { NM_ITEM,  (STRPTR)"Drive and browser...", NULL,        0, 0, (APTR)MN_SET_DRIVE },
    { NM_ITEM,  NM_BARLABEL,                    NULL,        0, 0, NULL },
    { NM_ITEM,  (STRPTR)"Snapshot window",      NULL,        0, 0, (APTR)MN_SNAPSHOT },
    { NM_ITEM,  (STRPTR)"Unsnapshot window",    NULL,        0, 0, (APTR)MN_UNSNAPSHOT },
    { NM_END,   NULL,                           NULL,        0, 0, NULL }
};

static struct MenuItem *findItem(struct Menu *m, ULONG id)
{
    for (; m; m = m->NextMenu) {
        struct MenuItem *it;
        for (it = m->FirstItem; it; it = it->NextItem)
            if ((ULONG)GTMENUITEM_USERDATA(it) == id) return it;
    }
    return NULL;
}

/* make the Disk menu's checkmarks show g->drive and g->rw */
static void syncMenu(struct Gui *g)
{
    struct MenuItem *it;
    int i;
    if (!g->menu) return;
    if (g->strip) ClearMenuStrip(g->win);
    for (i = 0; i < 4; i++)
        if ((it = findItem(g->menu, MN_PD0 + i))) {
            if (i == g->drive) it->Flags |= CHECKED; else it->Flags &= ~CHECKED;
        }
    if ((it = findItem(g->menu, MN_RW))) {
        if (g->rw) it->Flags |= CHECKED; else it->Flags &= ~CHECKED;
    }
    if (g->strip) ResetMenuStrip(g->win, g->menu);
}

/* ---- small helpers -------------------------------------------------------- */

static CONST_STRPTR gadStr(struct Gadget *g)
{
    struct StringInfo *si = (struct StringInfo *)g->SpecialInfo;
    return (CONST_STRPTR)si->Buffer;
}

static WORD tlen(struct Gui *g, const char *s)
{
    return (WORD)TextLength(&g->mrp, (STRPTR)s, strlen(s));
}

static WORD maxw(WORD a, WORD b) { return a > b ? a : b; }

static void setStatus(struct Gui *g, CONST_STRPTR t)
{
    strncpy(g->status, (const char *)t, sizeof g->status - 1);
    g->status[sizeof g->status - 1] = '\0';
    if (g->gStatus)
        GT_SetGadgetAttrs(g->gStatus, g->win, NULL, GTTX_Text, (ULONG)g->status, TAG_END);
}

/* short wording for the status line (the CLI keeps the long texts) */
static CONST_STRPTR guiErr(LONG r)
{
    switch (r) {
    case A314_ERR_LIB:  return (CONST_STRPTR)"No bsdsocket.library";
    case A314_ERR_SOCK: return (CONST_STRPTR)"No socket";
    case A314_ERR_HOST: return (CONST_STRPTR)"Host must be an IP";
    case A314_ERR_CONN: return (CONST_STRPTR)"Daemon not reachable";
    case A314_ERR_SEND: return (CONST_STRPTR)"Send failed";
    default:            return a314ErrText(r);
    }
}

static void setDrive(struct Gui *g, WORD d)
{
    g->drive = d;
    if (g->gDrive) GT_SetGadgetAttrs(g->gDrive, g->win, NULL, GTCY_Active, (ULONG)d, TAG_END);
    syncMenu(g);
}

static void setRW(struct Gui *g, BOOL rw)
{
    g->rw = rw;
    if (g->gRW) GT_SetGadgetAttrs(g->gRW, g->win, NULL, GTCB_Checked, (ULONG)rw, TAG_END);
    syncMenu(g);
}

/* Block the main window (an empty requester) while a modal window is open,
 * so clicks and menu picks made meanwhile don't run afterwards. */
static void busy(struct Gui *g, BOOL on)
{
    if (on && !g->blocked) {
        InitRequester(&g->blk);
        g->blocked = Request(&g->blk, g->win) ? TRUE : FALSE;
    } else if (!on && g->blocked) {
        EndRequest(&g->blk, g->win);
        g->blocked = FALSE;
    }
}

/* ---- custom Pi-directory browser (a file requester over the daemon) ------- */

enum { BGAD_LIST = 1, BGAD_PARENT, BGAD_OPEN, BGAD_CANCEL };

struct BItem {              /* one directory entry, node.ln_Name = disp */
    struct Node node;
    UBYTE isDir;
    char  name[128];        /* raw name, for navigation                */
    char  disp[132];        /* shown name (dirs get a trailing '/')     */
};

struct Browser {
    struct Gui    *g;
    struct Window *win;
    struct Gadget *glist, *gList, *gPath;
    struct List    items;
    char  curdir[300];
    WORD  selected;
};

static void bInitList(struct List *l)
{
    l->lh_Head     = (struct Node *)&l->lh_Tail;
    l->lh_Tail     = NULL;
    l->lh_TailPred = (struct Node *)&l->lh_Head;
    l->lh_Type = 0; l->l_pad = 0;
}

static void bFreeItems(struct List *l)
{
    struct Node *n;
    while ((n = RemHead(l))) FreeVec(n);
}

/* TRUE if name (length len) ends in .adf/.hdf/.adz (case-insensitive) */
static int bImageExt(const char *s, int len)
{
    static const char ext[3][5] = { ".adf", ".hdf", ".adz" };
    int i, k;
    for (i = 0; i < 3; i++) {
        if (len >= 4) {
            int m = 1;
            for (k = 0; k < 4; k++) {
                char a = s[len - 4 + k];
                if (a >= 'A' && a <= 'Z') a = (char)(a + 32);
                if (a != ext[i][k]) { m = 0; break; }
            }
            if (m) return 1;
        }
    }
    return 0;
}

/* Parse the output of `ls -1Ap` (dirs have a trailing '/'). Call with
 * wantDir=1 first, then wantDir=0, to list subdirectories before files.
 * Files are filtered to image types. */
static void bParseLs(struct List *l, const char *buf, int wantDir)
{
    const char *p = buf;
    while (*p) {
        const char *eol = p; int len, isDir, nl;
        while (*eol && *eol != '\n') eol++;
        len = (int)(eol - p);
        while (len > 0 && (p[len - 1] == '\r' || p[len - 1] == ' ')) len--;
        if (len > 0) {
            isDir = (p[len - 1] == '/');
            nl = isDir ? len - 1 : len;
            if (isDir == wantDir && nl > 0 && (isDir || bImageExt(p, nl))) {
                struct BItem *it = (struct BItem *)AllocVec(sizeof(struct BItem), MEMF_CLEAR);
                if (it) {
                    int cn = nl; if (cn > 126) cn = 126;
                    memcpy(it->name, p, cn); it->name[cn] = '\0';
                    strcpy(it->disp, it->name);
                    it->isDir = (UBYTE)isDir;
                    if (isDir) strcat(it->disp, "/");
                    it->node.ln_Name = it->disp;
                    AddTail(l, &it->node);
                }
            }
        }
        p = *eol ? eol + 1 : eol;
    }
}

static struct BItem *bItemAt(struct List *l, int idx)
{
    struct Node *n = l->lh_Head; int i = 0;
    if (idx < 0) return NULL;
    while (n->ln_Succ) { if (i == idx) return (struct BItem *)n; n = n->ln_Succ; i++; }
    return NULL;
}

static void bJoin(const char *dir, const char *name, char *out, int outsize)
{
    int p = 0, i;
    for (i = 0; dir[i] && p < outsize - 1; i++) out[p++] = dir[i];
    if (p > 0 && out[p - 1] != '/' && p < outsize - 1) out[p++] = '/';
    for (i = 0; name[i] && p < outsize - 1; i++) out[p++] = name[i];
    out[p] = '\0';
}

static void bParentOf(const char *dir, char *out, int outsize)
{
    int len = (int)strlen(dir), i, last = -1;
    for (i = 0; i < len; i++) if (dir[i] == '/') last = i;
    if (last <= 0) { strcpy(out, "/"); return; }
    if (last >= outsize) last = outsize - 1;
    memcpy(out, dir, last); out[last] = '\0';
}

/* List a Pi directory by running the standard a314 `pi` command
 * (`pi ls -1Ap <dir>`) and capturing its output — no daemon patch needed.
 * Returns bytes read into buf (>=0), or -1 on failure. */
static LONG piListDir(CONST_STRPTR dir, char *buf, int bufsize)
{
    char cmd[400];
    BPTR out, in;
    LONG total = 0, rc;
    int p = 0, i;
    const char *pre = "C:pi ls -1Ap ";
    for (i = 0; pre[i] && p < (int)sizeof cmd - 1; i++) cmd[p++] = pre[i];
    for (i = 0; dir[i] && p < (int)sizeof cmd - 1; i++) cmd[p++] = dir[i];
    cmd[p] = '\0';

    /* pi refuses a redirected (non-interactive) stdin, so give it an AUTO
     * console — it stays closed unless actually read from. */
    in  = Open((STRPTR)"CON:0/0/300/40/A314Mount/AUTO/CLOSE/INACTIVE", MODE_OLDFILE);
    out = Open((STRPTR)"RAM:a314list.tmp", MODE_NEWFILE);
    if (!out) { if (in) Close(in); return -1; }

    /* run the command, capturing its output into the file (synchronous).
     * NB: this dos.library does NOT close SYS_Input/SYS_Output for us, so we
     * close them ourselves (the write handle must be closed before we can
     * re-open the file to read it). */
    rc = SystemTags((STRPTR)cmd,
        SYS_Input,  (ULONG)in,
        SYS_Output, (ULONG)out,
        SYS_Asynch, FALSE,
        TAG_END);
    (void)rc;
    Close(out);
    if (in) Close(in);

    out = Open((STRPTR)"RAM:a314list.tmp", MODE_OLDFILE);
    if (out) {
        total = Read(out, buf, bufsize - 1);
        if (total < 0) total = 0;
        Close(out);
    }
    buf[total] = '\0';
    DeleteFile((STRPTR)"RAM:a314list.tmp");
    return total;
}

/* re-list b->curdir into the listview (subdirectories first, then images) */
static void bRefresh(struct Browser *b)
{
    char *reply;

    GT_SetGadgetAttrs(b->gList, b->win, NULL, GTLV_Labels, ~0, TAG_END);
    bFreeItems(&b->items);
    b->selected = -1;

    reply = (char *)AllocVec(16384, MEMF_CLEAR);
    if (reply) {
        LONG n = piListDir((CONST_STRPTR)b->curdir, reply, 16384);
        if (n > 0) { bParseLs(&b->items, reply, 1); bParseLs(&b->items, reply, 0); }
        FreeVec(reply);
    }
    GT_SetGadgetAttrs(b->gList, b->win, NULL,
        GTLV_Labels, (ULONG)&b->items, GTLV_Selected, ~0, GTLV_Top, 0, TAG_END);
    GT_SetGadgetAttrs(b->gPath, b->win, NULL, GTTX_Text, (ULONG)b->curdir, TAG_END);
}

static void bChoose(struct Browser *b, struct BItem *it)
{
    struct Gui *g = b->g;
    char path[300];
    bJoin(b->curdir, it->name, path, (int)sizeof path);
    strncpy(g->saveAdf, path, sizeof g->saveAdf - 1);
    g->saveAdf[sizeof g->saveAdf - 1] = '\0';
    GT_SetGadgetAttrs(g->gAdf, g->win, NULL, GTST_String, (ULONG)g->saveAdf, TAG_END);
    setStatus(g, "Image set - click Insert");
}

/* Open the browser, let the user pick a Pi image, drop its path in the field. */
static void doBrowse(struct Gui *g)
{
    struct Browser b;
    struct NewGadget ng;
    struct Gadget *gad;
    struct Screen *sc = g->scr;
    WORD winw, winh, wtop, cl, cr, y, by, gh, row, bw;
    BOOL done = FALSE;

    memset(&b, 0, sizeof b);
    b.g = g; b.selected = -1;
    bInitList(&b.items);
    setStr(b.curdir, sizeof b.curdir, g_set.browse[0] == '/' ? g_set.browse : DEF_BROWSE);

    gh  = (WORD)(g->font->tf_YSize + 3);
    row = gh + 3;
    bw  = maxw(maxw(tlen(g, "Parent"), tlen(g, "Open")), tlen(g, "Cancel")) + 16;
    if (bw < 90) bw = 90;
    wtop = sc->BarHeight + 1;
    winw = 360; if (winw > sc->Width) winw = sc->Width;
    winh = 200; if (winh > sc->Height - wtop) winh = sc->Height - wtop;

    b.win = OpenWindowTags(NULL,
        WA_Title,     (ULONG)"Select image on the Pi",
        WA_Left,      (sc->Width - winw) / 2, WA_Top, wtop,
        WA_Width,     winw, WA_Height, winh,
        WA_AutoAdjust, TRUE,
        WA_Flags,     WFLG_DRAGBAR | WFLG_DEPTHGADGET | WFLG_CLOSEGADGET | WFLG_ACTIVATE,
        WA_IDCMP,     IDCMP_CLOSEWINDOW | IDCMP_REFRESHWINDOW | LISTVIEWIDCMP | BUTTONIDCMP,
        WA_PubScreen, (ULONG)sc,
        TAG_END);
    if (!b.win) { bFreeItems(&b.items); return; }
    WindowToFront(b.win);
    ActivateWindow(b.win);

    cl = b.win->BorderLeft + 4;
    cr = b.win->Width - b.win->BorderRight - 4;
    y  = b.win->BorderTop + 2;
    by = b.win->Height - b.win->BorderBottom - gh - 3;      /* button row */

    gad = CreateContext(&b.glist);
    memset(&ng, 0, sizeof ng);
    ng.ng_VisualInfo = g->vi; ng.ng_TextAttr = g->ta;

    ng.ng_LeftEdge = cl; ng.ng_TopEdge = y; ng.ng_Width = cr - cl; ng.ng_Height = gh;
    ng.ng_Flags = 0; ng.ng_GadgetID = 0;
    gad = b.gPath = CreateGadget(TEXT_KIND, gad, &ng,
                                 GTTX_Text, (ULONG)b.curdir, GTTX_Border, TRUE,
                                 GTTX_Clipped, TRUE, TAG_END);

    ng.ng_TopEdge = y + row; ng.ng_Height = by - (y + row) - 3; ng.ng_Width = cr - cl;
    ng.ng_GadgetID = BGAD_LIST;
    gad = b.gList = CreateGadget(LISTVIEW_KIND, gad, &ng,
                                 GTLV_Labels, (ULONG)&b.items,
                                 GTLV_ShowSelected, 0L, GTLV_Selected, ~0, TAG_END);

    ng.ng_TopEdge = by; ng.ng_Height = gh; ng.ng_Width = bw;
    ng.ng_LeftEdge = cl;          ng.ng_GadgetText = (UBYTE *)"Parent"; ng.ng_GadgetID = BGAD_PARENT;
    gad = CreateGadget(BUTTON_KIND, gad, &ng, TAG_END);
    ng.ng_LeftEdge = cl + bw + 6; ng.ng_GadgetText = (UBYTE *)"Open";   ng.ng_GadgetID = BGAD_OPEN;
    gad = CreateGadget(BUTTON_KIND, gad, &ng, TAG_END);
    ng.ng_LeftEdge = cr - bw;     ng.ng_GadgetText = (UBYTE *)"Cancel"; ng.ng_GadgetID = BGAD_CANCEL;
    gad = CreateGadget(BUTTON_KIND, gad, &ng, TAG_END);

    if (gad) {
        AddGList(b.win, b.glist, ~0, -1, NULL);
        RefreshGList(b.glist, b.win, NULL, -1);
        GT_RefreshWindow(b.win, NULL);
    }
    bRefresh(&b);

    while (!done) {
        struct IntuiMessage *im;
        WaitPort(b.win->UserPort);
        while ((im = GT_GetIMsg(b.win->UserPort))) {
            ULONG cls = im->Class; UWORD code = im->Code;
            struct Gadget *gd = (struct Gadget *)im->IAddress;
            GT_ReplyIMsg(im);
            switch (cls) {
            case IDCMP_CLOSEWINDOW:
                done = TRUE; break;
            case IDCMP_GADGETUP:
                switch (gd->GadgetID) {
                case BGAD_LIST:   b.selected = (WORD)code; break;
                case BGAD_PARENT: { char p[300]; bParentOf(b.curdir, p, (int)sizeof p);
                                    strcpy(b.curdir, p); bRefresh(&b); } break;
                case BGAD_OPEN: {
                    struct BItem *it = bItemAt(&b.items, b.selected);
                    if (it) {
                        if (it->isDir) { char p[300]; bJoin(b.curdir, it->name, p, (int)sizeof p);
                                         strcpy(b.curdir, p); bRefresh(&b); }
                        else { bChoose(&b, it); done = TRUE; }
                    }
                } break;
                case BGAD_CANCEL: done = TRUE; break;
                }
                break;
            case IDCMP_REFRESHWINDOW:
                GT_BeginRefresh(b.win); GT_EndRefresh(b.win, TRUE); break;
            }
            if (done) break;
        }
    }

    GT_SetGadgetAttrs(b.gList, b.win, NULL, GTLV_Labels, ~0, TAG_END);
    CloseWindow(b.win);
    FreeGadgets(b.glist);
    bFreeItems(&b.items);
}

static void browseModal(struct Gui *g)
{
    busy(g, TRUE);
    doBrowse(g);
    busy(g, FALSE);
}

/* ------------------------------------------------------------------ */

static void doOp(struct Gui *g, BOOL insert)
{
    char cmd[300], reply[160];
    CONST_STRPTR adf  = gadStr(g->gAdf);
    CONST_STRPTR host = gadStr(g->gHost);
    CONST_STRPTR ports = gadStr(g->gPort);
    UWORD port = 0;
    LONG r;
    int i;

    for (i = 0; ports[i] >= '0' && ports[i] <= '9'; i++) port = (UWORD)(port * 10 + (ports[i] - '0'));
    if (port == 0) port = A314_DEF_PORT;
    if (!host || host[0] == '\0') host = (CONST_STRPTR)A314_DEF_HOST;

    if (insert && (!adf || adf[0] == '\0')) { browseModal(g); return; }

    setStatus(g, insert ? "Inserting..." : "Ejecting...");
    if (insert) {
        r = a314Insert(host, port, g->drive, adf, g->rw, reply, (int)sizeof reply);
    } else {
        a314BuildEject(cmd, (int)sizeof cmd, g->drive);
        r = a314SendCommand(host, port, cmd, reply, (int)sizeof reply);
    }
    if (r == A314_OK) {
        char buf[80];
        strcpy(buf, "PD");
        buf[2] = (char)('0' + g->drive); buf[3] = '\0';
        strcat(buf, insert ? ": inserted" : ": ejected");
        setStatus(g, reply[0] ? (CONST_STRPTR)reply : (CONST_STRPTR)buf);
    } else {
        setStatus(g, guiErr(r));
    }
}

/* ---- layout ------------------------------------------------------------- */

/* Measure what the main window needs in g->font: label column, gadget widths,
 * the narrowest inner width and the (fixed) inner height. */
static void measure(struct Gui *g)
{
    WORD fh = g->font->tf_YSize, a, b, c, d;
    g->lab = maxw(maxw(tlen(g, "Drive"), tlen(g, "Image")), tlen(g, "Host")) + 8;
    g->cyw = tlen(g, "PD0:") + 32;
    g->bw  = maxw(tlen(g, "Insert"), tlen(g, "Eject")) + 16;
    g->pw  = tlen(g, "000000") + 12;
    a = g->lab + g->cyw + 16 + tlen(g, "Read-write") + 8 + CHECKBOX_WIDTH;
    b = g->lab + 80 + 4 + FOLDER_W;
    c = 2 * g->bw + 12 + 100;                         /* status: at least 100 */
    d = g->lab + 80 + 16 + tlen(g, "Port") + g->pw;
    g->minIn  = maxw(maxw(a, b), maxw(c, d)) + 8;
    g->innerH = 2 + 3 * (fh + 6) + (fh + 3) + 3;      /* four rows */
}

static BOOL useFont(struct Gui *g, struct TextAttr *ta)
{
    if (!(g->font = OpenFont(ta))) return FALSE;
    g->ta = ta;
    InitRastPort(&g->mrp);
    SetFont(&g->mrp, g->font);
    measure(g);
    return TRUE;
}

/* the outer height the main window will have (the bottom border holds the
 * size gadget: about 11 pixels) */
static WORD outerH(struct Gui *g)
{
    return (WORD)(g->scr->WBorTop + g->scr->Font->ta_YSize + 1 + g->innerH + 11);
}

/* The screen's font, unless the window would not fit the screen with it (or
 * be wider than a classic window) - then Topaz 8. */
static BOOL chooseFont(struct Gui *g)
{
    struct Screen *sc = g->scr;
    if (useFont(g, sc->Font)) {
        if (g->minIn + 24 <= MAX_WIN_W && g->minIn + 24 <= sc->Width &&
            outerH(g) <= sc->Height - sc->BarHeight - 1)
            return TRUE;
        CloseFont(g->font); g->font = NULL;
    }
    return useFont(g, &topaz8);
}

static BOOL makeGadgets(struct Gui *g)
{
    struct NewGadget ng;
    struct Gadget *gad;
    struct Window *w = g->win;
    WORD fh = g->font->tf_YSize;
    WORD cl = w->BorderLeft + 4;
    WORD cr = w->Width - w->BorderRight - 4;
    WORD row = fh + 6, gh = fh + 3, lab = g->lab, x;
    WORD y = w->BorderTop + 2;

    gad = CreateContext(&g->glist);
    if (!gad) return FALSE;
    memset(&ng, 0, sizeof ng);
    ng.ng_VisualInfo = g->vi;
    ng.ng_TextAttr   = g->ta;

    /* Drive cycle + Read-write checkbox */
    ng.ng_LeftEdge = cl + lab; ng.ng_TopEdge = y; ng.ng_Width = g->cyw; ng.ng_Height = gh;
    ng.ng_GadgetText = (UBYTE *)"Drive"; ng.ng_Flags = PLACETEXT_LEFT; ng.ng_GadgetID = GAD_DRIVE;
    gad = g->gDrive = CreateGadget(CYCLE_KIND, gad, &ng,
                                   GTCY_Labels, (ULONG)driveLabels,
                                   GTCY_Active, (ULONG)g->drive, TAG_END);
    ng.ng_LeftEdge = cr - CHECKBOX_WIDTH; ng.ng_Width = CHECKBOX_WIDTH;
    ng.ng_GadgetText = (UBYTE *)"Read-write"; ng.ng_Flags = PLACETEXT_LEFT; ng.ng_GadgetID = GAD_RW;
    gad = g->gRW = CreateGadget(CHECKBOX_KIND, gad, &ng, GTCB_Checked, (ULONG)g->rw, TAG_END);

    /* Image path (ADF/HDF) + a folder button that opens the Pi browser */
    y += row;
    ng.ng_LeftEdge = cl + lab; ng.ng_TopEdge = y;
    ng.ng_Width = cr - (cl + lab) - FOLDER_W - 4; ng.ng_Height = gh;
    ng.ng_GadgetText = (UBYTE *)"Image"; ng.ng_Flags = PLACETEXT_LEFT; ng.ng_GadgetID = GAD_ADF;
    gad = g->gAdf = CreateGadget(STRING_KIND, gad, &ng,
                                 GTST_MaxChars, 200, GTST_String, (ULONG)g->saveAdf, TAG_END);
    browseGad.LeftEdge = cr - FOLDER_W;
    browseGad.TopEdge  = y + (gh - FOLDER_H) / 2;

    /* Insert / Eject + status */
    y += row;
    ng.ng_LeftEdge = cl; ng.ng_TopEdge = y; ng.ng_Width = g->bw; ng.ng_Height = gh;
    ng.ng_GadgetText = (UBYTE *)"Insert"; ng.ng_Flags = 0; ng.ng_GadgetID = GAD_INSERT;
    gad = g->gInsert = CreateGadget(BUTTON_KIND, gad, &ng, TAG_END);
    ng.ng_LeftEdge = cl + g->bw + 6;
    ng.ng_GadgetText = (UBYTE *)"Eject"; ng.ng_GadgetID = GAD_EJECT;
    gad = g->gEject = CreateGadget(BUTTON_KIND, gad, &ng, TAG_END);
    x = cl + 2 * g->bw + 12;
    ng.ng_LeftEdge = x; ng.ng_Width = cr - x; if (ng.ng_Width < 8) ng.ng_Width = 8;
    ng.ng_GadgetText = NULL; ng.ng_GadgetID = GAD_STATUS;
    gad = g->gStatus = CreateGadget(TEXT_KIND, gad, &ng,
                                    GTTX_Text, (ULONG)g->status, GTTX_Border, FALSE,
                                    GTTX_Clipped, TRUE, TAG_END);

    /* Host / Port */
    y += row;
    ng.ng_LeftEdge = cl + lab; ng.ng_TopEdge = y; ng.ng_Height = gh;
    ng.ng_Width = cr - g->pw - tlen(g, "Port") - 16 - (cl + lab);
    ng.ng_GadgetText = (UBYTE *)"Host"; ng.ng_Flags = PLACETEXT_LEFT; ng.ng_GadgetID = GAD_HOST;
    gad = g->gHost = CreateGadget(STRING_KIND, gad, &ng,
                                  GTST_MaxChars, 60, GTST_String, (ULONG)g->saveHost, TAG_END);
    ng.ng_LeftEdge = cr - g->pw; ng.ng_Width = g->pw;
    ng.ng_GadgetText = (UBYTE *)"Port"; ng.ng_GadgetID = GAD_PORT;
    gad = g->gPort = CreateGadget(STRING_KIND, gad, &ng,
                                  GTST_MaxChars, 6, GTST_String, (ULONG)g->savePort, TAG_END);
    return (gad != NULL);
}

static void saveFields(struct Gui *g)
{
    if (g->gAdf)  setStr(g->saveAdf,  sizeof g->saveAdf,  (const char *)gadStr(g->gAdf));
    if (g->gHost) setStr(g->saveHost, sizeof g->saveHost, (const char *)gadStr(g->gHost));
    if (g->gPort) setStr(g->savePort, sizeof g->savePort, (const char *)gadStr(g->gPort));
}

/* GadTools gadgets don't reflow on their own; on a resize we snapshot the
 * field contents, tear the gadgets down, clear the interior, and rebuild them
 * at the new width. */
static void rebuildGadgets(struct Gui *g)
{
    saveFields(g);

    RemoveGList(g->win, &browseGad, 1);
    if (g->glist) {
        RemoveGList(g->win, g->glist, -1);
        FreeGadgets(g->glist);
        g->glist = NULL;
        g->gDrive = g->gRW = g->gAdf = g->gInsert = g->gEject = g->gStatus = g->gHost = g->gPort = NULL;
    }
    /* erase old gadget imagery in the interior before re-laying-out */
    SetAPen(g->win->RPort, 0);
    RectFill(g->win->RPort, g->win->BorderLeft, g->win->BorderTop,
             g->win->Width  - g->win->BorderRight  - 1,
             g->win->Height - g->win->BorderBottom - 1);

    if (makeGadgets(g)) {
        AddGList(g->win, g->glist, ~0, -1, NULL);
        RefreshGList(g->glist, g->win, NULL, -1);
        GT_RefreshWindow(g->win, NULL);
        AddGadget(g->win, &browseGad, ~0);
        RefreshGList(&browseGad, g->win, NULL, 1);
    }
    RefreshWindowFrame(g->win);   /* redraw borders/size gadget cleanly */
}

static BOOL buildWindow(struct Gui *g)
{
    struct Screen *sc;
    LONG x, y, w, h, minW;

    g->scr = LockPubScreen(NULL);
    if (!g->scr) return FALSE;
    g->vi = GetVisualInfo(g->scr, TAG_END);
    if (!g->vi) return FALSE;
    if (!chooseFont(g)) return FALSE;
    sc = g->scr;
    strcpy(g->status, "Ready");

    /* the snapshot place and width (Settings > Snapshot window), kept on the
     * screen; else a classic-size window centred under the screen title */
    minW = g->minIn + 24;
    w = DEF_WIN_W; y = sc->BarHeight + 2; h = outerH(g);
    if (g_set.winw > 0) { w = g_set.winw; y = g_set.winy; }
    if (w < minW) w = minW;
    if (w > sc->Width) w = sc->Width;
    x = (g_set.winw > 0) ? g_set.winx : (sc->Width - w) / 2;
    if (x + w > sc->Width) x = sc->Width - w;
    if (y + h > sc->Height) y = sc->Height - h;
    if (x < 0) x = 0;
    if (y < 0) y = 0;

    g->win = OpenWindowTags(NULL,
        WA_Title,      (ULONG)"A314Mount",
        WA_Left,       (ULONG)x, WA_Top, (ULONG)y,
        WA_Width,      (ULONG)w, WA_InnerHeight, (ULONG)g->innerH,
        WA_MinWidth,   (ULONG)(minW < sc->Width ? minW : sc->Width), WA_MaxWidth, (ULONG)sc->Width,
        WA_MinHeight,  40, WA_MaxHeight, (ULONG)sc->Height,
        WA_AutoAdjust, TRUE,
        WA_Flags,      WFLG_DRAGBAR | WFLG_DEPTHGADGET | WFLG_CLOSEGADGET |
                       WFLG_SIZEGADGET | WFLG_SIZEBBOTTOM | WFLG_ACTIVATE,
        WA_IDCMP,      IDCMP_CLOSEWINDOW | IDCMP_REFRESHWINDOW | IDCMP_MENUPICK |
                       IDCMP_NEWSIZE |
                       BUTTONIDCMP | STRINGIDCMP | CYCLEIDCMP | CHECKBOXIDCMP,
        WA_PubScreen,  (ULONG)g->scr,
        TAG_END);
    if (!g->win) return FALSE;
    /* the real borders are known now: stretch width only, height fixed */
    minW = g->minIn + g->win->BorderLeft + g->win->BorderRight;
    if (minW > g->win->Width) minW = g->win->Width;
    WindowLimits(g->win, minW, g->win->Height, sc->Width, g->win->Height);
    g->prevW = g->win->Width;

    if (makeGadgets(g)) {
        AddGList(g->win, g->glist, ~0, -1, NULL);
        RefreshGList(g->glist, g->win, NULL, -1);
        GT_RefreshWindow(g->win, NULL);
        AddGadget(g->win, &browseGad, ~0);          /* plain image button */
        RefreshGList(&browseGad, g->win, NULL, 1);
    }
    g->menu = CreateMenus(amiNewMenu, TAG_END);
    if (g->menu) {
        if (LayoutMenus(g->menu, g->vi, TAG_END)) {
            syncMenu(g);
            g->strip = SetMenuStrip(g->win, g->menu) ? TRUE : FALSE;
        } else { FreeMenus(g->menu); g->menu = NULL; }
    }
    return TRUE;
}

static void closeWin(struct Gui *g)
{
    busy(g, FALSE);
    if (g->strip)  { ClearMenuStrip(g->win);    g->strip = FALSE; }
    if (g->menu)   { FreeMenus(g->menu);        g->menu = NULL; }
    if (g->win)    { CloseWindow(g->win);       g->win = NULL; }
    if (g->glist)  { FreeGadgets(g->glist);     g->glist = NULL; }
    if (g->font)   { CloseFont(g->font);        g->font = NULL; }
    if (g->vi)     { FreeVisualInfo(g->vi);     g->vi = NULL; }
    if (g->scr)    { UnlockPubScreen(NULL, g->scr); g->scr = NULL; }
}

/* ---- settings pages ------------------------------------------------------ */

static void reportWrite(struct Gui *g, int r)
{
    if (!setsWrite(&g_set, r == AMG_SAVE))
        setStatus(g, "Could not write prefs");
    else
        setStatus(g, r == AMG_SAVE ? "Settings saved" : "Settings in use");
}

static void connGadget(AmgPage *pg, int id, UWORD code, void *ctx)
{
    (void)code; (void)ctx;
    if (id == AMG_ID_DEFAULTS) {
        GT_SetGadgetAttrs(pg->g[0], pg->win, NULL, GTST_String, (ULONG)A314_DEF_HOST, TAG_END);
        GT_SetGadgetAttrs(pg->g[1], pg->win, NULL, GTST_String, (ULONG)DEF_PORT, TAG_END);
    }
}

/* Settings > Connection...: the daemon's host and port */
static void pageConn(struct Gui *g)
{
    AmgPage pg;
    struct NewGadget ng;
    struct Gadget *gad;
    int lw, w, h, bh, rowh, y, r = AMG_CANCEL;

    bh = g->font->tf_YSize + 6; rowh = bh + 4;
    lw = maxw(tlen(g, "Host"), tlen(g, "Port")) + 16;
    w = 8 + lw + tlen(g, "255.255.255.255") + 40 + 8;
    if (w < amg_min_width(g->ta)) w = amg_min_width(g->ta);
    if (w > MAX_WIN_W - 24) w = MAX_WIN_W - 24;
    h = 4 + 2 * rowh + 6 + bh + 4;
    if (!amg_page_open(&pg, g->scr, g->vi, g->ta, g->win, "A314Mount: Connection", w, h)) return;
    gad = CreateContext(&pg.glist);
    memset(&ng, 0, sizeof ng);
    ng.ng_VisualInfo = g->vi; ng.ng_TextAttr = g->ta;
    ng.ng_LeftEdge = pg.win->BorderLeft + 8 + lw; ng.ng_Height = bh; ng.ng_Flags = PLACETEXT_LEFT;
    y = 4;
    ng.ng_TopEdge = pg.win->BorderTop + y; y += rowh;
    ng.ng_Width = w - 16 - lw;
    ng.ng_GadgetText = (UBYTE *)"Host"; ng.ng_GadgetID = 1;
    pg.g[0] = gad = CreateGadget(STRING_KIND, gad, &ng, GTST_String, (ULONG)g_set.host,
                                 GTST_MaxChars, (ULONG)(sizeof g_set.host - 1), TAG_END);
    ng.ng_TopEdge = pg.win->BorderTop + y; y += rowh;
    ng.ng_Width = g->pw;
    ng.ng_GadgetText = (UBYTE *)"Port"; ng.ng_GadgetID = 2;
    pg.g[1] = gad = CreateGadget(STRING_KIND, gad, &ng, GTST_String, (ULONG)g_set.port,
                                 GTST_MaxChars, 5, TAG_END);
    gad = amg_buttons(&pg, gad, y + 6, w, 1);
    if (gad) {
        amg_page_show(&pg);
        r = amg_page_run(&pg, connGadget, g);
        if (r != AMG_CANCEL) {
            const char *s = amg_string(pg.g[0]);
            setStr(g_set.host, sizeof g_set.host, *s ? s : A314_DEF_HOST);
            s = amg_string(pg.g[1]);
            setStr(g_set.port, sizeof g_set.port, portOf(s) ? s : DEF_PORT);
        }
    }
    amg_page_close(&pg);
    if (r != AMG_CANCEL) {           /* in force now, in the window too */
        GT_SetGadgetAttrs(g->gHost, g->win, NULL, GTST_String, (ULONG)g_set.host, TAG_END);
        GT_SetGadgetAttrs(g->gPort, g->win, NULL, GTST_String, (ULONG)g_set.port, TAG_END);
        saveFields(g);
        reportWrite(g, r);
    }
}

struct DriveCtx { WORD drive; };

static void driveGadget(AmgPage *pg, int id, UWORD code, void *ctx)
{
    struct DriveCtx *dc = (struct DriveCtx *)ctx;
    if (id == 1) dc->drive = (WORD)code;
    else if (id == AMG_ID_DEFAULTS) {
        dc->drive = 0;
        GT_SetGadgetAttrs(pg->g[0], pg->win, NULL, GTCY_Active, 0, TAG_END);
        GT_SetGadgetAttrs(pg->g[1], pg->win, NULL, GTCB_Checked, FALSE, TAG_END);
        GT_SetGadgetAttrs(pg->g[2], pg->win, NULL, GTST_String, (ULONG)DEF_BROWSE, TAG_END);
    }
}

/* Settings > Drive and browser...: the drive and Read-write the window starts
 * with, and the Pi directory the image browser opens in */
static void pageDrive(struct Gui *g)
{
    AmgPage pg;
    struct NewGadget ng;
    struct Gadget *gad;
    struct DriveCtx dc;
    int lw, w, h, bh, rowh, y, r = AMG_CANCEL;

    dc.drive = g_set.drive;
    bh = g->font->tf_YSize + 6; rowh = bh + 4;
    lw = maxw(maxw(tlen(g, "Drive"), tlen(g, "Read-write")), tlen(g, "Browse from")) + 16;
    w = 8 + lw + 260 + 8;
    if (w < amg_min_width(g->ta)) w = amg_min_width(g->ta);
    if (w > MAX_WIN_W - 24) w = MAX_WIN_W - 24;
    h = 4 + 3 * rowh + 6 + bh + 4;
    if (!amg_page_open(&pg, g->scr, g->vi, g->ta, g->win, "A314Mount: Drive and browser", w, h)) return;
    gad = CreateContext(&pg.glist);
    memset(&ng, 0, sizeof ng);
    ng.ng_VisualInfo = g->vi; ng.ng_TextAttr = g->ta;
    ng.ng_LeftEdge = pg.win->BorderLeft + 8 + lw; ng.ng_Height = bh; ng.ng_Flags = PLACETEXT_LEFT;
    y = 4;
    ng.ng_TopEdge = pg.win->BorderTop + y; y += rowh;
    ng.ng_Width = g->cyw;
    ng.ng_GadgetText = (UBYTE *)"Drive"; ng.ng_GadgetID = 1;
    pg.g[0] = gad = CreateGadget(CYCLE_KIND, gad, &ng, GTCY_Labels, (ULONG)driveLabels,
                                 GTCY_Active, (ULONG)dc.drive, TAG_END);
    ng.ng_TopEdge = pg.win->BorderTop + y; y += rowh;
    ng.ng_Width = CHECKBOX_WIDTH;
    ng.ng_GadgetText = (UBYTE *)"Read-write"; ng.ng_GadgetID = 2;
    pg.g[1] = gad = CreateGadget(CHECKBOX_KIND, gad, &ng, GTCB_Checked, (ULONG)g_set.rw, TAG_END);
    ng.ng_TopEdge = pg.win->BorderTop + y; y += rowh;
    ng.ng_Width = w - 16 - lw;
    ng.ng_GadgetText = (UBYTE *)"Browse from"; ng.ng_GadgetID = 3;
    pg.g[2] = gad = CreateGadget(STRING_KIND, gad, &ng, GTST_String, (ULONG)g_set.browse,
                                 GTST_MaxChars, (ULONG)(sizeof g_set.browse - 1), TAG_END);
    gad = amg_buttons(&pg, gad, y + 6, w, 1);
    if (gad) {
        amg_page_show(&pg);
        r = amg_page_run(&pg, driveGadget, &dc);
        if (r != AMG_CANCEL) {
            const char *s = amg_string(pg.g[2]);
            g_set.drive = dc.drive;
            g_set.rw = amg_checked(pg.g[1]) ? TRUE : FALSE;
            setStr(g_set.browse, sizeof g_set.browse, s[0] == '/' ? s : DEF_BROWSE);
        }
    }
    amg_page_close(&pg);
    if (r != AMG_CANCEL) {           /* in force now, in the window too */
        setDrive(g, g_set.drive);
        setRW(g, g_set.rw);
        reportWrite(g, r);
    }
}

/* ---- events -------------------------------------------------------------- */

/* one menu pick (by its MN_* user data); rwOn = the Read-write item's state.
 * TRUE = quit */
static BOOL doMenu(struct Gui *g, ULONG id, BOOL rwOn)
{
    switch (id) {
    case MN_ABOUT:
        amg_about(g->win, GUI_NAME, GUI_VERSION,
                  "Inserts and ejects ADF and HDF images in the\n"
                  "A314's PD0: to PD3: drives, from the Amiga.\n\n"
                  "Licence: MIT");
        break;
    case MN_QUIT:   return TRUE;
    case MN_INSERT: doOp(g, TRUE);  break;
    case MN_EJECT:  doOp(g, FALSE); break;
    case MN_BROWSE: browseModal(g); break;
    case MN_PD0: case MN_PD1: case MN_PD2: case MN_PD3:
        setDrive(g, (WORD)(id - MN_PD0)); break;
    case MN_RW:     setRW(g, rwOn); break;
    case MN_SET_CONN:  busy(g, TRUE); pageConn(g);  busy(g, FALSE); break;
    case MN_SET_DRIVE: busy(g, TRUE); pageDrive(g); busy(g, FALSE); break;
    case MN_SNAPSHOT:     /* this place and width from now on (saved, like Save) */
        g_set.winx = g->win->LeftEdge; g_set.winy = g->win->TopEdge;
        g_set.winw = g->win->Width;    g_set.winh = g->win->Height;
        setStatus(g, setsWrite(&g_set, TRUE) ? "Window snapshot saved" : "Could not write prefs");
        break;
    case MN_UNSNAPSHOT:   /* back to the default place and size at the next start */
        g_set.winw = 0;
        setStatus(g, setsWrite(&g_set, TRUE) ? "Default window next time" : "Could not write prefs");
        break;
    default: break;
    }
    return FALSE;
}

static void eventLoop(struct Gui *g)
{
    BOOL done = FALSE;
    while (!done) {
        struct IntuiMessage *imsg;
        WaitPort(g->win->UserPort);
        while ((imsg = GT_GetIMsg(g->win->UserPort))) {
            ULONG cls = imsg->Class;
            UWORD code = imsg->Code;
            struct Gadget *gad = (struct Gadget *)imsg->IAddress;
            GT_ReplyIMsg(imsg);

            switch (cls) {
            case IDCMP_CLOSEWINDOW:
                done = TRUE; break;
            case IDCMP_GADGETUP:
                switch (gad->GadgetID) {
                case GAD_DRIVE:  g->drive = (WORD)code; syncMenu(g); break;
                case GAD_RW:     g->rw = code ? TRUE : FALSE; syncMenu(g); break;
                case GAD_INSERT: doOp(g, TRUE);  break;
                case GAD_EJECT:  doOp(g, FALSE); break;
                case GAD_BROWSE: browseModal(g); break;
                default: break;
                }
                break;
            case IDCMP_MENUPICK: {
                /* collect the picks first (multi-select via NextSelect), then
                 * act, so the strip is not changed while it is being walked */
                ULONG ids[16];
                BOOL rwOn = g->rw;
                int n = 0, i;
                while (code != MENUNULL && n < 16 && g->menu) {
                    struct MenuItem *it = ItemAddress(g->menu, code);
                    if (!it) break;
                    ids[n] = (ULONG)GTMENUITEM_USERDATA(it);
                    if (ids[n] == MN_RW) rwOn = (it->Flags & CHECKED) ? TRUE : FALSE;
                    n++;
                    code = it->NextSelect;
                }
                for (i = 0; i < n && !done; i++) done = doMenu(g, ids[i], rwOn);
                break;
            }
            case IDCMP_NEWSIZE:
                if (g->win->Width != g->prevW) {
                    g->prevW = g->win->Width;
                    rebuildGadgets(g);
                }
                break;
            case IDCMP_REFRESHWINDOW:
                GT_BeginRefresh(g->win); GT_EndRefresh(g->win, TRUE); break;
            default: break;
            }
            if (done) break;
        }
    }
}

int main(void)
{
    struct Gui g;
    int rc = 20;

    memset(&g, 0, sizeof g);

    IntuitionBase = (struct IntuitionBase *)OpenLibrary("intuition.library", 37);
    GfxBase       = (struct GfxBase *)OpenLibrary("graphics.library", 37);
    GadToolsBase  = OpenLibrary("gadtools.library", 37);

    if (IntuitionBase && GfxBase && GadToolsBase) {
        setsLoad(&g_set);
        setStr(g.saveHost, sizeof g.saveHost, g_set.host);
        setStr(g.savePort, sizeof g.savePort, g_set.port);
        g.saveAdf[0] = '\0';             /* empty: pick via the folder browser */
        g.drive = g_set.drive;
        g.rw    = g_set.rw;
        if (buildWindow(&g)) {
            eventLoop(&g);
            rc = 0;
        }
    }

    closeWin(&g);
    if (GadToolsBase)  CloseLibrary(GadToolsBase);
    if (GfxBase)       CloseLibrary((struct Library *)GfxBase);
    if (IntuitionBase) CloseLibrary((struct Library *)IntuitionBase);
    return rc;
}
