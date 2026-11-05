/*
 * The configuration image of a Yaesu air band handheld: the radio's config
 * memory as the programming port reads it, which is also what Yaesu's own
 * software keeps in its .dat files. Two layouts exist:
 *
 * FTA-850L (YCE46): 32 kB, model code 0352 at 7FFE, and at 0100 in Yaesu's
 * files only: the radio itself keeps 0101 there (firmware 02.02.00). Offsets
 * in hex:
 *   0106  function mode (zeroed by the vendor software)
 *   010A  dual watch (priority) frequency, 3 bytes BCD "118500", FF none
 *   0110  dimmer        0111 lamp        0112 key beep     0113 TX time out
 *   0114  timer alarm   0115 battery save 0116 AF pitch    0117 lock select
 *   0119  mic select    011A/1B side tone (select, level)
 *   0120  emergency call 0121 dual watch 0122 scan         0123 scan stop type
 *   0124  resume time   0125 PTT scan stop 0126 split mode 0127 split frequency
 *   012A  weather alert 012B 8.33 kHz    012C frequency step 012D recall GPS+F
 *   0130  VOX           0131 VOX level   0132 VOX delay    0133 ANL
 *   0134  scan band     0136 noise cancel TX 0137 noise cancel RX
 *   0140  GPS power     0141 GPS power save  0142 location format
 *   0143  time setup: bit7 local, bit6 24h, bits0-5 UTC offset code
 *   0144  speed unit    0145 distance unit   0146 altitude unit
 *   0147  pinning       0148 SBAS        0149 logger on    014A logger interval
 *   0151  arrival range
 *   0170  memory book scan bits, one per entry (400 bits)
 *   01AE  weather channel scan bits (10 bits)
 *   01C0  model name "FTA-850"   01D0 flash ID "AA003N"   01F0 serial number
 *   01DC  PTT lock      01DD mode enable bits: 80 COMM band, 40 memory,
 *         20 NAV, 10 function, 08 GPS, 04 expansion, 02 weather band, 01 timer
 *   0200  calibration (written back as read)
 *   0300  group names, 16 bytes each, 10 used: GROUP1..GROUP9
 *   0420  Bluetooth pairings (written only by a factory reset)
 *   0500  weather channel names, 16 bytes each, 14 used, 10 channels
 *   0600, 0700 per band last used channels (written back as read)
 *   0A00  last used memory, 0A40.. nine recent memories, 64 bytes apart
 *   1500  memory book "has frequency" bits   1540 "has position" bits
 *   1600  memory book "in use" bits; 1600 + 64*g bits of group g (1..9)
 *   1900  route "in use" bits; 1900 + 16*g bits of group g
 *   1A00  routes, 40 records of 64 bytes: start (2), 30 via points (2 each,
 *         stored last first), destination (2); memory numbers, FFFF none
 *   2900  route names, 16 bytes each, 14 used
 *   3000  memory book, 400 records of 48 bytes:
 *         +10 memory number (2, big endian), +12 group (0 all, 1..9),
 *         +13 frequency BCD 3, +16 latitude BCD "DDMMmmm0",
 *         +1A longitude BCD "0DDDMMmmm0", +1F hemispheres 1 NE 2 NW 3 SE
 *         4 SW, +20 name 14 chars, +2F shift 0/1
 *   7FFE  model code: the radio's own copy, the one that identifies it
 *
 * FTA-750L, FTA-550L, FTA-450L (YCE01): 16 kB, model code 02EE, 0226 or
 * 01C2 at 0000 and 3FFE. The same ideas, packed differently:
 *   0005  function mode  000B dual watch frequency BCD 3
 *   0010  dimmer  0011 lamp  0012 contrast  0013 key beep  0014 TX time out
 *   0015  timer alarm  0016 battery save  0017 AF pitch  0018 lock select
 *   001A  mic select  001B/1C side tone  0020 emergency call  0021 dual watch
 *   0022  scan  0024 resume time  0025 PTT scan stop  0026 split mode
 *   0027  split frequency  002A weather alert  002B 8.33 kHz  002C step
 *   0030  VOX  0031 VOX level  0032 VOX delay  0033 ANL  0034 scan band
 *   0040  GPS power  0041 GPS power save  0042 location format  0043 time
 *   0044  speed unit  0045 distance unit  0046 altitude unit  0047 pinning
 *   0048  SBAS  0050 logger on  0051 logger interval  007E mode enable bits
 *   00A0  memory scan bits (200)  00B9 weather scan bits
 *   01C0  last used memory  0270 group names 16 bytes each, 10 used: FLAG
 *         then GROUP1..GROUP8  03C0 "has frequency" bits  03E0 "has
 *         position" bits  0400 weather names 16 bytes each, 15 used
 *   0700  eight recent memories, 64 bytes apart
 *   0D00  "in use" bits; 0D00 + 32*g bits of group g (1..9)
 *   1900  memory book, 200 records of 48 bytes: +0D runway true course BCD
 *         "dddDD0", +10 memory number (1 byte), +11 group, +12 frequency,
 *         +15 latitude, +19 longitude, +1E hemispheres, +20 name 15 chars,
 *         +2F shift
 *   3FFE  model code again
 * The FTA-450L has no NAV band or positions, the FTA-550L no GPS; none of
 * the three has routes. The FTA-850L enters CP mode with SQL held while
 * switching on, the others with MENU held.
 *
 * Strings are ASCII padded with FF. Unused bytes are FF. Entries of the
 * memory book are kept contiguous from index 0 and sorted by name, as the
 * vendor software keeps them; each keeps its memory number, which is what
 * the radio shows and what routes refer to.
 *
 * The vendor software writes only parts of the image back to the radio,
 * leaving the identity and the Bluetooth pairings alone; a factory reset on
 * the 850 writes a little more. Every image buffer is CFG_SIZE bytes; a
 * 16 kB model uses the first half and the rest stays FF.
 */
#ifndef CFG_H
#define CFG_H

#include <stddef.h>
#include "radio.h"

#define MB_MAX    400  /* the most memories any model has */
#define WX_MAX    10
#define TAG_MAX   15   /* the longest name any model has */
#define GRP_MAX   10   /* group name length */
#define NGROUPS   9
#define ROUTE_MAX 40
#define VIA_MAX   30
#define RECENT_N  9
#define NRESETS_MAX 6

#define MB_RECLEN    48
#define RT_RECLEN    64

#define MODEL_FTA850 0x0352
#define MODEL_FTA750 0x02EE
#define MODEL_FTA550 0x0226
#define MODEL_FTA450 0x01C2

/* kinds of setting */
typedef enum { K_ENUM, K_FREQ, K_SIDETONE, K_TIMEDISP, K_TIMEFMT, K_TIMEOFF, K_BIT, K_ARRIVAL } setkind;

/* what a setting needs the radio to have */
#define NEEDS_NAV 1
#define NEEDS_GPS 2

typedef struct {
    const char *label;
    int         off;
    setkind     kind;
    int         page;         /* 0 configuration, 1 COMM, 2 GPS */
    int         n;            /* number of choices */
    const char *const *names; /* choice names, NULL: the value itself */
    const unsigned char *vals;/* stored value of each choice */
    int         mask;         /* K_BIT: the bit */
    int         needs;        /* NEEDS_* bits, 0 for every model */
} setting;

/* a model: its features and where everything is in its image */
typedef struct {
    int   code;               /* model code */
    const char *name;         /* "FTA-750" */
    const char *longname;     /* "FTA-750L" */
    const char *cpkey;        /* the key held while switching on for CP mode */
    int   size;               /* image bytes */
    int   mb_max;             /* memories */
    int   tag_max;            /* name length */
    int   has_nav, has_gps, has_routes, has_pos, has_course, has_id;
    int   group_edit_from;    /* lowest group whose name can be changed */
    int   recent_n;           /* recent memory slots */
    int   nresets;
    const char *const *reset_names;
    const char *const *reset_short;
    /* layout */
    int   o_model, o_footer;
    int   o_mbook, o_mbused, mb_bitstride, o_mbfreq, o_mbpos, o_mbscan;
    int   o_groups, o_wxnames, o_wxscan, o_lastmem, o_recent;
    int   o_rtused, o_routes, o_rtnames;
    int   o_modebits, o_flashid, o_serial;
    int   o_833, o_fstep, o_splitmode, o_splitfreq, o_logger, o_gpspower, o_scanband, o_distu, o_stsel, o_stlevel;
    /* memory record fields */
    int   r_addr, addr_len, r_group, r_freq, r_lat, r_lon, r_hemi, r_tag, r_shift, r_course;
    /* tables */
    const setting *settings;
    int   nsettings;
    const int *write_areas, *write_areas_factory;
    const int *const *resets;
} cfgmodel;

typedef struct {
    int  used;            /* entry exists */
    int  addr;            /* memory number 1..400, what the radio shows */
    char tag[TAG_MAX + 1];
    int  freq;            /* kHz, 0 none */
    int  group;           /* 0 all, 1..9 */
    int  scan;            /* in memory scan */
    int  haspos;
    char ns, ew;          /* 'N'/'S', 'E'/'W' */
    int  lat_deg, lat_mmm;    /* minutes in thousandths */
    int  lon_deg, lon_mmm;
    int  shift;           /* spurious shift 0/1 */
    int  course;          /* runway true course in hundredths of a degree, -1 none (750 family) */
} mbentry;

typedef struct {
    int  used;
    char name[TAG_MAX + 1];
    int  group;           /* 1..9 */
    int  from, to;        /* memory numbers, 0 none */
    int  nvia;
    int  via[VIA_MAX];    /* memory numbers in travelling order */
} route;

extern const int cfg_wx_freq[WX_MAX];     /* kHz */

/* models */
const cfgmodel *cfg_model_of(const unsigned char *img);   /* by the image's header, NULL unknown */
const cfgmodel *cfg_model_by_code(int code);              /* NULL unknown */
const cfgmodel *cfg_model_list(int i);                    /* the i'th known model, NULL past the end */
const cfgmodel *cfg_model_or_default(const unsigned char *img);  /* the image's, else the FTA-850 */
int  cfg_model(const unsigned char *img);                 /* the image's model code, 0 unknown */
int  cfg_check(const unsigned char *img, char *err, size_t errlen);
void cfg_flash_id(const unsigned char *img, char *out, size_t outlen);  /* "" if the model has none */
void cfg_serial(const unsigned char *img, char *out, size_t outlen);

/* settings of the image's model: index into cfg_settings_of(); a value is a
   choice index for K_ENUM-like kinds, kHz (0 none) for K_FREQ. -1 if the
   stored value is not a choice. A setting whose needs the model lacks is
   not shown */
const setting *cfg_settings_of(const unsigned char *img, int *n);
int  cfg_setting_shown(const unsigned char *img, int sid);
int  cfg_sid833(const unsigned char *img);
int  cfg_get(const unsigned char *img, int sid);
void cfg_set(unsigned char *img, int sid, int v);
const char *cfg_choice(const unsigned char *img, int sid, int v, char *buf, size_t buflen);

/* memory book */
int  cfg_mb_count(const unsigned char *img);
void cfg_mb_get(const unsigned char *img, int i, mbentry *e);
void cfg_mb_set(unsigned char *img, int i, const mbentry *e);
int  cfg_mb_add(unsigned char *img, mbentry *e);          /* gives it a number; index, or -1 if full */
int  cfg_mb_delete(unsigned char *img, int i, char *err, size_t errlen);  /* 0 if a route uses it */
void cfg_mb_clear(unsigned char *img);
int  cfg_mb_normalize(unsigned char *img, const mbentry *keep); /* sort; index of keep, or -1 */
int  cfg_mb_valid(const unsigned char *img, const mbentry *e, int allow833, char *err, size_t errlen);
int  cfg_mb_find(const unsigned char *img, int addr);     /* index of memory number, -1 */
int  cfg_mb_next_pos(const unsigned char *img, int addr, int dir); /* next memory number with a position after addr (0 wraps), 0 none */
void cfg_tag_fit(const unsigned char *img, char *tag);    /* cut a name to the model's length */

/* routes (FTA-850 only; the others count 0 and refuse to add) */
int  cfg_rt_count(const unsigned char *img);
void cfg_rt_get(const unsigned char *img, int i, route *r);
void cfg_rt_set(unsigned char *img, int i, const route *r);
int  cfg_rt_add(unsigned char *img, const route *r);       /* index, or -1 if full */
void cfg_rt_delete(unsigned char *img, int i);
void cfg_rt_clear(unsigned char *img);
int  cfg_rt_normalize(unsigned char *img, const route *keep);
int  cfg_rt_valid(const unsigned char *img, const route *r, char *err, size_t errlen);
int  cfg_rt_uses(const unsigned char *img, int addr);     /* index of a route using the memory, -1 */

/* weather channels and group names */
void cfg_wx_get(const unsigned char *img, int i, char *tag, int *scan);
void cfg_wx_set(unsigned char *img, int i, const char *tag, int scan);
void cfg_group_name(const unsigned char *img, int g, char *out, size_t outlen);
void cfg_set_group_name(unsigned char *img, int g, const char *name);

/* text and number checks */
int  cfg_tag_ok(const char *s);
int  cfg_freq_ok(const unsigned char *img, int khz, int allow833);
int  cfg_parse_freq(const char *s);                 /* "118.500" -> 118500, -1 bad */
void cfg_fmt_freq(int khz, char *out, size_t outlen);
int  cfg_parse_pos(const char *s, int lat, int *deg, int *mmm);  /* "37 19.583" */
void cfg_fmt_pos(int lat, int deg, int mmm, char *out, size_t outlen);

/* what a write sends: the model's areas, or a factory write's */
const int *cfg_write_areas_of(const unsigned char *img, int factory);

/* defaults and resets. which indexes the model's reset list; the last one
   is always the factory reset */
void cfg_defaults(unsigned char *img, const cfgmodel *m);
int  cfg_nresets(const unsigned char *img);
const char *cfg_reset_name(const unsigned char *img, int which, int shortname);
void cfg_reset(unsigned char *img, int which);

/* .dat files: the raw image, 16 or 32 kB by the model */
int  cfg_load(const char *path, unsigned char *img, char *err, size_t errlen);
int  cfg_save(const char *path, const unsigned char *img, char *err, size_t errlen);

/* the model's default images, generated from the vendor files */
void cfg_defaults_850(unsigned char *img);
void cfg_defaults_750(unsigned char *img);

#endif
