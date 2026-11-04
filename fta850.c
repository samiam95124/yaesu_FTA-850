/*
 * FTA-850 programmer: a front panel for the USB programming port of the
 * Yaesu FTA-850L, FTA-750L, FTA-550L and FTA-450L air band transceivers,
 * for Linux and macOS, on the AMI tool kit. The model is read from the radio
 * when the session starts (or from the file opened) and the pages follow
 * it: the FTA-750 family has 200 memories with a runway course each and no
 * routes, the FTA-550 no GPS, the FTA-450 no NAV band or positions.
 *
 * The window shows the radio on the left, its LCD reporting the link, and
 * the programmer's pages on the right: the memory book, the routes, the
 * weather channels, the setup menus, the GPS logger, and the raw memory
 * image.
 *
 * Use:
 *   fta850              find the radio on USB
 *   fta850 -p PORT      use this serial port
 *   fta850 --sim [MODEL] no hardware: a simulated radio (850, 750, 550 or 450)
 *
 * The radio has to be in CP mode: switch it off, then switch it on while
 * holding SQL. READ RADIO fetches its memory, the pages edit the copy,
 * WRITE RADIO (pressed twice) programs it back. OPEN and SAVE keep the
 * copy in YCE46 compatible .dat files.
 *
 * Fields: click a text field and type; Enter keeps the change, Esc drops
 * it, Tab moves on. A choice field steps to its next value on a click
 * (right half) or the one before (left half); the mouse wheel steps it too.
 *
 * Keys: 1-6 choose a page, r reads, w writes (twice), o opens, s saves,
 * n adds a memory or route, f finds stations, Delete removes the chosen
 * one, Up/Down move in the list, PageUp/PageDown scroll.
 *
 * STATIONS (on the memory page) opens the station finder, a second window
 * that searches the OurAirports database (data/README.md) by code or name,
 * or lists what is nearest a position, as a tree of facilities and their
 * frequencies. Chosen rows go into the memory book with ADD.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <limits.h>
#include <ctype.h>
#include <math.h>
#include <localdefs.h>
#include <services.h>
#include <graphics.h>

#include "radio.h"
#include "cfg.h"
#include "gpslog.h"
#include "stations.h"

#ifdef SHOT
/* test build: FTA850_SHOT=<file> takes a picture of the window after 3 s and
   quits; FTA850_PAGE=<n> chooses the page shown */
void screen_capture(void);
void screen_capture_name(const char *fn);
void screen_capture_window(FILE *f);
static const char *shot_file;
static long shot_at;
#endif

/* the panel is laid out in these units and scaled to the window. The
   window opens with the unit equal to a pixel at 96 DPI: the console font
   (console_points in ami.cfg, 11 points unless set, sized at the display's
   DPI) has a cell REFCELL units tall in the layout, so its real height on
   this display gives the pixels per unit. A bigger display or a bigger
   console_points gives a bigger window, and everything in it scales */
#define PANW    1180
#define PANH    740
#define REFCELL 15

/* pixels per layout unit on this display, from the console font of window f */
static double unit_px(FILE *f)
{
    double k = (double)ami_chrsizy(f) / REFCELL;

    return k < 0.5 ? 0.5 : k;
}

#define TICK_ID  1
#define TICK     500   /* 50 ms, in 100 us units */

/* pages */
enum { P_MEMORY, P_ROUTES, P_WEATHER, P_SETUP, P_GPS, P_IMAGE, NPAGES };
static const char *page_name[NPAGES] = { "MEMORY BOOK", "ROUTES", "WEATHER", "SETUP", "GPS", "IMAGE" };

/* things that can be clicked */
enum {
    H_NONE, H_READ, H_WRITE, H_OPEN, H_SAVE, H_SAVEAS, H_NEWIMG, H_CANCEL,
    H_TAB0, H_TAB1, H_TAB2, H_TAB3, H_TAB4, H_TAB5,
    H_SB_UP, H_SB_DN, H_SB_THUMB, H_SB_TRACK,
    H_MB_NEW, H_MB_DEL, H_MB_DELALL,
    H_RT_NEW, H_RT_DEL, H_RT_DELALL, H_RT_ADDVIA, H_RT_RMVIA, H_RT_CLRVIA,
    H_LOG_STAT, H_LOG_GET, H_LOG_ERASE, H_LOG_FIX, H_HOST_LOCATE,
    H_RESET0, H_RESET1, H_RESET2, H_RESET3, H_RESET4, H_RESET5,
    H_SQLKEY,
    H_STA_OPEN, H_STA_SEARCH, H_STA_NEAR, H_STA_ADD, H_STA_WPT, H_STA_EXPAND, H_STA_CLOSE,
    H_ROW0  = 100,   /* + visible row */
    H_ARROW0 = 340,  /* + visible row: a tree row's expand arrow */
    H_TEXT0 = 400,   /* + text field id */
    H_CYC0  = 600    /* + choice field id */
};

/* text fields */
enum { T_MB_TAG, T_MB_FREQ, T_MB_LAT, T_MB_LON, T_RT_NAME, T_STA_QUERY, T_MB_COURSE, T_WX0 = 10, T_GRP0 = 20, T_SET0 = 40 };
/* choice fields */
enum { C_MB_GROUP, C_MB_SCAN, C_MB_NS, C_MB_EW, C_MB_SHIFT, C_RT_GROUP, C_RT_FROM, C_RT_TO, C_RT_ADD,
       C_STA_GROUP, C_STA_SCAN, C_NEWMODEL, C_WX0 = 12, C_SET0 = 40 };

typedef struct { int x1, y1, x2, y2, id; } hitbox;

/* the clickable things of one window */
#define HITMAX 700
typedef struct { hitbox h[HITMAX]; int n; } hitset;

/* the window being drawn and its hit set: the main window, or the station
   finder while it is drawn or clicked */
static FILE   *gw;
static hitset  main_hs, pop_hs;
static hitset *hs = &main_hs;

static radio_state radio;

/* the image being edited */
static unsigned char img[CFG_SIZE];
static int   new_model = MODEL_FTA850;   /* what NEW creates: the radio's model once known */

/* the model of the image being edited (an FTA-850's when nothing is loaded) */
static const cfgmodel *mdl(void) { return cfg_model_or_default(img); }
static int nsettings_of(void) { int n; cfg_settings_of(img, &n); return n; }
static int   have_img, modified, factory_pending;
static char  filename[512];

/* view */
static int   page = P_MEMORY;
static int   sel = -1;          /* chosen memory entry */
static int   top;               /* first visible list row */
static int   rsel = -1;         /* chosen route */
static int   rtop;
static int   rt_add;            /* memory number the ADD VIA chooser shows */
static int   hextop;            /* first visible row of the hex dump */
static int   pressed = H_NONE;
static int   mx, my;
static int   drag_thumb, drag_off;
static int   confirm_id;
static long  confirm_until;
static char  note[256];
static long  note_until;
static char  logfile[512];
static int   log_points = -1;

/* the station finder window, see below */
#define POPWIN  2
#define POPW    920
#define POPH    606
#define POPROWS 19
#define PROWH   20
#define PLY1    84
#define SHITS   400       /* stations a search lists */
#define SROWS   4000      /* tree rows */
#define SPICKS  512       /* rows chosen at once */
static FILE  *popwf;
static int    pmx, pmy, ppressed = H_NONE, pmods, pdrag_thumb, pdrag_off;
static char   squery[64];
static stahit shits[SHITS];
static double sdist[SHITS];
static int    nshits, snear;                /* hits, and they are a nearest list */
static unsigned char sexp[SHITS];           /* hit shown with its frequencies */
typedef struct { short hit, k; unsigned char sel; } srow;   /* a tree row: k < 0 the facility */
static srow   srows[SROWS];
static int    nsrows, stop_, sanchor = -1;   /* rows, first shown, row of the last click */
typedef struct { short hit, k; } spick;
static spick  spicks[SPICKS];
static int    nspicks;
static int    sgroup, sscan;                 /* what added memories get */
static char   pnote[256];
static long   pnote_until;
static int    pop_dirty, pop_main_dirty;
static int    have_fix;                      /* the radio's last GPS fix is known */
static double fix_lat, fix_lon;
static unsigned long fix_time;               /* when it was logged, seconds since 1970 UTC */
static int    host_done, host_have;          /* this computer's position was asked for, and is known */
static double host_lat, host_lon, host_alt;  /* degrees, metres (shown in feet: aviation) */
static char   host_src[300];                 /* where the tool kit got it */

/* ask the tool kit where this computer is (ami.cfg, or a lookup of its
   internet address: it may take a few seconds the first time) */
static void host_locate(void)
{
    ami_long la, lo, al;

    la = ami_latitude();
    lo = ami_longitude();
    al = ami_altitude();
    ami_locsrc(host_src, sizeof host_src);
    host_done = 1;
    host_have = la || lo;
    host_lat = (double)la / (double)AMI_LONG_MAX * 90;
    host_lon = (double)lo / (double)AMI_LONG_MAX * 180;
    host_alt = (double)al / (double)AMI_LONG_MAX * 100000;
}

/* "37 19.583N 121 49.113W" for a position in degrees */
static void fmt_fix(double lat, double lon, char *out, size_t outlen)
{
    double la = fabs(lat), lo = fabs(lon);
    int lad = (int)la, lod = (int)lo;

    snprintf(out, outlen, "%d %06.3f%c %d %06.3f%c", lad, (la - lad) * 60, lat < 0 ? 'S' : 'N',
             lod, (lo - lod) * 60, lon < 0 ? 'W' : 'E');
}
static int    near_pending;                  /* a log read was started to find the fix */
static char   sta_dir[1200], sta_err[256];
static int    sta_n;
static void pset_note(const char *m);
static void pop_search(void);
static void pop_nearest_at(double lat, double lon, const char *what);

/* text editing */
static int   edit_id = -1;
static char  edit_buf[64];
static int   edit_len, edit_max;
static char  edit_shown[64];    /* the field's text while not editing, for Esc */

/* list geometry */
#define NROWS   20
#define ROWH    20
#define LY1     122
#define HEXROWS 30
#define HEXH    18
#define HY1     126

/* milliseconds since the first call. ami_clock wraps every 48 hours, so whole
   milliseconds are taken off the elapsed count each call and accumulated */
static long now_ms(void)
{
    static ami_long ref;
    static long     acc;
    static int      started;
    ami_long        ms;

    if (!started) { ref = ami_clock(); started = 1; }
    ms = ami_elapsed(ref) / 10;
    if (ms > 0) { ref += ms * 10; acc += ms; }
    return acc;
}

/* a file in the user's directory, or in a subdirectory of it when sub is given */
static void user_file(char *fn, int fnl, const char *sub, const char *name, const char *ext)
{
    char usr[256], dir[512];

    ami_getusr(usr, sizeof usr - 1);
    usr[sizeof usr - 1] = 0;
    if (sub) {
        ami_maknam(dir, sizeof dir - 1, usr, (char *)sub, "");
        dir[sizeof dir - 1] = 0;
    } else strcpy(dir, usr);
    ami_maknam(fn, fnl - 1, dir, (char *)name, (char *)ext);
    fn[fnl - 1] = 0;
}

static void local_time(ami_long t, int *y, int *mo, int *d, int *h, int *mi, int *sec)
{
    long days = (long)(t / 86400), rem = (long)(t % 86400), era, doe, yoe, doy, mp;

    if (rem < 0) { rem += 86400; days--; }
    *h = (int)(rem / 3600); *mi = (int)(rem / 60 % 60); *sec = (int)(rem % 60);
    days += 10957 + 719468;
    era = (days >= 0 ? days : days - 146096) / 146097;
    doe = days - era * 146097;
    yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    mp = (5 * doy + 2) / 153;
    *d = (int)(doy - (153 * mp + 2) / 5 + 1);
    *mo = (int)(mp < 10 ? mp + 3 : mp - 9);
    *y = (int)(yoe + era * 400 + (*mo <= 2));
}

static void set_note(const char *s)
{
    snprintf(note, sizeof note, "%s", s);
    note_until = now_ms() + 6000;
}

static void stamp(char *out, size_t outlen)
{
    int y, mo, d, h, mi, s;

    local_time(ami_local(ami_time()), &y, &mo, &d, &h, &mi, &s);
    snprintf(out, outlen, "%04d%02d%02d_%02d%02d%02d", y, mo, d, h, mi, s);
}

/* ---------------------------------------------------------------- drawing */

#define GRID 10

static int X(double x) { return (int)lround(x * GRID); }
static int Y(double y) { return (int)lround(y * GRID); }
static int S(double d) { int v = (int)lround(d * GRID); return v < 1 ? 1 : v; }

static ami_long cv(int v) { return (ami_long)v * (LONG_MAX / 255); }
static void color(int r, int g, int b) { ami_fcolorg(gw, cv(r), cv(g), cv(b)); }

static void addhit(double x1, double y1, double x2, double y2, int id)
{
    hitbox *b;

    if (hs->n >= HITMAX) return;
    b = &hs->h[hs->n++];
    b->x1 = X(x1); b->y1 = Y(y1);
    b->x2 = X(x2); b->y2 = Y(y2);
    b->id = id;
}

static int hit(int x, int y)
{
    int i;
    const hitbox *b;

    for (i = hs->n - 1; i >= 0; i--) {
        b = &hs->h[i];
        if (x >= b->x1 && x <= b->x2 && y >= b->y1 && y <= b->y2) return b->id;
    }
    return H_NONE;
}

static const hitbox *hitbox_of(int id)
{
    int i;

    for (i = 0; i < hs->n; i++) if (hs->h[i].id == id) return &hs->h[i];
    return NULL;
}

static void frect(double x1, double y1, double x2, double y2)
{
    ami_frect(gw, X(x1), Y(y1), X(x2), Y(y2));
}

static void frrect(double x1, double y1, double x2, double y2, double r)
{
    ami_frrect(gw, X(x1), Y(y1), X(x2), Y(y2), S(r), S(r));
}

static void rrect(double x1, double y1, double x2, double y2, double r, double w)
{
    ami_linewidth(gw, S(w));
    ami_rrect(gw, X(x1), Y(y1), X(x2), Y(y2), S(r), S(r));
}

static void rect(double x1, double y1, double x2, double y2, double w)
{
    ami_linewidth(gw, S(w));
    ami_rect(gw, X(x1), Y(y1), X(x2), Y(y2));
}

static void font(int face, double size, int bold)
{
    ami_font(gw, face);
    ami_fontsiz(gw, S(size));
    ami_bold(gw, bold);
}

/* width of a string in panel units */
static double tw(const char *s) { return (double)ami_strsiz(gw, s) / GRID; }

/* text with its top left at x,y; align -1 left, 0 centre, 1 right of x */
static void text(double x, double y, int align, const char *s)
{
    double w = tw(s);

    if (align == 0) x -= w / 2;
    else if (align > 0) x -= w;
    ami_cursorg(gw, X(x), Y(y));
    ami_binvis(gw);
    fputs(s, gw);
}

/* text centred in a box */
static void ctext(double x1, double y1, double x2, double y2, const char *s)
{
    double h = (double)ami_chrsizy(gw) / GRID;

    text((x1 + x2) / 2, (y1 + y2) / 2 - h / 2, 0, s);
}

/* text at the left of a box, vertically centred */
static void ltext(double x1, double y1, double y2, const char *s)
{
    double h = (double)ami_chrsizy(gw) / GRID;

    text(x1, (y1 + y2) / 2 - h / 2, -1, s);
}

/* cut a string to fit a width */
static void fit(const char *s, double w, char *out, size_t outlen)
{
    size_t n = strlen(s);

    snprintf(out, outlen, "%s", s);
    while (n > 1 && tw(out) > w) out[--n] = 0;
}

static int busy(void) { return radio.op != OP_NONE; }

/* a push button */
static void button(double x1, double y1, double x2, double y2, const char *label, int id, int enabled, int lit)
{
    int down = pressed == id && enabled;
    int armed = confirm_id == id && now_ms() < confirm_until;

    color(14, 14, 16);
    frrect(x1, y1 + 2, x2, y2 + 2, 5);
    if (!enabled) color(58, 60, 66);
    else if (armed) color(190, 70, 50);
    else if (lit) color(60, 110, 180);
    else color(down ? 54 : 78, down ? 58 : 82, down ? 66 : 92);
    frrect(x1, y1 + (down ? 2 : 0), x2, y2 + (down ? 2 : 0), 5);
    font(AMI_FONT_SIGN, 12, TRUE);
    color(enabled ? 240 : 130, enabled ? 240 : 130, enabled ? 245 : 140);
    ctext(x1, y1 + (down ? 2 : 0), x2, y2 + (down ? 2 : 0), armed ? "CONFIRM" : label);
    if (enabled) addhit(x1, y1, x2, y2 + 2, id);
}

/* a text field: shows its text, or the edit buffer while being edited */
static void textfield(int id, double x1, double y1, double x2, double y2, const char *s, int enabled)
{
    int editing = edit_id == id;
    char buf[80];

    color(editing ? 40 : 24, editing ? 44 : 26, editing ? 60 : 30);
    frrect(x1, y1, x2, y2, 3);
    color(editing ? 120 : 70, editing ? 160 : 74, editing ? 230 : 84);
    rrect(x1, y1, x2, y2, 3, 1);
    font(AMI_FONT_TERM, 13, FALSE);
    if (editing) {
        snprintf(buf, sizeof buf, "%s_", edit_buf);
        color(255, 255, 255);
    } else {
        fit(s, x2 - x1 - 10, buf, sizeof buf);
        color(enabled ? 230 : 120, enabled ? 230 : 120, enabled ? 235 : 125);
    }
    ltext(x1 + 5, y1, y2, buf);
    if (enabled) addhit(x1, y1, x2, y2, H_TEXT0 + id);
}

/* a choice field: a value with arrows at its ends */
static void cycfield(int id, double x1, double y1, double x2, double y2, const char *s, int enabled)
{
    char buf[80];

    color(30, 34, 44);
    frrect(x1, y1, x2, y2, 3);
    color(80, 86, 100);
    rrect(x1, y1, x2, y2, 3, 1);
    font(AMI_FONT_SIGN, 12, FALSE);
    color(enabled ? 150 : 90, enabled ? 160 : 90, enabled ? 185 : 95);
    ltext(x1 + 4, y1, y2, "<");
    text(x2 - 4, (y1 + y2) / 2 - (double)ami_chrsizy(gw) / GRID / 2, 1, ">");
    font(AMI_FONT_SIGN, 12, TRUE);
    fit(s, x2 - x1 - 26, buf, sizeof buf);
    color(enabled ? 235 : 120, enabled ? 235 : 120, enabled ? 240 : 125);
    ctext(x1, y1, x2, y2, buf);
    if (enabled) addhit(x1, y1, x2, y2, H_CYC0 + id);
}

static void label(double x, double y1, double y2, const char *s)
{
    font(AMI_FONT_SIGN, 12, TRUE);
    color(160, 168, 185);
    ltext(x, y1, y2, s);
}

static void help(double x, double y, const char *s)
{
    font(AMI_FONT_SIGN, 11, FALSE);
    color(130, 138, 155);
    text(x, y, -1, s);
}

/* a vertical scroll bar for a list of n rows, vis visible, first at top */
static void scrollbar(double x1, double y1, double x2, double y2, int n, int vis, int first, int idbase)
{
    double th, ty, track = y2 - y1 - 32;

    color(30, 32, 38);
    frect(x1, y1, x2, y2);
    button(x1, y1, x2, y1 + 14, "^", idbase, n > vis, 0);
    button(x1, y2 - 14, x2, y2, "v", idbase + 1, n > vis, 0);
    if (n <= vis) return;
    th = track * vis / n;
    if (th < 20) th = 20;
    ty = y1 + 16 + (track - th) * first / (n - vis);
    addhit(x1, y1 + 16, x2, y2 - 16, idbase + 3);
    color(110, 116, 130);
    frrect(x1 + 2, ty, x2 - 2, ty + th, 3);
    addhit(x1, ty, x2, ty + th, idbase + 2);
}

/* "12 KRHV-ATIS" for a memory number, or "(none)" */
static void memname(int addr, char *out, size_t outlen)
{
    int i = cfg_mb_find(img, addr);
    mbentry e;

    if (i < 0) { snprintf(out, outlen, addr ? "%d (missing)" : "(none)", addr); return; }
    cfg_mb_get(img, i, &e);
    snprintf(out, outlen, "%d %s", addr, e.tag);
}

/* ---------------------------------------------------------------- the radio */

static void draw_radio(void)
{
    const double bx1 = 58, bx2 = 252, by1 = 56, by2 = 706;
    const double lx1 = 76, ly1 = 108, lx2 = 234, ly2 = 300;
    static const char *keys[6][3] = {
        { "MENU", "^", "ENT" }, { "<", "v", ">" }, { "1", "2", "3" },
        { "4", "5", "6" }, { "7", "8", "9" }, { "121.5", "0", "WX" }
    };
    int r, c, on = radio.connected || radio.mode == RM_NORMAL;
    int sqlblink = radio.mode == RM_NORMAL && !radio.connected && (now_ms() / 500) % 2;
    char buf[160];
    double y;

    /* antenna */
    color(12, 12, 14);
    frrect(74, 6, 94, 72, 6);
    color(40, 40, 44);
    for (r = 0; r < 5; r++) frect(76, 12 + r * 11, 92, 15 + r * 11);
    /* power button on top */
    color(24, 24, 26);
    frrect(190, 40, 236, 60, 5);
    color(200, 60, 50);
    frrect(196, 44, 230, 56, 3);
    /* body */
    color(18, 18, 20);
    frrect(bx1 - 2, by1 - 2, bx2 + 2, by2 + 2, 24);
    color(52, 54, 58);
    frrect(bx1, by1, bx2, by2, 22);
    color(64, 66, 72);
    frrect(bx1 + 8, by1 + 8, bx2 - 8, by2 - 8, 16);
    /* side controls: PTT and SQL on the left, the DATA jack on the right */
    color(28, 28, 30);
    frrect(bx1 - 10, 150, bx1 + 4, 240, 5);
    color(44, 44, 48);
    frrect(bx1 - 8, 156, bx1 + 2, 234, 4);
    font(AMI_FONT_SIGN, 9, FALSE);
    color(150, 152, 160);
    text(bx1 - 14, 244, -1, "PTT");
    color(28, 28, 30);
    frrect(bx1 - 10, 262, bx1 + 4, 300, 5);
    if (sqlblink) color(220, 150, 40);
    else color(60, 62, 70);
    frrect(bx1 - 8, 266, bx1 + 2, 296, 4);
    color(150, 152, 160);
    text(bx1 - 14, 304, -1, "SQL");
    addhit(bx1 - 12, 260, bx1 + 6, 314, H_SQLKEY);
    color(28, 28, 30);
    frrect(bx2 - 4, 296, bx2 + 10, 324, 4);
    color(70, 110, 190);
    frect(bx2, 304, bx2 + 6, 316);
    color(150, 152, 160);
    text(bx2 - 2, 328, -1, "DATA");

    font(AMI_FONT_SIGN, 15, TRUE);
    color(230, 230, 235);
    text((bx1 + bx2) / 2, 72, 0, "YAESU");
    font(AMI_FONT_SIGN, 9, FALSE);
    color(170, 172, 180);
    text((bx1 + bx2) / 2, 92, 0, "AIR BAND TRANSCEIVER");

    /* the colour LCD */
    color(10, 10, 12);
    frrect(lx1 - 6, ly1 - 6, lx2 + 6, ly2 + 6, 6);
    if (radio.connected) color(16, 32, 70);
    else if (on) color(28, 34, 60);
    else color(26, 28, 34);
    frect(lx1, ly1, lx2, ly2);
    /* status strip */
    color(radio.connected ? 40 : 36, radio.connected ? 70 : 46, radio.connected ? 130 : 70);
    frect(lx1, ly1, lx2, ly1 + 18);
    color(230, 235, 245);
    font(AMI_FONT_SIGN, 11, TRUE);
    {
        const cfgmodel *rm = cfg_model_by_code(radio.model);

        ltext(lx1 + 6, ly1, ly1 + 18, rm ? rm->name : have_img ? mdl()->name : "FTA");
    }
    rect(lx2 - 26, ly1 + 5, lx2 - 8, ly1 + 13, 1);
    frect(lx2 - 8, ly1 + 7, lx2 - 6, ly1 + 11);
    frect(lx2 - 24, ly1 + 7, lx2 - 12, ly1 + 11);
    font(AMI_FONT_SIGN, 15, TRUE);
    if (radio.connected) color(120, 230, 150); else color(230, 220, 120);
    if (radio.sim) ctext(lx1, ly1 + 30, lx2, ly1 + 56, "SIMULATED");
    else if (radio.connected) ctext(lx1, ly1 + 30, lx2, ly1 + 56, "CP MODE");
    else if (radio.mode == RM_NORMAL) ctext(lx1, ly1 + 30, lx2, ly1 + 56, "NOT IN CP MODE");
    else { color(170, 176, 190); ctext(lx1, ly1 + 30, lx2, ly1 + 56, "NO RADIO"); }
    font(AMI_FONT_SIGN, 11, FALSE);
    color(220, 226, 240);
    y = ly1 + 62;
    if (radio.connected) {
        snprintf(buf, sizeof buf, "Firmware %s", radio.version);
        ctext(lx1, y, lx2, y + 14, buf);
        y += 16;
    }
    if (radio.sim) snprintf(buf, sizeof buf, "simulator");
    else if (radio.port[0]) {
        const char *p = strrchr(radio.port, '/');

        snprintf(buf, sizeof buf, "%s", p ? p + 1 : radio.port);
    } else snprintf(buf, sizeof buf, "no port");
    fit(buf, lx2 - lx1 - 8, buf, sizeof buf);
    ctext(lx1, y, lx2, y + 14, buf);
    y += 20;
    if (busy()) {
        int pct = radio.total > 0 ? radio.progress * 100 / radio.total : 0;

        if (pct > 100) pct = 100;
        fit(radio_opname(radio.op), lx2 - lx1 - 8, buf, sizeof buf);
        ctext(lx1, y, lx2, y + 14, buf);
        color(90, 110, 160);
        rect(lx1 + 10, y + 18, lx2 - 10, y + 30, 1);
        color(120, 230, 150);
        frect(lx1 + 12, y + 20, lx1 + 12 + (lx2 - lx1 - 24) * pct / 100, y + 28);
        color(220, 226, 240);
        snprintf(buf, sizeof buf, "%d%%", pct);
        ctext(lx1, y + 32, lx2, y + 46, buf);
    } else if (!radio.connected && radio.mode == RM_NORMAL) {
        ctext(lx1, y, lx2, y + 14, "Switch off, then hold");
        ctext(lx1, y + 14, lx2, y + 28, "SQL and switch on");
    } else if (!radio.connected && !radio.sim) {
        ctext(lx1, y, lx2, y + 14, "Plug in the radio and");
        ctext(lx1, y + 14, lx2, y + 28, "start it in CP mode");
    } else if (have_img) {
        snprintf(buf, sizeof buf, "%d memories, %d routes", cfg_mb_count(img), cfg_rt_count(img));
        ctext(lx1, y, lx2, y + 14, buf);
        if (modified) ctext(lx1, y + 14, lx2, y + 28, "edited, not written");
    } else {
        ctext(lx1, y, lx2, y + 14, "Press READ RADIO");
    }

    /* speaker */
    color(30, 30, 34);
    for (r = 0; r < 3; r++) frrect(lx1 + 10, 314 + r * 8, lx2 - 10, 317 + r * 8, 1);

    /* keypad */
    for (r = 0; r < 6; r++)
        for (c = 0; c < 3; c++) {
            double kx = 82 + c * 52, ky = 348 + r * 42;
            int menu = r == 0 && c == 0;

            color(16, 16, 18);
            frrect(kx, ky + 2, kx + 46, ky + 30, 5);
            color(menu ? 90 : 76, menu ? 96 : 80, menu ? 110 : 90);
            frrect(kx, ky, kx + 46, ky + 28, 5);
            font(AMI_FONT_SIGN, menu || (r == 5 && c == 0) ? 10 : 12, TRUE);
            color(240, 240, 245);
            ctext(kx, ky, kx + 46, ky + 28, keys[r][c]);
        }
    font(AMI_FONT_SIGN, 12, TRUE);
    color(230, 230, 235);
    {
        const cfgmodel *rm = cfg_model_by_code(radio.model);

        text((bx1 + bx2) / 2, 618, 0, rm ? rm->longname : have_img ? mdl()->longname : "FTA-850L");
    }
    font(AMI_FONT_SIGN, 9, FALSE);
    color(170, 172, 180);
    {
        const cfgmodel *rm = cfg_model_by_code(radio.model);

        if (!rm && have_img) rm = mdl();
        text((bx1 + bx2) / 2, 636, 0, !rm || rm->code == MODEL_FTA850 ? "NAV COM GPS BLUETOOTH" : rm->has_gps ? "NAV COM GPS" : rm->has_nav ? "NAV COM" : "COM");
    }
    /* microphone */
    color(30, 30, 34);
    for (r = 0; r < 3; r++) frrect(120, 662 + r * 8, 190, 665 + r * 8, 1);
}

/* ---------------------------------------------------------------- pages */

#define RX1 310
#define RX2 1166
#define PY1 96
#define PY2 690

static void draw_memory_page(void)
{
    static const struct { const char *h; double x, w; } col[8] = {
        { "No", 318, 36 }, { "Name", 360, 150 }, { "Freq MHz", 516, 72 }, { "Group", 594, 100 },
        { "Scan", 700, 44 }, { "Latitude", 750, 110 }, { "Longitude", 866, 120 }, { "Shift", 1000, 44 }
    };
    int n = cfg_mb_count(img), i, r, c;
    const cfgmodel *m = mdl();
    mbentry e;
    char buf[80], g[20], f[20], la[20], lo[20];
    double y;

    color(38, 42, 52);
    frect(RX1 + 4, 102, RX2 - 4, 120);
    font(AMI_FONT_SIGN, 12, TRUE);
    color(190, 200, 220);
    for (c = 0; c < 8; c++) if (m->has_pos || (c != 5 && c != 6)) ltext(col[c].x, 102, 120, col[c].h);
    if (top > n - NROWS) top = n - NROWS;
    if (top < 0) top = 0;
    for (r = 0; r < NROWS; r++) {
        i = top + r;
        y = LY1 + r * ROWH;
        if (i >= n) {
            if (i == 0) {
                font(AMI_FONT_SIGN, 12, TRUE);
                color(140, 146, 160);
                ltext(360, y, y + ROWH, have_img ? "No memories. NEW adds one." :
                      "Nothing loaded: READ RADIO, OPEN a .dat file, or NEW for a blank image.");
            }
            break;
        }
        if (i == sel) color(40, 70, 130);
        else color(r % 2 ? 24 : 28, r % 2 ? 26 : 30, r % 2 ? 32 : 36);
        frect(RX1 + 4, y, 1128, y + ROWH);
        addhit(RX1 + 4, y, 1128, y + ROWH, H_ROW0 + r);
        cfg_mb_get(img, i, &e);
        font(AMI_FONT_TERM, 13, FALSE);
        color(i == sel ? 255 : 225, i == sel ? 255 : 228, i == sel ? 255 : 235);
        snprintf(buf, sizeof buf, "%3d", e.addr);
        ltext(col[0].x, y, y + ROWH, buf);
        ltext(col[1].x, y, y + ROWH, e.tag);
        cfg_fmt_freq(e.freq, f, sizeof f);
        ltext(col[2].x, y, y + ROWH, f);
        cfg_group_name(img, e.group, g, sizeof g);
        fit(g, col[3].w, buf, sizeof buf);
        ltext(col[3].x, y, y + ROWH, buf);
        ltext(col[4].x, y, y + ROWH, e.scan ? "on" : "");
        if (e.haspos) {
            cfg_fmt_pos(1, e.lat_deg, e.lat_mmm, la, sizeof la);
            snprintf(buf, sizeof buf, "%s %c", la, e.ns);
            ltext(col[5].x, y, y + ROWH, buf);
            cfg_fmt_pos(0, e.lon_deg, e.lon_mmm, lo, sizeof lo);
            snprintf(buf, sizeof buf, "%s %c", lo, e.ew);
            ltext(col[6].x, y, y + ROWH, buf);
        }
        ltext(col[7].x, y, y + ROWH, e.shift ? "on" : "");
    }
    scrollbar(1132, LY1, 1150, LY1 + NROWS * ROWH, n, NROWS, top, H_SB_UP);

    /* the editor */
    color(24, 26, 32);
    frrect(RX1 + 4, 532, RX2 - 4, PY2 - 4, 6);
    color(60, 64, 76);
    rrect(RX1 + 4, 532, RX2 - 4, PY2 - 4, 6, 1);
    if (sel >= 0 && sel < n) {
        cfg_mb_get(img, sel, &e);
        cfg_fmt_freq(e.freq, f, sizeof f);
        cfg_group_name(img, e.group, g, sizeof g);
        if (e.haspos) {
            cfg_fmt_pos(1, e.lat_deg, e.lat_mmm, la, sizeof la);
            cfg_fmt_pos(0, e.lon_deg, e.lon_mmm, lo, sizeof lo);
        } else la[0] = lo[0] = 0;
        snprintf(buf, sizeof buf, "Memory %d (entry %d of %d)", e.addr, sel + 1, n);
    } else {
        memset(&e, 0, sizeof e);
        f[0] = la[0] = lo[0] = 0;
        strcpy(g, "");
        snprintf(buf, sizeof buf, "No memory chosen");
    }
    font(AMI_FONT_SIGN, 12, TRUE);
    color(190, 200, 220);
    ltext(324, 538, 556, buf);
    {
        int on = sel >= 0 && sel < n && !busy();

        label(324, 562, 584, "Name");
        textfield(T_MB_TAG, 370, 562, 540, 584, e.tag, on);
        label(556, 562, 584, "Freq");
        textfield(T_MB_FREQ, 600, 562, 690, 584, f, on);
        label(706, 562, 584, "Group");
        cycfield(C_MB_GROUP, 760, 562, 890, 584, g, on);
        label(906, 562, 584, "Scan");
        cycfield(C_MB_SCAN, 950, 562, 1010, 584, e.scan ? "On" : "Off", on);
        label(1026, 562, 584, "Shift");
        cycfield(C_MB_SHIFT, 1070, 562, 1140, 584, e.shift ? "On" : "Off", on);

        if (m->has_pos) {
            label(324, 596, 618, "Lat");
            textfield(T_MB_LAT, 366, 596, 470, 618, la, on);
            snprintf(buf, sizeof buf, "%c", e.ns ? e.ns : 'N');
            cycfield(C_MB_NS, 474, 596, 514, 618, buf, on && e.haspos);
            label(526, 596, 618, "Lon");
            textfield(T_MB_LON, 562, 596, 672, 618, lo, on);
            snprintf(buf, sizeof buf, "%c", e.ew ? e.ew : 'W');
            cycfield(C_MB_EW, 676, 596, 716, 618, buf, on && e.haspos);
        }
        if (m->has_course) {
            if (e.course >= 0) snprintf(buf, sizeof buf, "%03d.%02d", e.course / 100, e.course % 100);
            else buf[0] = 0;
            label(728, 596, 618, "RWY");
            textfield(T_MB_COURSE, 764, 596, 834, 618, buf, on);
        }
        button(850, 594, 940, 620, "STATIONS", H_STA_OPEN, !busy(), 0);
        button(950, 594, 1010, 620, "NEW", H_MB_NEW, have_img && !busy() && n < m->mb_max, 0);
        button(1018, 594, 1086, 620, "DELETE", H_MB_DEL, on, 0);
        button(1094, 594, 1150, 620, "ALL", H_MB_DELALL, have_img && !busy() && n > 0, 0);
    }
    help(324, 634, "Click a field and type; Enter keeps it, Esc drops it, Tab moves on. Choice fields step on a click or the wheel.");
    help(324, 650, "Entries stay sorted by name and keep their number. STATIONS looks up airports and navaids to add. Positions: 37 19.583, 37 19 35 or 37.3264.");
    if (sel >= 0 && sel < n) {
        char err[128];

        if (!cfg_mb_valid(img, &e, cfg_get(img, cfg_sid833(img)) == 1, err, sizeof err)) {
            font(AMI_FONT_SIGN, 11, FALSE);
            color(240, 150, 80);
            text(324, 668, -1, err);
        }
    }
}

static void draw_routes_page(void)
{
    static const struct { const char *h; double x, w; } col[6] = {
        { "No", 318, 30 }, { "Name", 356, 150 }, { "Group", 512, 100 }, { "Start", 620, 150 },
        { "Destination", 780, 150 }, { "Via points", 940, 90 }
    };
    int n = cfg_rt_count(img), i, r, c, k;
    route rt;
    char buf[200], g[20], nm[40];
    double y;

    color(38, 42, 52);
    frect(RX1 + 4, 102, RX2 - 4, 120);
    font(AMI_FONT_SIGN, 12, TRUE);
    color(190, 200, 220);
    for (c = 0; c < 6; c++) ltext(col[c].x, 102, 120, col[c].h);
    if (rtop > n - NROWS) rtop = n - NROWS;
    if (rtop < 0) rtop = 0;
    for (r = 0; r < NROWS; r++) {
        i = rtop + r;
        y = LY1 + r * ROWH;
        if (i >= n) {
            if (i == 0) {
                font(AMI_FONT_SIGN, 12, TRUE);
                color(140, 146, 160);
                ltext(356, y, y + ROWH, have_img ? "No routes. NEW adds one; it needs memories with positions." : "Nothing loaded.");
            }
            break;
        }
        if (i == rsel) color(40, 70, 130);
        else color(r % 2 ? 24 : 28, r % 2 ? 26 : 30, r % 2 ? 32 : 36);
        frect(RX1 + 4, y, 1128, y + ROWH);
        addhit(RX1 + 4, y, 1128, y + ROWH, H_ROW0 + r);
        cfg_rt_get(img, i, &rt);
        font(AMI_FONT_TERM, 13, FALSE);
        color(i == rsel ? 255 : 225, i == rsel ? 255 : 228, i == rsel ? 255 : 235);
        snprintf(buf, sizeof buf, "%2d", i + 1);
        ltext(col[0].x, y, y + ROWH, buf);
        ltext(col[1].x, y, y + ROWH, rt.name);
        cfg_group_name(img, rt.group, g, sizeof g);
        fit(g, col[2].w, buf, sizeof buf);
        ltext(col[2].x, y, y + ROWH, buf);
        memname(rt.from, nm, sizeof nm);
        fit(nm, col[3].w, buf, sizeof buf);
        ltext(col[3].x, y, y + ROWH, buf);
        memname(rt.to, nm, sizeof nm);
        fit(nm, col[4].w, buf, sizeof buf);
        ltext(col[4].x, y, y + ROWH, buf);
        snprintf(buf, sizeof buf, "%d", rt.nvia);
        ltext(col[5].x, y, y + ROWH, buf);
    }
    scrollbar(1132, LY1, 1150, LY1 + NROWS * ROWH, n, NROWS, rtop, H_SB_UP);

    /* the editor */
    color(24, 26, 32);
    frrect(RX1 + 4, 532, RX2 - 4, PY2 - 4, 6);
    color(60, 64, 76);
    rrect(RX1 + 4, 532, RX2 - 4, PY2 - 4, 6, 1);
    if (rsel >= 0 && rsel < n) {
        cfg_rt_get(img, rsel, &rt);
        snprintf(buf, sizeof buf, "Route %d of %d", rsel + 1, n);
    } else {
        memset(&rt, 0, sizeof rt);
        snprintf(buf, sizeof buf, "No route chosen");
    }
    font(AMI_FONT_SIGN, 12, TRUE);
    color(190, 200, 220);
    ltext(324, 538, 556, buf);
    {
        int on = rsel >= 0 && rsel < n && !busy();

        label(324, 562, 584, "Name");
        textfield(T_RT_NAME, 370, 562, 540, 584, rt.name, on);
        label(556, 562, 584, "Group");
        cfg_group_name(img, rt.group, g, sizeof g);
        cycfield(C_RT_GROUP, 610, 562, 740, 584, rt.group ? g : "", on);
        label(756, 562, 584, "Start");
        memname(rt.from, nm, sizeof nm);
        cycfield(C_RT_FROM, 800, 562, 960, 584, nm, on);
        label(976, 562, 584, "Dest");
        memname(rt.to, nm, sizeof nm);
        cycfield(C_RT_TO, 1012, 562, 1150, 584, nm, on);

        label(324, 596, 618, "Via");
        buf[0] = 0;
        for (k = 0; k < rt.nvia; k++) {
            memname(rt.via[k], nm, sizeof nm);
            snprintf(buf + strlen(buf), sizeof buf - strlen(buf), "%s%s", k ? " > " : "", nm);
        }
        if (!rt.nvia) snprintf(buf, sizeof buf, "(direct)");
        font(AMI_FONT_TERM, 12, FALSE);
        color(225, 228, 235);
        fit(buf, 560, nm, sizeof nm);
        {
            char line[200];

            fit(buf, 590, line, sizeof line);
            ltext(370, 596, 618, line);
        }
        memname(rt_add, nm, sizeof nm);
        cycfield(C_RT_ADD, 800, 596, 960, 618, rt_add ? nm : "(choose)", on);
        button(966, 594, 1010, 620, "ADD", H_RT_ADDVIA, on && rt.nvia < VIA_MAX && rt_add, 0);
        button(1018, 594, 1086, 620, "REMOVE", H_RT_RMVIA, on && rt.nvia > 0, 0);
        button(1094, 594, 1150, 620, "CLEAR", H_RT_CLRVIA, on && rt.nvia > 0, 0);
        button(950, 630, 1010, 656, "NEW", H_RT_NEW, have_img && !busy() && n < ROUTE_MAX, 0);
        button(1018, 630, 1086, 656, "DELETE", H_RT_DEL, on, 0);
        button(1094, 630, 1150, 656, "ALL", H_RT_DELALL, have_img && !busy() && n > 0, 0);
    }
    help(324, 634, "A route goes from a start memory to a destination, through up to 30 via points. Start, Dest and the");
    help(324, 650, "ADD chooser step through the memories that have a position. REMOVE takes off the last via point.");
    if (rsel >= 0 && rsel < n) {
        char err[128];

        if (!cfg_rt_valid(img, &rt, err, sizeof err)) {
            font(AMI_FONT_SIGN, 11, FALSE);
            color(240, 150, 80);
            text(324, 668, -1, err);
        }
    }
}

static void draw_weather_page(void)
{
    int i, scan;
    char tag[20], buf[40];
    double y;

    color(38, 42, 52);
    frect(RX1 + 4, 102, RX2 - 4, 120);
    font(AMI_FONT_SIGN, 12, TRUE);
    color(190, 200, 220);
    ltext(330, 102, 120, "Channel");
    ltext(420, 102, 120, "Frequency MHz");
    ltext(560, 102, 120, "Name");
    ltext(800, 102, 120, "Memory scan");
    for (i = 0; i < WX_MAX; i++) {
        y = 130 + i * 32;
        cfg_wx_get(img, i, tag, &scan);
        font(AMI_FONT_TERM, 13, FALSE);
        color(225, 228, 235);
        snprintf(buf, sizeof buf, "WX%02d", i + 1);
        ltext(330, y, y + 24, buf);
        snprintf(buf, sizeof buf, "%d.%03d", cfg_wx_freq[i] / 1000, cfg_wx_freq[i] % 1000);
        ltext(420, y, y + 24, buf);
        textfield(T_WX0 + i, 560, y, 760, y + 24, tag, have_img && !busy());
        cycfield(C_WX0 + i, 800, y, 880, y + 24, scan ? "On" : "Off", have_img && !busy());
    }
    help(330, 470, "The ten NOAA weather channels have fixed frequencies; the radio shows the name.");
    help(330, 486, "Memory scan includes a channel in the weather scan.");
}

static void draw_setting(int sid, double lx, double fx1, double fx2, double y)
{
    char buf[40];
    int v = cfg_get(img, sid), on = have_img && !busy();

    label(lx, y, y + 22, cfg_settings_of(img, NULL)[sid].label);
    if (cfg_settings_of(img, NULL)[sid].kind == K_FREQ) {
        cfg_fmt_freq(v, buf, sizeof buf);
        textfield(T_SET0 + sid, fx1, y, fx2, y + 22, buf, on);
    } else cycfield(C_SET0 + sid, fx1, y, fx2, y + 22, cfg_choice(img, sid, v, buf, sizeof buf), on);
}

static void draw_setup_page(void)
{
    int i, r0 = 0, r1 = 0;
    char g[20];

    font(AMI_FONT_SIGN, 13, TRUE);
    color(190, 200, 220);
    text(324, 104, -1, "CONFIGURATION");
    text(750, 104, -1, "COMM SETUP");
    for (i = 0; i < nsettings_of(); i++) {
        if (cfg_settings_of(img, NULL)[i].page == 0 && cfg_setting_shown(img, i)) draw_setting(i, 324, 480, 690, 126 + r0++ * 28);
        else if (cfg_settings_of(img, NULL)[i].page == 1 && cfg_setting_shown(img, i)) draw_setting(i, 750, 940, 1150, 126 + r1++ * 24);
    }
    font(AMI_FONT_SIGN, 13, TRUE);
    color(190, 200, 220);
    text(324, 340, -1, "MEMORY GROUPS");
    for (i = 0; i < NGROUPS; i++) {
        double y = 362 + (i % 5) * 28, x = 324 + (i / 5) * 200;
        char l[12];

        snprintf(l, sizeof l, "Group %d", i + 1);
        label(x, y, y + 22, l);
        cfg_group_name(img, i + 1, g, sizeof g);
        if (!strcmp(g, "(blank)")) g[0] = 0;
        textfield(T_GRP0 + i, x + 64, y, x + 186, y + 22, g, have_img && !busy() && i + 1 >= mdl()->group_edit_from);
    }
    font(AMI_FONT_SIGN, 13, TRUE);
    color(190, 200, 220);
    text(324, 516, -1, "RESET (then WRITE RADIO)");
    for (i = 0; i < cfg_nresets(img); i++) {
        char l[24];

        snprintf(l, sizeof l, "%s", cfg_reset_name(img, i, 1));
        for (r0 = 0; l[r0]; r0++) l[r0] = (char)toupper((unsigned char)l[r0]);
        button(324 + i * 70, 536, 324 + i * 70 + 66, 562, l, H_RESET0 + i, have_img && !busy(), 0);
    }
    help(324, 574, "A reset puts the factory values of that part into the image; FACTORY also");
    help(324, 590, "clears the Bluetooth pairings (FTA-850) and erases the GPS log when written.");
    help(324, 606, "Dual watch and split frequencies take a COMM or NAVI channel, or nothing");
    help(324, 622, "to switch them off. Group names are 10 characters.");
}

static void draw_gps_page(void)
{
    int i, r = 0;
    char buf[640];

    font(AMI_FONT_SIGN, 13, TRUE);
    color(190, 200, 220);
    text(324, 104, -1, mdl()->has_gps ? "GPS SETUP" : "GPS SETUP: this model has no GPS");
    for (i = 0; i < nsettings_of(); i++)
        if (cfg_settings_of(img, NULL)[i].page == 2 && cfg_setting_shown(img, i)) draw_setting(i, 324, 500, 720, 126 + r++ * 28);

    color(24, 26, 32);
    frrect(760, 100, RX2 - 4, 450, 6);
    color(60, 64, 76);
    rrect(760, 100, RX2 - 4, 450, 6, 1);
    font(AMI_FONT_SIGN, 13, TRUE);
    color(190, 200, 220);
    text(776, 110, -1, "GPS LOGGER");
    font(AMI_FONT_SIGN, 11, FALSE);
    color(200, 205, 215);
    if (!mdl()->has_gps) {
        snprintf(buf, sizeof buf, "The %s has no GPS, so no logger and no position of its own.", mdl()->longname);
        text(776, 136, -1, buf);
    } else {
    if (radio.log_records > 0 || (radio.log_pct == 0 && radio.log_records == 0 && log_points == 0))
        snprintf(buf, sizeof buf, "The radio holds %d positions (%d%% of the log memory).", radio.log_records, radio.log_pct);
    else snprintf(buf, sizeof buf, "STATUS asks the radio how much it has logged.");
    text(776, 136, -1, buf);
    text(776, 156, -1, "DOWNLOAD saves the track; the file name's extension chooses");
    text(776, 172, -1, "the format: .gpx, .kml, .nmea or .csv.");
    text(776, 188, -1, "ERASE (pressed twice) clears the radio's log.");
    button(776, 216, 870, 244, "STATUS", H_LOG_STAT, radio.connected && !busy(), 0);
    button(880, 216, 990, 244, "DOWNLOAD", H_LOG_GET, radio.connected && !busy(), 0);
    button(1000, 216, 1090, 244, "ERASE", H_LOG_ERASE, radio.connected && !busy(), 0);
    if (log_points >= 0) {
        const char *p = strrchr(logfile, '/');

        snprintf(buf, sizeof buf, "Last download: %d positions to %s", log_points, p ? p + 1 : logfile);
        color(120, 230, 120);
        text(776, 262, -1, buf);
    }
    font(AMI_FONT_SIGN, 13, TRUE);
    color(190, 200, 220);
    text(776, 290, -1, "RADIO'S LAST POSITION");
    font(AMI_FONT_SIGN, 11, FALSE);
    if (have_fix) {
        char pos[48], when[48];
        int y, mo, d, h, mi, sec;

        fmt_fix(fix_lat, fix_lon, pos, sizeof pos);
        local_time((ami_long)fix_time, &y, &mo, &d, &h, &mi, &sec);
        snprintf(when, sizeof when, "%04d-%02d-%02d %02d:%02d", y, mo, d, h, mi);
        color(120, 230, 120);
        snprintf(buf, sizeof buf, "%s, logged %s (the newest point of the GPS log)", pos, when);
        text(776, 312, -1, buf);
    } else {
        color(200, 205, 215);
        text(776, 312, -1, log_points == 0 || (radio.log_pct == 0 && radio.log_records == 0 && log_points >= 0)
             ? "Unknown: the GPS log is empty. Switch LOGGER on in the radio's menu and get a fix."
             : "Unknown until the log is read: POSITION fetches it without saving a file.");
    }
    button(776, 334, 870, 362, "POSITION", H_LOG_FIX, radio.connected && !busy(), 0);
    help(776, 380, "The logger records the position at the interval set on the left while LOGGER");
    help(776, 396, "is switched on in the radio's menu. The log is read at the GPS module's own");
    help(776, 412, "speed, about a minute for a full memory. The station finder's NEAREST uses it.");
    }

    /* this computer */
    color(24, 26, 32);
    frrect(760, 462, RX2 - 4, 616, 6);
    color(60, 64, 76);
    rrect(760, 462, RX2 - 4, 616, 6, 1);
    font(AMI_FONT_SIGN, 13, TRUE);
    color(190, 200, 220);
    text(776, 472, -1, "THIS COMPUTER'S POSITION");
    font(AMI_FONT_SIGN, 11, FALSE);
    if (host_done) {
        /* the tool kit's account, "position from ..., altitude from ...", a
           line each */
        char src[300], *alt = strstr(host_src, ", altitude");

        snprintf(src, sizeof src, "%s", host_src);
        if (alt) src[alt - host_src] = 0;
        if (host_have) {
            char pos[48];

            fmt_fix(host_lat, host_lon, pos, sizeof pos);
            color(120, 230, 120);
            snprintf(buf, sizeof buf, "%s, %.0f ft above sea level", pos, host_alt * 3.28084);
        } else {
            color(240, 170, 80);
            snprintf(buf, sizeof buf, "Unknown: set latitude and longitude in ami.cfg, or allow the lookup there.");
        }
        text(776, 494, -1, buf);
        color(200, 205, 215);
        fit(src, RX2 - 4 - 776 - 8, buf, sizeof buf);
        text(776, 510, -1, buf);
        if (alt) { fit(alt + 2, RX2 - 4 - 776 - 8, buf, sizeof buf); text(776, 526, -1, buf); }
    } else {
        color(200, 205, 215);
        text(776, 494, -1, "LOCATE asks the tool kit: a position set in ami.cfg, else the city the");
        text(776, 510, -1, "internet address maps to, and the terrain height there. Approximate.");
    }
    button(776, 548, 870, 576, "LOCATE", H_HOST_LOCATE, 1, 0);
    help(776, 590, "The station finder's NEAREST falls back to this when the radio has no position.");
}

static int in_write_area(int off)
{
    const int *a;

    for (a = cfg_write_areas_of(img, factory_pending); a[0] >= 0; a += 2) if (off >= a[0] && off < a[1]) return 1;
    return 0;
}

static void draw_image_page(void)
{
    int r, c, off, nrows = mdl()->size / 16;
    char buf[320], id[20], ser[24], err[128];
    double y;

    cfg_flash_id(img, id, sizeof id);
    cfg_serial(img, ser, sizeof ser);
    font(AMI_FONT_SIGN, 11, FALSE);
    color(200, 205, 215);
    if (!have_img) snprintf(buf, sizeof buf, "No image loaded.");
    else {
        cfg_check(img, err, sizeof err);
        snprintf(buf, sizeof buf, "Model code %04X, flash ID %s, serial %s. %s%s", cfg_model(img), id, ser,
                 err[0] ? err : "Bright bytes are the ones a write sends back, dim ones stay as the radio has them.",
                 modified ? "  Edited." : "");
    }
    text(320, 104, -1, buf);
    if (hextop > nrows - HEXROWS) hextop = nrows - HEXROWS;
    if (hextop < 0) hextop = 0;
    for (r = 0; r < HEXROWS; r++) {
        off = (hextop + r) * 16;
        y = HY1 + r * HEXH;
        color(r % 2 ? 24 : 28, r % 2 ? 26 : 30, r % 2 ? 32 : 36);
        frect(RX1 + 4, y, 1128, y + HEXH);
        font(AMI_FONT_TERM, 12, FALSE);
        color(150, 160, 190);
        snprintf(buf, sizeof buf, "%04X", off);
        ltext(320, y, y + HEXH, buf);
        if (!have_img) continue;
        for (c = 0; c < 16; c++) {
            int w = in_write_area(off + c);

            if (img[off + c] == 0xff) color(w ? 110 : 70, w ? 114 : 72, w ? 124 : 80);
            else color(w ? 235 : 130, w ? 238 : 132, w ? 245 : 140);
            snprintf(buf, sizeof buf, "%02X", img[off + c]);
            ltext(372 + c * 24 + (c >= 8 ? 8 : 0), y, y + HEXH, buf);
        }
        for (c = 0; c < 16; c++) {
            int b = img[off + c];

            buf[c] = (char)(b >= 32 && b < 127 ? b : '.');
        }
        buf[16] = 0;
        color(200, 205, 215);
        ltext(790, y, y + HEXH, buf);
    }
    scrollbar(1132, HY1, 1150, HY1 + HEXROWS * HEXH, nrows, HEXROWS, hextop, H_SB_UP);
}

/* ---------------------------------------------------------------- the whole window */

static void redraw(void)
{
    static int buf = 1;
    int    w, h, i;
    double scale;
    char   s[600];
    long   t = now_ms();

    gw = stdout;
    hs = &main_hs;
    buf = 3 - buf;
    ami_select(gw, buf, 3 - buf);
    ami_auto(gw, FALSE);
    ami_curvis(gw, FALSE);
    ami_binvis(gw);

    ami_viewoffg(gw, 0, 0);
    ami_viewscale(gw, 1, 1);
    w = (int)ami_maxxg(gw);
    h = (int)ami_maxyg(gw);
    ami_bcolorg(gw, cv(30), cv(31), cv(34));
    ami_fcolorg(gw, cv(30), cv(31), cv(34));
    ami_frect(gw, 1, 1, w, h);
    scale = fmin((double)w / (PANW * GRID), (double)h / (PANH * GRID));
    ami_viewoffg(gw, (ami_long)((w - PANW * GRID * scale) / 2), (ami_long)((h - PANH * GRID * scale) / 2));
    ami_viewscale(gw, (float)scale, (float)scale);
    hs->n = 0;

    /* backdrop */
    color(20, 20, 22);
    frrect(2, 2, PANW - 2, PANH - 2, 16);
    color(36, 38, 43);
    frrect(6, 6, PANW - 6, PANH - 6, 12);

    draw_radio();

    /* toolbar */
    {
        int can = have_img && !busy();

        button(RX1, 14, RX1 + 110, 44, "READ RADIO", H_READ, radio.connected && !busy(), 0);
        button(RX1 + 120, 14, RX1 + 230, 44, "WRITE RADIO", H_WRITE, radio.connected && can, 0);
        button(RX1 + 250, 14, RX1 + 330, 44, "OPEN", H_OPEN, !busy(), 0);
        button(RX1 + 340, 14, RX1 + 420, 44, "SAVE", H_SAVE, can, 0);
        button(RX1 + 430, 14, RX1 + 520, 44, "SAVE AS", H_SAVEAS, can, 0);
        button(RX1 + 540, 14, RX1 + 620, 44, "NEW", H_NEWIMG, !busy(), 0);
        cycfield(C_NEWMODEL, RX1 + 626, 16, RX1 + 722, 42, cfg_model_by_code(new_model)->name, !busy());
        if (busy()) button(RX2 - 90, 14, RX2, 44, "CANCEL", H_CANCEL, 1, 0);
    }

    /* tabs */
    for (i = 0; i < NPAGES; i++) {
        double x = RX1 + i * 143;

        int off = i == P_ROUTES && !mdl()->has_routes;   /* a page this model lacks */

        if (i == page) color(44, 48, 60);
        else color(28, 30, 36);
        frrect(x, 58, x + 137, 92, 6);
        if (i == page) { color(70, 120, 200); frect(x, 88, x + 137, 92); }
        font(AMI_FONT_SIGN, 12, TRUE);
        if (off) color(90, 94, 104);
        else color(i == page ? 255 : 170, i == page ? 255 : 176, i == page ? 255 : 190);
        ctext(x, 58, x + 137, 88, page_name[i]);
        if (!off) addhit(x, 58, x + 137, 92, H_TAB0 + i);
    }
    color(44, 48, 60);
    frrect(RX1, PY1 - 6, RX2, PY2, 6);
    color(22, 24, 28);
    frrect(RX1 + 2, PY1 - 4, RX2 - 2, PY2 - 2, 5);

    switch (page) {
        case P_MEMORY:  draw_memory_page(); break;
        case P_ROUTES:
            if (mdl()->has_routes) draw_routes_page();
            else {
                char m[80];

                snprintf(m, sizeof m, "The %s has no routes.", mdl()->longname);
                font(AMI_FONT_SIGN, 12, TRUE);
                color(140, 146, 160);
                ltext(360, 122, 142, m);
            }
            break;
        case P_WEATHER: draw_weather_page(); break;
        case P_SETUP:   draw_setup_page(); break;
        case P_GPS:     draw_gps_page(); break;
        default:        draw_image_page(); break;
    }

    /* status bar */
    color(22, 24, 28);
    frrect(RX1, 700, RX2, 728, 5);
    font(AMI_FONT_SIGN, 12, TRUE);
    if (note[0] && t < note_until) { color(240, 200, 90); fit(note, 560, s, sizeof s); }
    else if (busy()) {
        color(120, 200, 240);
        snprintf(s, sizeof s, "%s...", radio_opname(radio.op));
    } else { color(radio.connected ? 120 : 230, radio.connected ? 220 : 150, radio.connected ? 120 : 90); fit(radio.status, 560, s, sizeof s); }
    ltext(RX1 + 10, 700, 728, s);
    color(160, 168, 185);
    if (have_img) {
        const char *p = strrchr(filename, '/');

        snprintf(s, sizeof s, "%s%s", filename[0] ? (p ? p + 1 : filename) : "(from radio)", modified ? " *" : "");
    } else s[0] = 0;
    fit(s, 260, s, sizeof s);
    text(RX2 - 10, 700 + 14 - (double)ami_chrsizy(gw) / GRID / 2, 1, s);

    ami_select(gw, buf, buf);
}

/* ---------------------------------------------------------------- editing */

static int mb_valid_sel(void) { return sel >= 0 && sel < cfg_mb_count(img); }
static int rt_valid_sel(void) { return rsel >= 0 && rsel < cfg_rt_count(img); }

/* store an edited entry back and keep it chosen through the re-sort */
static void mb_update(mbentry *e)
{
    int k;

    e->used = 1;
    cfg_mb_set(img, sel, e);
    k = cfg_mb_normalize(img, e);
    if (k >= 0) sel = k;
    modified = 1;
}

static void rt_update(route *r)
{
    int k;

    r->used = 1;
    cfg_rt_set(img, rsel, r);
    k = cfg_rt_normalize(img, r);
    if (k >= 0) rsel = k;
    modified = 1;
}

static void start_edit(int id, const char *current, int max)
{
    edit_id = id;
    snprintf(edit_buf, sizeof edit_buf, "%s", current);
    edit_len = (int)strlen(edit_buf);
    edit_max = max;
    snprintf(edit_shown, sizeof edit_shown, "%s", current);
}

/* the text a field shows, for starting an edit */
static void field_text(int id, char *out, size_t outlen, int *max)
{
    mbentry e;
    route rt;
    int scan;
    char tag[20];

    out[0] = 0;
    *max = mdl()->tag_max;
    if (id == T_STA_QUERY) { snprintf(out, outlen, "%s", squery); *max = 40; return; }
    if (id >= T_SET0) {
        cfg_fmt_freq(cfg_get(img, id - T_SET0), out, outlen);
        *max = 8;
    } else if (id >= T_GRP0) {
        cfg_group_name(img, id - T_GRP0 + 1, out, outlen);
        if (!strcmp(out, "(blank)")) out[0] = 0;
        *max = GRP_MAX;
    } else if (id >= T_WX0) {
        cfg_wx_get(img, id - T_WX0, tag, &scan);
        snprintf(out, outlen, "%s", tag);
    } else if (id == T_RT_NAME) {
        if (rt_valid_sel()) { cfg_rt_get(img, rsel, &rt); snprintf(out, outlen, "%s", rt.name); }
    } else if (mb_valid_sel()) {
        cfg_mb_get(img, sel, &e);
        switch (id) {
            case T_MB_TAG:  snprintf(out, outlen, "%s", e.tag); break;
            case T_MB_FREQ: cfg_fmt_freq(e.freq, out, outlen); *max = 8; break;
            case T_MB_LAT:  if (e.haspos) cfg_fmt_pos(1, e.lat_deg, e.lat_mmm, out, outlen); *max = 14; break;
            case T_MB_LON:  if (e.haspos) cfg_fmt_pos(0, e.lon_deg, e.lon_mmm, out, outlen); *max = 14; break;
            case T_MB_COURSE: if (e.course >= 0) snprintf(out, outlen, "%03d.%02d", e.course / 100, e.course % 100); *max = 6; break;
        }
    }
}

static void begin_edit(int id)
{
    char cur[64];
    int max;

    field_text(id, cur, sizeof cur, &max);
    start_edit(id, cur, max);
}

/* apply the edit buffer to its field */
static void commit_edit(void)
{
    int id = edit_id, v, deg, mmm, allow833;
    mbentry e;
    route rt;
    char *s = edit_buf, err[128], tag[20];
    int scan, n;

    edit_id = -1;
    if (id < 0) return;
    while (*s == ' ') s++;
    n = (int)strlen(s);
    while (n > 0 && s[n - 1] == ' ') s[--n] = 0;
    if (id == T_STA_QUERY) {
        int changed = strcmp(s, squery) != 0;

        snprintf(squery, sizeof squery, "%s", s);
        if (changed) pop_search();
        return;
    }
    if (!have_img) return;
    if (!strcmp(s, edit_shown)) return;
    allow833 = cfg_get(img, cfg_sid833(img)) == 1;

    if (id >= T_SET0) {
        if (!*s) v = 0;
        else {
            v = cfg_parse_freq(s);
            if (v < 0 || !cfg_freq_ok(img, v, allow833)) { set_note("Not a COMM or NAVI channel frequency, e.g. 121.500"); return; }
        }
        cfg_set(img, id - T_SET0, v);
        modified = 1;
        return;
    }
    if (id >= T_GRP0) {
        if (!cfg_tag_ok(s)) { set_note("Names take letters, digits, space and * + , / & - [ ] ."); return; }
        if (!*s) { set_note("A group needs a name"); return; }
        cfg_set_group_name(img, id - T_GRP0 + 1, s);
        modified = 1;
        return;
    }
    if (id >= T_WX0) {
        if (!cfg_tag_ok(s)) { set_note("Names take letters, digits, space and * + , / & - [ ] ."); return; }
        cfg_wx_get(img, id - T_WX0, tag, &scan);
        cfg_wx_set(img, id - T_WX0, s, scan);
        modified = 1;
        return;
    }
    if (id == T_RT_NAME) {
        if (!rt_valid_sel()) return;
        if (!*s) { set_note("The route name cannot be empty"); return; }
        if (!cfg_tag_ok(s)) { set_note("Names take letters, digits, space and * + , / & - [ ] ."); return; }
        cfg_rt_get(img, rsel, &rt);
        snprintf(rt.name, sizeof rt.name, "%.15s", s);
        cfg_tag_fit(img, rt.name);
        rt_update(&rt);
        return;
    }
    if (!mb_valid_sel()) return;
    cfg_mb_get(img, sel, &e);
    switch (id) {
        case T_MB_TAG:
            if (!*s) { set_note("The name cannot be empty"); return; }
            if (!cfg_tag_ok(s)) { set_note("Names take letters, digits, space and * + , / & - [ ] ."); return; }
            snprintf(e.tag, sizeof e.tag, "%.15s", s);
            cfg_tag_fit(img, e.tag);
            break;
        case T_MB_FREQ:
            if (!*s) e.freq = 0;
            else {
                v = cfg_parse_freq(s);
                if (v < 0 || !cfg_freq_ok(img, v, allow833)) {
                    set_note(allow833 ? "COMM 118.000-136.990 or NAVI 108.000-117.975, 25 kHz or 8.33 kHz steps (xxx.005, .010, .015, .030...)"
                                      : "COMM 118.000-136.975 or NAVI 108.000-117.975 in 25 kHz steps (switch 8.33 kHz on in SETUP for those)");
                    return;
                }
                e.freq = v;
            }
            break;
        case T_MB_COURSE:
            if (!*s) e.course = -1;
            else {
                double c = atof(s);

                if (strspn(s, "0123456789.") != strlen(s) || c < 0 || c >= 360) { set_note("Runway true course as 310 or 310.50 degrees"); return; }
                e.course = (int)(c * 100 + 0.5);
            }
            break;
        case T_MB_LAT:
        case T_MB_LON:
            if (!*s) {
                if (e.haspos && cfg_rt_uses(img, e.addr) >= 0) { set_note("A route uses this memory's position"); return; }
                e.haspos = 0;
                break;
            }
            if (!cfg_parse_pos(s, id == T_MB_LAT, &deg, &mmm)) {
                set_note(id == T_MB_LAT ? "Latitude as 37 19.583, 37 19 35 or 37.3264" : "Longitude as 121 49.113, 121 49 07 or 121.8186");
                return;
            }
            if (id == T_MB_LAT) { e.lat_deg = deg; e.lat_mmm = mmm; }
            else { e.lon_deg = deg; e.lon_mmm = mmm; }
            if (!e.haspos) { e.haspos = 1; if (!e.ns) e.ns = 'N'; if (!e.ew) e.ew = 'W'; }
            break;
    }
    if (!cfg_mb_valid(img, &e, allow833, err, sizeof err)) set_note(err);
    mb_update(&e);
}

static void cancel_edit(void)
{
    edit_id = -1;
}

/* step a choice field */
static void cycle(int id, int dir)
{
    mbentry e;
    route rt;
    int v, scan;
    char tag[20];

    if (id == C_NEWMODEL) {
        int i, n;

        for (n = 0; cfg_model_list(n); n++) ;
        for (i = 0; i < n; i++) if (cfg_model_list(i)->code == new_model) break;
        new_model = cfg_model_list(((i < n ? i : 0) + dir + n) % n)->code;
        return;
    }
    if (id == C_STA_GROUP) { sgroup = (sgroup + dir + NGROUPS + 1) % (NGROUPS + 1); pop_dirty = 1; return; }
    if (id == C_STA_SCAN) { sscan = !sscan; pop_dirty = 1; return; }
    if (!have_img || busy()) return;
    if (edit_id >= 0) commit_edit();
    if (id >= C_SET0) {
        const setting *s = &cfg_settings_of(img, NULL)[id - C_SET0];

        if (s->kind == K_FREQ) return;
        v = cfg_get(img, id - C_SET0);
        v = v < 0 ? 0 : (v + dir + s->n) % s->n;
        cfg_set(img, id - C_SET0, v);
        modified = 1;
        return;
    }
    if (id >= C_WX0) {
        cfg_wx_get(img, id - C_WX0, tag, &scan);
        cfg_wx_set(img, id - C_WX0, tag, !scan);
        modified = 1;
        return;
    }
    if (id == C_RT_ADD) {
        rt_add = cfg_mb_next_pos(img, rt_add, dir);
        return;
    }
    if (id >= C_RT_GROUP && id <= C_RT_TO) {
        if (!rt_valid_sel()) return;
        cfg_rt_get(img, rsel, &rt);
        if (id == C_RT_GROUP) rt.group = ((rt.group - 1 + dir + NGROUPS) % NGROUPS) + 1;
        else if (id == C_RT_FROM) { v = cfg_mb_next_pos(img, rt.from, dir); if (v) rt.from = v; }
        else { v = cfg_mb_next_pos(img, rt.to, dir); if (v) rt.to = v; }
        rt_update(&rt);
        return;
    }
    if (!mb_valid_sel()) return;
    cfg_mb_get(img, sel, &e);
    switch (id) {
        case C_MB_GROUP: e.group = (e.group + dir + NGROUPS + 1) % (NGROUPS + 1); break;
        case C_MB_SCAN:  e.scan = !e.scan; break;
        case C_MB_SHIFT: e.shift = !e.shift; break;
        case C_MB_NS:    if (!e.haspos) return; e.ns = e.ns == 'N' ? 'S' : 'N'; break;
        case C_MB_EW:    if (!e.haspos) return; e.ew = e.ew == 'E' ? 'W' : 'E'; break;
        default: return;
    }
    mb_update(&e);
}

/* ---------------------------------------------------------------- actions */

static int armed(int id)
{
    if (confirm_id == id && now_ms() < confirm_until) { confirm_id = H_NONE; return 1; }
    confirm_id = id;
    confirm_until = now_ms() + 4000;
    return 0;
}

static void select_row(int i)
{
    int n;

    if (edit_id >= 0) commit_edit();
    if (page == P_ROUTES) {
        n = cfg_rt_count(img);
        if (i < 0) i = 0;
        if (i >= n) i = n - 1;
        rsel = i;
        if (rsel >= 0) {
            if (rsel < rtop) rtop = rsel;
            if (rsel >= rtop + NROWS) rtop = rsel - NROWS + 1;
        }
        return;
    }
    n = cfg_mb_count(img);
    if (i < 0) i = 0;
    if (i >= n) i = n - 1;
    sel = i;
    if (sel >= 0) {
        if (sel < top) top = sel;
        if (sel >= top + NROWS) top = sel - NROWS + 1;
    }
}

static void fresh_image(void)
{
    sel = rsel = -1;
    top = rtop = 0;
    rt_add = 0;
    factory_pending = 0;
    edit_id = -1;
}

static void do_open(void)
{
    char fn[512], err[256];

    if (modified && !armed(H_OPEN)) { set_note("The image has unsaved changes: press OPEN again to drop them"); return; }
    fn[0] = 0;
    if (filename[0]) snprintf(fn, sizeof fn, "%s", filename);
    ami_queryopen(fn, sizeof fn);
    if (!fn[0]) return;
    if (!cfg_load(fn, img, err, sizeof err)) { set_note(err); return; }
    have_img = 1;
    modified = 0;
    fresh_image();
    snprintf(filename, sizeof filename, "%s", fn);
    set_note("Image loaded");
}

static void do_save(int as)
{
    char fn[512], err[640], name[64], ts[32];

    if (!have_img) return;
    if (edit_id >= 0) commit_edit();
    if (as || !filename[0]) {
        if (filename[0]) snprintf(fn, sizeof fn, "%s", filename);
        else {
            stamp(ts, sizeof ts);
            snprintf(name, sizeof name, "FTA850_%s", ts);
            user_file(fn, sizeof fn, "Desktop", name, "dat");
        }
        ami_querysave(fn, sizeof fn);
        if (!fn[0]) return;
        if (!strrchr(fn, '.') || strlen(strrchr(fn, '.')) > 5) strncat(fn, ".dat", sizeof fn - strlen(fn) - 1);
    } else snprintf(fn, sizeof fn, "%s", filename);
    if (!cfg_save(fn, img, err, sizeof err)) { set_note(err); return; }
    snprintf(filename, sizeof filename, "%s", fn);
    modified = 0;
    snprintf(err, sizeof err, "Saved %s", fn);
    set_note(err);
}

static void do_new(void)
{
    if (modified && !armed(H_NEWIMG)) { set_note("The image has unsaved changes: press NEW again to drop them"); return; }
    cfg_defaults(img, cfg_model_by_code(new_model));
    have_img = 1;
    modified = 1;
    filename[0] = 0;
    fresh_image();
    {
        char m[120];

        snprintf(m, sizeof m, "Factory default %s image: no memories, everything as delivered", mdl()->longname);
        set_note(m);
    }
}

static void do_read(void)
{
    if (modified && !armed(H_READ)) { set_note("The image has unsaved changes: press READ RADIO again to replace them"); return; }
    if (edit_id >= 0) cancel_edit();
    if (!radio_start(&radio, OP_READ, NULL, NULL)) set_note(radio.result[0] ? radio.result : "The radio is not ready");
}

static void do_write(void)
{
    int i, n, allow833 = cfg_get(img, cfg_sid833(img)) == 1;
    mbentry e;
    route rt;
    char err[128], msg[200];

    if (!have_img) return;
    if (edit_id >= 0) commit_edit();
    if (!cfg_check(img, err, sizeof err)) { set_note(err); return; }
    n = cfg_mb_count(img);
    for (i = 0; i < n; i++) {
        cfg_mb_get(img, i, &e);
        if (!cfg_mb_valid(img, &e, allow833, err, sizeof err)) {
            snprintf(msg, sizeof msg, "Memory %d: %s", e.addr, err);
            set_note(msg);
            page = P_MEMORY;
            select_row(i);
            return;
        }
    }
    n = cfg_rt_count(img);
    for (i = 0; i < n; i++) {
        cfg_rt_get(img, i, &rt);
        if (!cfg_rt_valid(img, &rt, err, sizeof err)) {
            snprintf(msg, sizeof msg, "Route %s: %s", rt.name, err);
            set_note(msg);
            page = P_ROUTES;
            select_row(i);
            return;
        }
    }
    if (!armed(H_WRITE)) { set_note("Press WRITE RADIO again to program the radio"); return; }
    if (!radio_start(&radio, OP_WRITE, img, cfg_write_areas_of(img, factory_pending)))
        set_note(radio.result[0] ? radio.result : "The radio is not ready");
}

static void do_log_get(void)
{
    char fn[512], name[64], ts[32];

    stamp(ts, sizeof ts);
    snprintf(name, sizeof name, "FTA850_log_%s", ts);
    user_file(fn, sizeof fn, "Desktop", name, "gpx");
    ami_querysave(fn, sizeof fn);
    if (!fn[0]) return;
    if (!strrchr(fn, '.') || strlen(strrchr(fn, '.')) > 5) strncat(fn, ".gpx", sizeof fn - strlen(fn) - 1);
    snprintf(logfile, sizeof logfile, "%s", fn);
    if (!radio_start(&radio, OP_LOGREAD, NULL, NULL)) set_note("The radio is not ready");
}

static void do_reset(int which)
{
    char m[128];

    if (!have_img) return;
    if (which >= cfg_nresets(img)) return;
    if (which == cfg_nresets(img) - 1 && !armed(H_RESET0 + which)) { set_note("Press FACTORY again to put the whole image to factory values"); return; }
    if (edit_id >= 0) cancel_edit();
    cfg_reset(img, which);
    modified = 1;
    sel = rsel = -1;
    if (which == cfg_nresets(img) - 1) factory_pending = 1;
    snprintf(m, sizeof m, "%s reset applied to the image; WRITE RADIO sends it", cfg_reset_name(img, which, 0));
    set_note(m);
}

static void mb_new(void)
{
    mbentry e;
    int k;

    if (!have_img) return;
    if (edit_id >= 0) commit_edit();
    memset(&e, 0, sizeof e);
    snprintf(e.tag, sizeof e.tag, "NEW");
    e.ns = 'N'; e.ew = 'W';
    k = cfg_mb_add(img, &e);
    if (k < 0) { set_note("The memory book is full"); return; }
    modified = 1;
    page = P_MEMORY;
    select_row(k);
    begin_edit(T_MB_TAG);
    edit_len = 0;
    edit_buf[0] = 0;
}

static void mb_delete(void)
{
    char err[160];

    if (!mb_valid_sel()) return;
    if (edit_id >= 0) cancel_edit();
    if (!cfg_mb_delete(img, sel, err, sizeof err)) { set_note(err); return; }
    modified = 1;
    select_row(sel);
}

static void mb_delete_all(void)
{
    if (!have_img) return;
    if (!armed(H_MB_DELALL)) { set_note("Press ALL again to delete every memory and route"); return; }
    cancel_edit();
    cfg_mb_clear(img);
    modified = 1;
    sel = rsel = -1;
    top = rtop = 0;
}

static void rt_new(void)
{
    route r;
    int k;

    if (!have_img) return;
    if (edit_id >= 0) commit_edit();
    memset(&r, 0, sizeof r);
    snprintf(r.name, sizeof r.name, "NEW");
    r.group = 1;
    r.from = cfg_mb_next_pos(img, 0, 1);
    r.to = cfg_mb_next_pos(img, r.from, 1);
    if (!r.to) r.to = r.from;
    if (!r.from) { set_note("A route needs memories with positions; add those first"); return; }
    k = cfg_rt_add(img, &r);
    if (k < 0) { set_note("The route list is full"); return; }
    modified = 1;
    page = P_ROUTES;
    select_row(k);
    begin_edit(T_RT_NAME);
    edit_len = 0;
    edit_buf[0] = 0;
}

static void rt_delete(void)
{
    if (!rt_valid_sel()) return;
    if (edit_id >= 0) cancel_edit();
    cfg_rt_delete(img, rsel);
    modified = 1;
    select_row(rsel);
}

static void rt_delete_all(void)
{
    if (!have_img) return;
    if (!armed(H_RT_DELALL)) { set_note("Press ALL again to delete every route"); return; }
    cancel_edit();
    cfg_rt_clear(img);
    modified = 1;
    rsel = -1;
    rtop = 0;
}

static void rt_via(int what)
{
    route rt;

    if (!rt_valid_sel()) return;
    if (edit_id >= 0) commit_edit();
    cfg_rt_get(img, rsel, &rt);
    if (what == H_RT_ADDVIA) {
        if (!rt_add || rt.nvia >= VIA_MAX) return;
        rt.via[rt.nvia++] = rt_add;
    } else if (what == H_RT_RMVIA) {
        if (rt.nvia > 0) rt.nvia--;
    } else rt.nvia = 0;
    rt_update(&rt);
}


/* ------------------------------------------------------- station finder */

static void pset_note(const char *m)
{
    snprintf(pnote, sizeof pnote, "%s", m);
    pnote_until = now_ms() + 8000;
    pop_dirty = 1;
}

static int picked(int hit, int k)
{
    int i;

    for (i = 0; i < nspicks; i++) if (spicks[i].hit == hit && spicks[i].k == k) return 1;
    return 0;
}

static void pick_add(int hit, int k)
{
    if (picked(hit, k) || nspicks >= SPICKS) return;
    spicks[nspicks].hit = (short)hit;
    spicks[nspicks].k = (short)k;
    nspicks++;
}

static void pick_remove(int hit, int k)
{
    int i;

    for (i = 0; i < nspicks; i++)
        if (spicks[i].hit == hit && spicks[i].k == k) { spicks[i] = spicks[--nspicks]; return; }
}

/* the tree rows: each hit, then its frequencies if it is open */
static void srows_build(void)
{
    int i, k;

    nsrows = 0;
    for (i = 0; i < nshits && nsrows < SROWS; i++) {
        const station *s = sta_get(shits[i].sta);

        srows[nsrows].hit = (short)i; srows[nsrows].k = -1; srows[nsrows].sel = picked(i, -1);
        nsrows++;
        if (!sexp[i]) continue;
        for (k = 0; k < s->nfreq && nsrows < SROWS; k++) {
            srows[nsrows].hit = (short)i; srows[nsrows].k = (short)k; srows[nsrows].sel = picked(i, k);
            nsrows++;
        }
    }
    if (stop_ > nsrows - POPROWS) stop_ = nsrows - POPROWS;
    if (stop_ < 0) stop_ = 0;
    if (sanchor >= nsrows) sanchor = nsrows - 1;
    pop_dirty = 1;
}

static void pop_show_row(int r)
{
    if (r < stop_) stop_ = r;
    if (r >= stop_ + POPROWS) stop_ = r - POPROWS + 1;
}

/* a new list of hits */
static void pop_fresh(int near)
{
    snear = near;
    memset(sexp, 0, sizeof sexp);
    nspicks = 0;
    sanchor = -1;
    stop_ = 0;
}

static void pop_search(void)
{
    int i;
    char m[160];

    if (!sta_n) { pset_note(sta_err[0] ? sta_err : "No station data"); return; }
    pop_fresh(0);
    nshits = sta_search(squery, shits, SHITS);
    for (i = 0; i < nshits; i++) sexp[i] = shits[i].rank == 0;   /* exact code matches open */
    if (nshits == 1) { pick_add(0, -1); sanchor = 0; }
    srows_build();
    if (!squery[0]) pnote_until = 0;
    else if (!nshits) { snprintf(m, sizeof m, "Nothing matches %s", squery); pset_note(m); }
    else { snprintf(m, sizeof m, "%d match%s%s", nshits, nshits == 1 ? "" : "es", nshits == SHITS ? " (the first 400)" : ""); pset_note(m); }
}

static void pop_nearest_at(double lat, double lon, const char *what)
{
    char m[220];

    pop_fresh(1);
    nshits = sta_nearest(lat, lon, shits, sdist, SHITS);
    srows_build();
    snprintf(m, sizeof m, "%d stations around %s, nearest first", nshits, what);
    pset_note(m);
}

/* this computer's position from the tool kit (ami.cfg, or the internet
   address looked up), as the last resort; 0 if it has none */
static int pop_nearest_host(void)
{
    char pos[48], what[400];

    if (!host_done) host_locate();
    if (!host_have) return 0;
    fmt_fix(host_lat, host_lon, pos, sizeof pos);
    snprintf(what, sizeof what, "this computer, %s (%s)", pos, host_src);
    pop_nearest_at(host_lat, host_lon, what);
    return 1;
}

/* NEAREST: around the station or "lat lon" typed, else the radio's last
   GPS fix, else the chosen memory's position, else fetch the fix from the
   radio's log, else this computer's position */
static void pop_nearest(void)
{
    stahit h[8];
    double la, lo;
    char what[160];
    mbentry e;

    if (!sta_n) { pset_note(sta_err[0] ? sta_err : "No station data"); return; }
    if (edit_id == T_STA_QUERY) { snprintf(squery, sizeof squery, "%s", edit_buf); edit_id = -1; }
    if (squery[0]) {
        if (sta_search(squery, h, 8) > 0 && h[0].rank == 0) {
            const station *s = sta_get(h[0].sta);

            snprintf(what, sizeof what, "%s %s", s->ident, s->name);
            pop_nearest_at(s->lat, s->lon, what);
            return;
        }
        if (sscanf(squery, "%lf %lf", &la, &lo) == 2 || sscanf(squery, "%lf,%lf", &la, &lo) == 2) {
            if (fabs(la) <= 90 && fabs(lo) <= 180) { pop_nearest_at(la, lo, squery); return; }
        }
    }
    if (have_fix) {
        char pos[48];

        fmt_fix(fix_lat, fix_lon, pos, sizeof pos);
        snprintf(what, sizeof what, "the radio's last GPS fix, %s", pos);
        pop_nearest_at(fix_lat, fix_lon, what);
        return;
    }
    if (have_img && mb_valid_sel()) {
        cfg_mb_get(img, sel, &e);
        if (e.haspos) {
            la = e.lat_deg + e.lat_mmm / 60000.0;
            lo = e.lon_deg + e.lon_mmm / 60000.0;
            if (e.ns == 'S') la = -la;
            if (e.ew == 'W') lo = -lo;
            snprintf(what, sizeof what, "memory %d %s", e.addr, e.tag);
            pop_nearest_at(la, lo, what);
            return;
        }
    }
    if (radio.connected && !busy()) {
        logfile[0] = 0;
        near_pending = 1;
        if (radio_start(&radio, OP_LOGREAD, NULL, NULL)) { pset_note("Reading the radio's GPS log for its last fix..."); return; }
        near_pending = 0;
    }
    if (pop_nearest_host()) return;
    pset_note("No position to start from: type a code or 'lat lon', choose a memory with a position, or connect the radio");
}

/* put the chosen rows in the memory book; waypoint: the facilities' positions only */
static void pop_add(int waypoint)
{
    int i, added = 0;
    char err[256], last[256] = "", m[560];
    unsigned char done[SHITS];

    if (!have_img) { pset_note("Nothing to add to yet: READ RADIO, OPEN a file or NEW in the main window first"); return; }
    if (busy()) { pset_note("The radio is busy"); return; }
    if (!nspicks) { pset_note("Choose a station or a frequency first: click a row, shift-click a range, control-click to add one"); return; }
    memset(done, 0, sizeof done);
    for (i = 0; i < nspicks; i++) {
        const station *s = sta_get(shits[spicks[i].hit].sta);

        if (waypoint) {
            if (done[spicks[i].hit]) continue;
            done[spicks[i].hit] = 1;
            added += sta_add_waypoint(img, s, sgroup, err, sizeof err);
        } else added += sta_add(img, s, spicks[i].k, sgroup, sscan, err, sizeof err);
        if (err[0]) snprintf(last, sizeof last, "%s", err);
    }
    if (added) { modified = 1; pop_main_dirty = 1; }
    snprintf(m, sizeof m, "Added %d memor%s%s%s", added, added == 1 ? "y" : "ies", last[0] ? ". " : "", last);
    pset_note(m);
    set_note(m);
}

/* a click on tree row r with the modifier keys held */
static void pop_pick(int r, int mods)
{
    int a, b, i;

    if (r < 0 || r >= nsrows) return;
    if ((mods & AMI_MOD_SHIFT) && sanchor >= 0 && sanchor < nsrows) {
        a = sanchor < r ? sanchor : r;
        b = sanchor < r ? r : sanchor;
        if (!(mods & AMI_MOD_CTRL)) nspicks = 0;
        for (i = a; i <= b; i++) pick_add(srows[i].hit, srows[i].k);
    } else if (mods & AMI_MOD_CTRL) {
        if (picked(srows[r].hit, srows[r].k)) pick_remove(srows[r].hit, srows[r].k);
        else pick_add(srows[r].hit, srows[r].k);
        sanchor = r;
    } else {
        nspicks = 0;
        pick_add(srows[r].hit, srows[r].k);
        sanchor = r;
    }
    srows_build();
}

static void pop_expand_all(int on)
{
    int i;

    for (i = 0; i < nshits; i++) sexp[i] = (unsigned char)on;
    srows_build();
}

static void pop_open(void)
{
    ami_long wx, wy;
    double k;

    if (popwf) { ami_front(popwf); return; }
    if (!sta_n) { set_note(sta_err[0] ? sta_err : "No station data: see data/README.md"); return; }
    ami_openwin(&stdin, &popwf, NULL, POPWIN);
    ami_auto(popwf, FALSE);
    ami_curvis(popwf, FALSE);
    ami_binvis(popwf);
    ami_title(popwf, "Stations");
    ami_autobuf(popwf, FALSE);
    k = unit_px(popwf);
    ami_winclientg(popwf, (ami_long)lround(POPW * k), (ami_long)lround(POPH * k), &wx, &wy,
                   BIT(ami_wmframe) | BIT(ami_wmsize) | BIT(ami_wmsysbar));
    ami_setsizg(popwf, wx, wy);
    ami_front(popwf);
    if (edit_id >= 0) commit_edit();
    begin_edit(T_STA_QUERY);
    pop_dirty = 1;
}

static void pop_close(void)
{
    if (!popwf) return;
    if (edit_id == T_STA_QUERY) cancel_edit();
    fclose(popwf);
    popwf = NULL;
}

static void pop_redraw(void)
{
    static int buf = 1;
    FILE   *save_gw = gw;
    hitset *save_hs = hs;
    int     w, h, r, i, allow833 = have_img ? cfg_get(img, cfg_sid833(img)) == 1 : 1, anyopen = 0;
    double  scale, y;
    char    b[120], g[24];
    long    t = now_ms();

    if (!popwf) return;
    gw = popwf;
    hs = &pop_hs;
    buf = 3 - buf;
    ami_select(gw, buf, 3 - buf);
    ami_auto(gw, FALSE);
    ami_curvis(gw, FALSE);
    ami_binvis(gw);
    ami_viewoffg(gw, 0, 0);
    ami_viewscale(gw, 1, 1);
    w = (int)ami_maxxg(gw);
    h = (int)ami_maxyg(gw);
    ami_bcolorg(gw, cv(30), cv(31), cv(34));
    ami_fcolorg(gw, cv(30), cv(31), cv(34));
    ami_frect(gw, 1, 1, w, h);
    scale = fmin((double)w / (POPW * GRID), (double)h / (POPH * GRID));
    ami_viewoffg(gw, (ami_long)((w - POPW * GRID * scale) / 2), (ami_long)((h - POPH * GRID * scale) / 2));
    ami_viewscale(gw, (float)scale, (float)scale);
    hs->n = 0;

    color(36, 38, 43);
    frrect(2, 2, POPW - 2, POPH - 2, 10);

    /* the query row */
    label(14, 14, 38, "Find");
    textfield(T_STA_QUERY, 52, 14, 420, 38, squery, 1);
    button(428, 12, 512, 40, "SEARCH", H_STA_SEARCH, sta_n > 0, 0);
    button(520, 12, 610, 40, "NEAREST", H_STA_NEAR, sta_n > 0 && !busy(), 0);
    label(626, 14, 38, "Group");
    if (have_img) cfg_group_name(img, sgroup, g, sizeof g);
    else if (sgroup) snprintf(g, sizeof g, "GROUP%d", sgroup);
    else snprintf(g, sizeof g, "ALL");
    cycfield(C_STA_GROUP, 672, 14, 782, 38, g, 1);
    label(796, 14, 38, "Scan");
    cycfield(C_STA_SCAN, 836, 14, 906, 38, sscan ? "On" : "Off", 1);
    help(14, 46, "A code (RHV, KRHV, SJC) or part of a name, then Enter. NEAREST: around the station or 'lat lon' typed, else the radio's last GPS fix, else the chosen memory.");

    /* the tree */
    color(38, 42, 52);
    frect(8, 62, 892, 80);
    font(AMI_FONT_SIGN, 12, TRUE);
    color(190, 200, 220);
    ltext(32, 62, 80, "Code");
    ltext(96, 62, 80, "Also");
    ltext(146, 62, 80, "Kind");
    ltext(252, 62, 80, "Name");
    ltext(558, 62, 80, "Place");
    ltext(754, 62, 80, snear ? "Distance" : "Freqs");
    for (r = 0; r < POPROWS; r++) {
        const srow *row;
        const station *s;

        i = stop_ + r;
        y = PLY1 + r * PROWH;
        if (i >= nsrows) {
            if (i == 0) {
                font(AMI_FONT_SIGN, 12, TRUE);
                color(140, 146, 160);
                ltext(32, y, y + PROWH, sta_n ? "Type a code or a name and press Enter, or press NEAREST." : sta_err);
            }
            break;
        }
        row = &srows[i];
        s = sta_get(shits[row->hit].sta);
        if (row->sel) color(40, 70, 130);
        else if (row->k < 0) color(r % 2 ? 26 : 30, r % 2 ? 28 : 32, r % 2 ? 34 : 38);
        else color(r % 2 ? 22 : 25, r % 2 ? 24 : 27, r % 2 ? 29 : 32);
        frect(8, y, 892, y + PROWH);
        addhit(8, y, 892, y + PROWH, H_ROW0 + r);
        font(AMI_FONT_TERM, 13, FALSE);
        color(row->sel ? 255 : 225, row->sel ? 255 : 228, row->sel ? 255 : 235);
        if (row->k < 0) {
            char place[100], kind[32];

            if (s->nfreq) {
                font(AMI_FONT_SIGN, 12, TRUE);
                color(150, 170, 210);
                ltext(14, y, y + PROWH, sexp[row->hit] ? "v" : ">");
                addhit(8, y, 30, y + PROWH, H_ARROW0 + r);
                font(AMI_FONT_TERM, 13, FALSE);
                color(row->sel ? 255 : 225, row->sel ? 255 : 228, row->sel ? 255 : 235);
            }
            ltext(32, y, y + PROWH, s->ident);
            if (s->kind != SK_NAVAID) ltext(96, y, y + PROWH, s->code);
            sta_kind_desc(s, kind, sizeof kind);
            fit(kind, 100, b, sizeof b);
            ltext(146, y, y + PROWH, b);
            fit(s->name, 300, b, sizeof b);
            ltext(252, y, y + PROWH, b);
            snprintf(place, sizeof place, "%s%s%s", s->city, s->city[0] && s->region[0] ? ", " : "", s->region);
            fit(place, 190, b, sizeof b);
            ltext(558, y, y + PROWH, b);
            if (snear) snprintf(b, sizeof b, "%.1f nm", sdist[row->hit]);
            else if (s->nfreq) snprintf(b, sizeof b, "%d", s->nfreq);
            else b[0] = 0;
            ltext(754, y, y + PROWH, b);
            if (sexp[row->hit]) anyopen = 1;
        } else {
            const stafreq *q = sta_freq(s, row->k);
            int u = sta_freq_usable(img, q->khz, allow833);
            char tag[20];

            ltext(56, y, y + PROWH, q->type);
            fit(q->desc, 130, b, sizeof b);
            ltext(110, y, y + PROWH, b);
            snprintf(b, sizeof b, "%3d.%03d", q->khz / 1000, q->khz % 1000);
            ltext(252, y, y + PROWH, b);
            sta_tag(s, row->k, tag, sizeof tag);
            ltext(340, y, y + PROWH, tag);
            font(AMI_FONT_SIGN, 11, FALSE);
            if (have_img && sta_in_book(img, s, row->k)) { color(120, 220, 120); ltext(470, y, y + PROWH, "in the memory book"); }
            else if (u < 0) { color(240, 170, 80); ltext(470, y, y + PROWH, "needs 8.33 kHz on in SETUP"); }
            else if (!u) { color(140, 146, 160); ltext(470, y, y + PROWH, "outside the radio's bands"); }
        }
    }
    scrollbar(896, PLY1, 912, PLY1 + POPROWS * PROWH, nsrows, POPROWS, stop_, H_SB_UP);

    /* actions */
    button(14, 476, 100, 504, "ADD", H_STA_ADD, have_img && nspicks > 0 && !busy(), 0);
    button(108, 476, 212, 504, "WAYPOINT", H_STA_WPT, have_img && nspicks > 0 && !busy(), 0);
    button(220, 476, 330, 504, anyopen ? "COLLAPSE ALL" : "EXPAND ALL", H_STA_EXPAND, nshits > 0, 0);
    button(820, 476, 906, 504, "CLOSE", H_STA_CLOSE, 1, 0);
    help(14, 516, "Click the arrow of a facility to see its frequencies. Click a row to choose it, shift-click for a range, control-click to add one more.");
    help(14, 532, "ADD puts the chosen frequencies in the memory book, named like KRHV-TWR with the facility's position; a chosen facility adds all of its");
    help(14, 548, "usable frequencies. WAYPOINT adds a facility as a position only, for routes. Memories go in the group with scan as set above.");
    help(14, 564, "Station data: OurAirports (ourairports.com/data), public domain. Check a frequency against a current chart before relying on it.");
    font(AMI_FONT_SIGN, 12, TRUE);
    if (pnote[0] && t < pnote_until) { color(240, 200, 90); fit(pnote, 880, b, sizeof b); }
    else if (!have_img) { color(240, 170, 80); snprintf(b, sizeof b, "Nothing to add to yet: READ RADIO, OPEN a file or press NEW in the main window, then ADD lights up"); }
    else if (busy()) { color(130, 138, 155); snprintf(b, sizeof b, "Waiting for the radio..."); }
    else if (!nspicks) { color(130, 138, 155); snprintf(b, sizeof b, "Click a row to choose it (it turns blue), then ADD. %d stations loaded", sta_n); }
    else { color(130, 138, 155); snprintf(b, sizeof b, "%d row%s chosen: ADD puts %s in the memory book", nspicks, nspicks == 1 ? "" : "s", nspicks == 1 ? "it" : "them"); }
    ltext(14, 578, 598, b);

    ami_select(gw, buf, buf);
    gw = save_gw;
    hs = save_hs;
    pop_dirty = 0;
}

static void pop_scroll(int d)
{
    stop_ += d;
    if (stop_ > nsrows - POPROWS) stop_ = nsrows - POPROWS;
    if (stop_ < 0) stop_ = 0;
    pop_dirty = 1;
}

static void pop_click(int id)
{
    int r;

    if (id >= H_CYC0) { cycle(id - H_CYC0, pmx < (hitbox_of(id)->x1 + hitbox_of(id)->x2) / 2 ? -1 : 1); return; }
    if (id >= H_TEXT0) {
        if (edit_id != id - H_TEXT0) { if (edit_id >= 0) commit_edit(); begin_edit(id - H_TEXT0); }
        pop_dirty = 1;
        return;
    }
    if (id >= H_ARROW0 && id < H_ARROW0 + POPROWS) {
        r = stop_ + id - H_ARROW0;
        if (r < nsrows && srows[r].k < 0) { sexp[srows[r].hit] ^= 1; srows_build(); }
        return;
    }
    if (id >= H_ROW0 && id < H_ROW0 + POPROWS) { pop_pick(stop_ + id - H_ROW0, pmods); return; }
    switch (id) {
        case H_STA_SEARCH:
            if (edit_id == T_STA_QUERY) { snprintf(squery, sizeof squery, "%s", edit_buf); edit_id = -1; }
            pop_search();
            begin_edit(T_STA_QUERY);
            break;
        case H_STA_NEAR:   pop_nearest(); begin_edit(T_STA_QUERY); break;
        case H_STA_ADD:    pop_add(0); break;
        case H_STA_WPT:    pop_add(1); break;
        case H_STA_EXPAND: {
            int i, anyopen = 0;

            for (i = 0; i < nshits; i++) if (sexp[i]) anyopen = 1;
            pop_expand_all(!anyopen);
            break;
        }
        case H_STA_CLOSE:  pop_close(); break;
        case H_SB_UP:      pop_scroll(-1); break;
        case H_SB_DN:      pop_scroll(1); break;
        case H_SB_TRACK: {
            const hitbox *t = hitbox_of(H_SB_THUMB);

            if (t) pop_scroll(pmy < t->y1 ? -POPROWS : POPROWS);
            break;
        }
        default: break;
    }
}

/* an event for the finder window */
static void pop_event(ami_evtrec *er)
{
    int id, r;

    gw = popwf;
    hs = &pop_hs;
    switch (er->etype) {
        case ami_etterm:   /* the window was closed, not the program */
            pop_close();
            break;
        case ami_etresize:
            ami_sizbufg(popwf, er->rszxg, er->rszyg);
            pop_dirty = 1;
            break;
        case ami_etredraw:
            pop_dirty = 1;
            break;
        case ami_etmoumovg:
            pmx = (int)ami_scalex(popwf, er->moupxg);
            pmy = (int)ami_scaley(popwf, er->moupyg);
            if (pdrag_thumb) {
                const hitbox *tr = hitbox_of(H_SB_TRACK), *th = hitbox_of(H_SB_THUMB);

                if (tr && nsrows > POPROWS) {
                    int span = tr->y2 - tr->y1 - (th ? th->y2 - th->y1 : 0);
                    int pos = span > 0 ? (pmy - pdrag_off - tr->y1) * (nsrows - POPROWS) / span : 0;

                    if (pos < 0) pos = 0;
                    if (pos > nsrows - POPROWS) pos = nsrows - POPROWS;
                    stop_ = pos;
                    pop_dirty = 1;
                }
            }
            break;
        case ami_etmouba:
            if (er->amoubn == 4 || er->amoubn == 5) {
                int d = er->amoubn == 4 ? -1 : 1;

                id = hit(pmx, pmy);
                if (id >= H_CYC0) cycle(id - H_CYC0, d);
                else pop_scroll(d * 3);
                break;
            }
            if (er->amoubn != 1) break;
            id = hit(pmx, pmy);
            pmods = (int)ami_modkeys(popwf);
            if (id == H_SB_THUMB) {
                const hitbox *th = hitbox_of(H_SB_THUMB);

                pdrag_thumb = 1;
                pdrag_off = th ? pmy - th->y1 : 0;
            } else ppressed = id;
            break;
        case ami_etmoubd:
            if (er->dmoubn != 1) break;
            if (pdrag_thumb) pdrag_thumb = 0;
            else if (ppressed != H_NONE) {
                if (hit(pmx, pmy) == ppressed) pop_click(ppressed);
                ppressed = H_NONE;
            }
            break;
        case ami_etchar:
            if (edit_id != T_STA_QUERY) { if (edit_id >= 0) commit_edit(); begin_edit(T_STA_QUERY); }
            if (er->echar >= ' ' && er->echar < 127 && edit_len < edit_max && edit_len < (int)sizeof edit_buf - 2) {
                edit_buf[edit_len++] = er->echar;
                edit_buf[edit_len] = 0;
            }
            pop_dirty = 1;
            break;
        case ami_etdelcb:
            if (edit_id == T_STA_QUERY && edit_len > 0) { edit_buf[--edit_len] = 0; pop_dirty = 1; }
            break;
        case ami_etenter:
            pop_click(H_STA_SEARCH);
            break;
        case ami_etcan:
            /* Esc drops a changed query first, then closes the window */
            if (edit_id == T_STA_QUERY && strcmp(edit_buf, squery)) { cancel_edit(); begin_edit(T_STA_QUERY); pop_dirty = 1; }
            else pop_close();
            break;
        case ami_etup:
        case ami_etdown:
            r = sanchor < 0 ? 0 : sanchor + (er->etype == ami_etup ? -1 : 1);
            if (r < 0) r = 0;
            if (r >= nsrows) r = nsrows - 1;
            if (r >= 0) { pop_pick(r, (int)ami_modkeys(popwf) & AMI_MOD_SHIFT); pop_show_row(r); }
            break;
        case ami_etleft:
        case ami_etright:
            if (sanchor >= 0 && sanchor < nsrows) {
                r = srows[sanchor].hit;
                sexp[r] = er->etype == ami_etright;
                if (srows[sanchor].k >= 0 && !sexp[r]) {
                    int i;

                    for (i = 0; i < nsrows; i++) if (srows[i].hit == r && srows[i].k < 0) { sanchor = i; break; }
                }
                srows_build();
            }
            break;
        case ami_etpagu: pop_scroll(-POPROWS); break;
        case ami_etpagd: pop_scroll(POPROWS); break;
        case ami_ethome: pop_scroll(-nsrows); break;
        case ami_etend:  pop_scroll(nsrows); break;
        default: break;
    }
    gw = stdout;
    hs = &main_hs;
}

static void scroll_list(int d)
{
    if (page == P_IMAGE) hextop += d;
    else if (page == P_ROUTES) rtop += d;
    else top += d;
    if (top < 0) top = 0;
    if (rtop < 0) rtop = 0;
    if (hextop < 0) hextop = 0;
}

static int page_rows(void)
{
    return page == P_IMAGE ? HEXROWS : NROWS;
}

static void click(int id)
{
    if (id >= H_CYC0) { cycle(id - H_CYC0, mx < (hitbox_of(id)->x1 + hitbox_of(id)->x2) / 2 ? -1 : 1); return; }
    if (id >= H_TEXT0) {
        if (edit_id == id - H_TEXT0) return;
        if (edit_id >= 0) commit_edit();
        begin_edit(id - H_TEXT0);
        return;
    }
    if (id >= H_ROW0 && id < H_ROW0 + NROWS) { select_row((page == P_ROUTES ? rtop : top) + id - H_ROW0); return; }
    if (edit_id >= 0 && id != H_NONE) commit_edit();
    switch (id) {
        case H_READ:    do_read(); break;
        case H_WRITE:   do_write(); break;
        case H_OPEN:    do_open(); break;
        case H_SAVE:    do_save(0); break;
        case H_SAVEAS:  do_save(1); break;
        case H_NEWIMG:  do_new(); break;
        case H_CANCEL:  radio_cancel(&radio); break;
        case H_TAB0: case H_TAB1: case H_TAB2: case H_TAB3: case H_TAB4: case H_TAB5:
            page = id - H_TAB0;
            break;
        case H_SB_UP:   scroll_list(-1); break;
        case H_SB_DN:   scroll_list(1); break;
        case H_SB_TRACK: {
            const hitbox *t = hitbox_of(H_SB_THUMB);

            if (t) scroll_list(my < t->y1 ? -page_rows() : page_rows());
            break;
        }
        case H_MB_NEW:    mb_new(); break;
        case H_MB_DEL:    mb_delete(); break;
        case H_MB_DELALL: mb_delete_all(); break;
        case H_RT_NEW:    rt_new(); break;
        case H_RT_DEL:    rt_delete(); break;
        case H_RT_DELALL: rt_delete_all(); break;
        case H_RT_ADDVIA: case H_RT_RMVIA: case H_RT_CLRVIA: rt_via(id); break;
        case H_LOG_STAT:  if (!radio_start(&radio, OP_LOGSTAT, NULL, NULL)) set_note("The radio is not ready"); break;
        case H_LOG_GET:   do_log_get(); break;
        case H_LOG_FIX:
            logfile[0] = 0;
            if (!radio_start(&radio, OP_LOGREAD, NULL, NULL)) set_note("The radio is not ready");
            break;
        case H_HOST_LOCATE:
            host_locate();
            set_note(host_have ? "This computer's position is on the GPS page" : "The tool kit does not know where this computer is");
            break;
        case H_LOG_ERASE:
            if (!armed(H_LOG_ERASE)) { set_note("Press ERASE again to erase the radio's GPS log"); break; }
            if (!radio_start(&radio, OP_LOGERASE, NULL, NULL)) set_note("The radio is not ready");
            break;
        case H_RESET0: case H_RESET1: case H_RESET2: case H_RESET3: case H_RESET4: case H_RESET5:
            do_reset(id - H_RESET0);
            break;
        case H_SQLKEY:
            {
                char m[200];

                snprintf(m, sizeof m, "To enter CP mode: %s", radio.cphint[0] ? radio.cphint :
                         "switch the radio off, then hold SQL (FTA-850) or MENU (FTA-750/550/450) while switching it on");
                set_note(m);
            }
            break;
        case H_STA_OPEN: pop_open(); break;
        default: break;
    }
}

/* an operation ended */
static void op_done(void)
{
    logpt *pts;
    int n;
    char err[256], m[700];

    radio.finished = 0;
    set_note(radio.result);
    if (!radio.ok) {
        if (!strncmp(radio.result, "The logger holds no data", 24)) {
            log_points = 0;
            if (near_pending && !pop_nearest_host()) pset_note("The radio's GPS log is empty, so its position is unknown: type a code or choose a memory with a position");
        }
        near_pending = 0;
        return;
    }
    if (!strncmp(radio.result, "Read from radio", 15)) {
        memcpy(img, radio.img, CFG_SIZE);
        if (cfg_model(img)) new_model = cfg_model(img);
        have_img = 1;
        modified = 0;
        filename[0] = 0;
        fresh_image();
        if (!cfg_check(img, err, sizeof err)) set_note(err);
    } else if (!strncmp(radio.result, "Program to radio", 16)) {
        modified = 0;
        factory_pending = 0;
    } else if (!strncmp(radio.result, "Logger download", 15)) {
        n = gpslog_decode(radio.log, radio.loglen, &pts);
        if (n > 0) { have_fix = 1; fix_lat = pts[n - 1].lat; fix_lon = pts[n - 1].lon; fix_time = pts[n - 1].time; }
        else log_points = 0;
        if (logfile[0]) {
            if (!gpslog_export(logfile, pts, n, err, sizeof err)) set_note(err);
            else {
                log_points = n;
                snprintf(m, sizeof m, "Saved %d positions to %s", n, logfile);
                set_note(m);
            }
        }
        if (near_pending) {
            near_pending = 0;
            if (have_fix) {
                char pos[48], what[120];

                fmt_fix(fix_lat, fix_lon, pos, sizeof pos);
                snprintf(what, sizeof what, "the radio's last GPS fix, %s", pos);
                pop_nearest_at(fix_lat, fix_lon, what);
            } else if (!pop_nearest_host()) pset_note("The GPS log is empty, so the radio's position is unknown: type a code or choose a memory with a position");
        }
        free(pts);
    }
}

static void usage(const char *prog)
{
    fprintf(stderr, "usage: %s [-p port] [--sim [850|750|550|450]]\n", prog);
    exit(1);
}

int main(int argc, char *argv[])
{
    ami_evtrec er;
    ami_long   wx, wy;
    const char *port = NULL;
    int sim = 0, i, id, dirty, quit = 0;
    double k;
    char title[160];

    gw = stdout;

    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--sim") || !strcmp(argv[i], "-s")) {
            int k = i + 1 < argc ? atoi(argv[i + 1]) : 0;

            sim = k == 750 ? MODEL_FTA750 : k == 550 ? MODEL_FTA550 : k == 450 ? MODEL_FTA450 : MODEL_FTA850;
            if (k == 850 || k == 750 || k == 550 || k == 450) i++;
        }
        else if (!strcmp(argv[i], "-p") && i + 1 < argc) port = argv[++i];
        else usage(argv[0]);
    }

    memset(img, 0xff, sizeof img);
    radio_init(&radio, port, sim);
    if (sta_find_dir(argv[0], sta_dir, sizeof sta_dir)) {
        sta_n = sta_load(sta_dir, sta_err, sizeof sta_err);
        if (sta_n < 0) sta_n = 0;
    } else snprintf(sta_err, sizeof sta_err, "No station data: put the OurAirports files in data/ (see data/README.md)");
#ifdef SHOT
    shot_file = getenv("FTA850_SHOT");
    if (shot_file) { screen_capture_name(shot_file); shot_at = now_ms() + (getenv("FTA850_SHOT_MS") ? atoi(getenv("FTA850_SHOT_MS")) : 3000); }
    if (getenv("FTA850_PAGE")) page = atoi(getenv("FTA850_PAGE")) % NPAGES;
    if (sim) { radio_start(&radio, OP_READ, NULL, NULL); }
#endif

    ami_autohold(FALSE);
    ami_auto(gw, FALSE);
    ami_curvis(gw, FALSE);
    ami_binvis(gw);
    snprintf(title, sizeof title, "Yaesu FTA programmer%s", sim ? " (simulated)" : "");
    ami_title(gw, title);
    ami_autobuf(gw, FALSE);
    k = unit_px(gw);
    ami_winclientg(gw, (ami_long)lround(PANW * k), (ami_long)lround(PANH * k), &wx, &wy,
                   BIT(ami_wmframe) | BIT(ami_wmsize) | BIT(ami_wmsysbar));
    ami_setsizg(gw, wx, wy);
    ami_timer(gw, TICK_ID, TICK, TRUE);
    redraw();

    do {
        ami_event(stdin, &er);
        if (popwf && er.winid == POPWIN) {
            pop_event(&er);
            if (pop_main_dirty) { pop_main_dirty = 0; redraw(); }
            if (popwf && pop_dirty) pop_redraw();
            continue;
        }
        dirty = 1;
        switch (er.etype) {
            case ami_ettim:
                dirty = radio_poll(&radio);
                if (radio.finished) { op_done(); dirty = 1; }
                if (note_until && now_ms() >= note_until) { note_until = 0; dirty = 1; }
                if (popwf) {
                    if (pnote_until && now_ms() >= pnote_until) { pnote_until = 0; pop_dirty = 1; }
                    if (pop_dirty) pop_redraw();
                }
                if (confirm_id != H_NONE && now_ms() >= confirm_until) { confirm_id = H_NONE; dirty = 1; }
                if (busy() || (radio.mode == RM_NORMAL && !radio.connected)) dirty = 1;
#ifdef SHOT
                if (shot_file && have_img && sel < 0 && cfg_mb_count(img) > 3) { sel = 3; rsel = 0; dirty = 1; }
                /* FTA850_POP=<query> pictures the station finder instead, with the
                   query searched (or FTA850_NEAR=1: its nearest list) and a few rows chosen */
                if (shot_file && getenv("FTA850_LOCATE") && !host_done) host_locate();
                if (shot_file && have_img && !popwf && getenv("FTA850_POP")) {
                    pop_open();
                    snprintf(edit_buf, sizeof edit_buf, "%s", getenv("FTA850_POP"));
                    edit_len = (int)strlen(edit_buf);
                    if (getenv("FTA850_NEAR")) pop_nearest(); else pop_click(H_STA_SEARCH);
                    if (nsrows > 3) { pop_pick(1, 0); pop_pick(3, AMI_MOD_SHIFT); }
                    screen_capture_window(popwf);
                }
                if (shot_file && now_ms() >= shot_at) { redraw(); if (popwf) pop_redraw(); screen_capture(); er.etype = ami_etterm; }
#endif
                break;
            case ami_etmoumovg:
                mx = (int)ami_scalex(gw, er.moupxg);
                my = (int)ami_scaley(gw, er.moupyg);
                dirty = 0;
                if (drag_thumb) {
                    const hitbox *tr = hitbox_of(H_SB_TRACK);
                    int n = page == P_IMAGE ? mdl()->size / 16 : page == P_ROUTES ? cfg_rt_count(img) : cfg_mb_count(img);
                    int vis = page_rows();

                    if (tr && n > vis) {
                        const hitbox *th = hitbox_of(H_SB_THUMB);
                        int span = tr->y2 - tr->y1 - (th ? th->y2 - th->y1 : 0);
                        int pos = span > 0 ? (my - drag_off - tr->y1) * (n - vis) / span : 0;

                        if (pos < 0) pos = 0;
                        if (pos > n - vis) pos = n - vis;
                        if (page == P_IMAGE) hextop = pos; else if (page == P_ROUTES) rtop = pos; else top = pos;
                        dirty = 1;
                    }
                }
                break;
            case ami_etmouba:
                if (er.amoubn == 4 || er.amoubn == 5) {
                    int d = er.amoubn == 4 ? -1 : 1;

                    id = hit(mx, my);
                    if (id >= H_CYC0) cycle(id - H_CYC0, d);
                    else scroll_list(d * 3);
                    break;
                }
                if (er.amoubn != 1) { dirty = 0; break; }
                id = hit(mx, my);
                if (id == H_SB_THUMB) {
                    const hitbox *th = hitbox_of(H_SB_THUMB);

                    drag_thumb = 1;
                    drag_off = th ? my - th->y1 : 0;
                    dirty = 0;
                } else pressed = id;
                break;
            case ami_etmoubd:
                if (er.dmoubn == 1 && drag_thumb) drag_thumb = 0;
                else if (er.dmoubn == 1 && pressed != H_NONE) {
                    if (hit(mx, my) == pressed) click(pressed);
                    pressed = H_NONE;
                } else if (er.dmoubn == 1) {
                    if (edit_id >= 0) commit_edit(); else dirty = 0;
                } else dirty = 0;
                break;
            case ami_etchar:
                if (edit_id >= 0) {
                    if (er.echar >= ' ' && er.echar < 127 && edit_len < edit_max && edit_len < (int)sizeof edit_buf - 2) {
                        edit_buf[edit_len++] = er.echar;
                        edit_buf[edit_len] = 0;
                    }
                    break;
                }
                switch (er.echar) {
                    case '1': case '2': case '3': case '4': case '5': case '6':
                        page = er.echar - '1';
                        if (page == P_ROUTES && !mdl()->has_routes) page = P_MEMORY;
                        break;
                    case 'r': case 'R': if (radio.connected && !busy()) do_read(); break;
                    case 'w': case 'W': if (radio.connected && have_img && !busy()) do_write(); break;
                    case 'o': case 'O': if (!busy()) do_open(); break;
                    case 's': case 'S': if (have_img && !busy()) do_save(0); break;
                    case 'n': case 'N':
                        if (have_img && !busy()) { if (page == P_ROUTES) rt_new(); else if (page == P_MEMORY) mb_new(); }
                        break;
                    case 'f': case 'F': if (!busy()) pop_open(); break;
                    default: dirty = 0; break;
                }
                break;
            case ami_etenter:
                if (edit_id >= 0) commit_edit();
                else if (page == P_MEMORY && mb_valid_sel() && !busy()) begin_edit(T_MB_TAG);
                else if (page == P_ROUTES && rt_valid_sel() && !busy()) begin_edit(T_RT_NAME);
                break;
            case ami_etcan:
                if (edit_id >= 0) cancel_edit();
                confirm_id = H_NONE;
                break;
            case ami_etdelcb:
                if (edit_id >= 0 && edit_len > 0) edit_buf[--edit_len] = 0;
                else dirty = 0;
                break;
            case ami_etdel:
            case ami_etdelcf:
                if (edit_id < 0 && page == P_MEMORY && !busy()) mb_delete();
                else if (edit_id < 0 && page == P_ROUTES && !busy()) rt_delete();
                else dirty = 0;
                break;
            case ami_ettab:
                if (edit_id >= 0 && (edit_id < T_RT_NAME || edit_id == T_MB_COURSE)) {
                    int next = edit_id == T_MB_TAG ? T_MB_FREQ :
                               edit_id == T_MB_FREQ ? (mdl()->has_pos ? T_MB_LAT : mdl()->has_course ? T_MB_COURSE : T_MB_TAG) :
                               edit_id == T_MB_LAT ? T_MB_LON :
                               edit_id == T_MB_LON ? (mdl()->has_course ? T_MB_COURSE : T_MB_TAG) : T_MB_TAG;

                    commit_edit();
                    if (mb_valid_sel()) begin_edit(next);
                } else if (edit_id >= T_WX0 && edit_id < T_WX0 + WX_MAX - 1) {
                    int next = edit_id + 1;

                    commit_edit();
                    begin_edit(next);
                } else if (edit_id >= T_GRP0 && edit_id < T_GRP0 + NGROUPS - 1) {
                    int next = edit_id + 1;

                    commit_edit();
                    begin_edit(next);
                } else if (edit_id >= 0) commit_edit();
                else dirty = 0;
                break;
            case ami_etup:
                if (edit_id >= 0) { dirty = 0; break; }
                if (page == P_MEMORY) select_row(sel - 1); else if (page == P_ROUTES) select_row(rsel - 1); else scroll_list(-1);
                break;
            case ami_etdown:
                if (edit_id >= 0) { dirty = 0; break; }
                if (page == P_MEMORY) select_row(sel + 1); else if (page == P_ROUTES) select_row(rsel + 1); else scroll_list(1);
                break;
            case ami_etpagu: scroll_list(-page_rows()); break;
            case ami_etpagd: scroll_list(page_rows()); break;
            case ami_ethome:
                if (page == P_IMAGE) hextop = 0;
                else if (page == P_MEMORY) { top = 0; if (cfg_mb_count(img)) select_row(0); }
                else if (page == P_ROUTES) { rtop = 0; if (cfg_rt_count(img)) select_row(0); }
                break;
            case ami_etend:
                if (page == P_IMAGE) hextop = mdl()->size / 16;
                else if (page == P_MEMORY) select_row(cfg_mb_count(img) - 1);
                else if (page == P_ROUTES) select_row(cfg_rt_count(img) - 1);
                break;
            case ami_etleft:
            case ami_etright:
                if (edit_id < 0 && page == P_MEMORY && mb_valid_sel()) cycle(C_MB_GROUP, er.etype == ami_etleft ? -1 : 1);
                else dirty = 0;
                break;
            case ami_etresize:
                ami_sizbufg(gw, er.rszxg, er.rszyg);
                break;
            case ami_etredraw: break;
            default: dirty = 0; break;
        }
        if (er.etype == ami_etterm) quit = 1;
        else if (dirty) { redraw(); if (popwf) pop_redraw(); }
    } while (!quit);

    pop_close();
    radio_shutdown(&radio);
    return 0;
}
