/*
 * fta850cli: the FTA-850 link from the command line, for scripts and for
 * checking the radio without the panel.
 *
 *   fta850cli [-p PORT] [--sim [MODEL]] info    find the radio, show mode and firmware
 *   fta850cli ... read FILE.dat                 read the radio's memory into a file
 *   fta850cli ... write FILE.dat                program the radio from a file
 *   fta850cli ... show FILE.dat                 list what is in a file
 *   fta850cli ... new FILE.dat [MODEL]          write a factory default file (850, the default, 750, 550 or 450)
 *   fta850cli ... logstat                       how much the GPS logger holds
 *   fta850cli ... log FILE.gpx                  download the GPS log (.gpx .kml .nmea .csv)
 *   fta850cli ... erase                         erase the GPS log
 *   fta850cli search QUERY                      look up airports and navaids (code or name)
 *   fta850cli nearest CODE | LAT LON | here     the stations around a station, a position, or
 *                                               this computer (ami.cfg or its internet address)
 *   fta850cli add FILE.dat IDENT [TYPE,...] [-g N] [-scan]
 *                                               add a station's frequencies to a file's memory
 *                                               book: all usable ones, or the listed types
 *                                               (TWR,GND,ATIS ...), into group N, scan on
 *
 * The station data is the OurAirports public domain database
 * (https://ourairports.com/data/), see data/README.md.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "radio.h"
#include "cfg.h"
#include "gpslog.h"
#include "stations.h"
#include <localdefs.h>
#include <services.h>

static void msleep(int ms)
{
    struct timespec ts = { ms / 1000, (ms % 1000) * 1000000L };

    nanosleep(&ts, NULL);
}

static int wait_connected(radio_state *r, int seconds)
{
    int i;
    char last[256] = "";

    for (i = 0; i < seconds * 20; i++) {
        radio_poll(r);
        if (strcmp(last, r->status)) { fprintf(stderr, "%s\n", r->status); strcpy(last, r->status); }
        if (r->connected) return 1;
        msleep(50);
    }
    return 0;
}

static int run(radio_state *r, radio_op op, const unsigned char *img, const int *areas)
{
    int lastpct = -1;

    if (!radio_start(r, op, img, areas)) {
        fprintf(stderr, "Cannot start: %s\n", r->result[0] ? r->result : r->status);
        return 0;
    }
    while (!r->finished) {
        radio_poll(r);
        if (r->total > 0) {
            int pct = r->progress * 100 / r->total;

            if (pct != lastpct) { fprintf(stderr, "\r%s: %3d%%", radio_opname(op), pct); lastpct = pct; }
        }
        msleep(20);
    }
    if (lastpct >= 0) fprintf(stderr, "\n");
    fprintf(stderr, "%s\n", r->result);
    r->finished = 0;
    return r->ok;
}

static void show(const unsigned char *img)
{
    int i, k, n, s;
    char buf[64], f[16], la[16], lo[16], g[16], tag[16], err[128], ser[20], nm[20];
    mbentry e;
    route rt;

    const cfgmodel *m = cfg_model_or_default(img);
    const setting *st;
    int nst;

    cfg_check(img, err, sizeof err);
    cfg_flash_id(img, buf, sizeof buf);
    cfg_serial(img, ser, sizeof ser);
    printf("%s image, model code %04X", m->longname, cfg_model(img));
    if (m->has_id) printf(", flash ID %s, serial %s", buf, ser);
    printf("%s%s\n", err[0] ? ": " : "", err);
    n = cfg_mb_count(img);
    printf("\nMemory book: %d of %d entries\n", n, m->mb_max);
    printf("   No  Name            Freq     Group      Scan Position                Shift%s\n", m->has_course ? "  RWY" : "");
    for (i = 0; i < n; i++) {
        cfg_mb_get(img, i, &e);
        cfg_fmt_freq(e.freq, f, sizeof f);
        cfg_group_name(img, e.group, g, sizeof g);
        if (e.haspos) {
            cfg_fmt_pos(1, e.lat_deg, e.lat_mmm, la, sizeof la);
            cfg_fmt_pos(0, e.lon_deg, e.lon_mmm, lo, sizeof lo);
            snprintf(buf, sizeof buf, "%s%c %s%c", la, e.ns, lo, e.ew);
        } else buf[0] = 0;
        printf("  %3d  %-15s %-8s %-10s %-4s %-23s %-5s", e.addr, e.tag, f, g, e.scan ? "on" : "", buf,
               e.shift ? "on" : "");
        if (m->has_course && e.course >= 0) printf("  %03d.%02d", e.course / 100, e.course % 100);
        printf("\n");
    }
    n = cfg_rt_count(img);
    if (m->has_routes) printf("\nRoutes: %d of %d\n", n, ROUTE_MAX);
    for (i = 0; i < n; i++) {
        cfg_rt_get(img, i, &rt);
        cfg_group_name(img, rt.group, g, sizeof g);
        printf("  %-15s %-10s from %d", rt.name, g, rt.from);
        for (k = 0; k < rt.nvia; k++) printf(" via %d", rt.via[k]);
        printf(" to %d\n", rt.to);
    }
    printf("\nGroups:");
    for (i = 1; i <= NGROUPS; i++) { cfg_group_name(img, i, nm, sizeof nm); printf(" %d=%s", i, nm); }
    printf("\n\nWeather channels:\n");
    for (i = 0; i < WX_MAX; i++) {
        cfg_wx_get(img, i, tag, &s);
        printf("  WX%02d %d.%03d %-16s %s\n", i + 1, cfg_wx_freq[i] / 1000, cfg_wx_freq[i] % 1000, tag, s ? "scan" : "");
    }
    st = cfg_settings_of(img, &nst);
    for (s = 0; s < 3; s++) {
        if (s == 2 && !m->has_gps) continue;
        printf("\n%s:\n", s == 0 ? "Configuration" : s == 1 ? "COMM setup" : "GPS setup");
        for (i = 0; i < nst; i++) {
            if (st[i].page != s || !cfg_setting_shown(img, i)) continue;
            printf("  %-18s %s\n", st[i].label, cfg_choice(img, i, cfg_get(img, i), buf, sizeof buf));
        }
    }
}

/* the station database, loaded on first use */
static int need_stations(const char *argv0)
{
    char dir[1200], err[256];
    int n;

    if (sta_loaded()) return 1;
    if (!sta_find_dir(argv0, dir, sizeof dir)) {
        fprintf(stderr, "No station data: put the OurAirports CSV files in a data folder by the program (see data/README.md)\n");
        return 0;
    }
    n = sta_load(dir, err, sizeof err);
    if (n < 0) { fprintf(stderr, "%s\n", err); return 0; }
    fprintf(stderr, "%d stations from %s (OurAirports data, https://ourairports.com/data/, public domain)\n", n, dir);
    return 1;
}

/* an image of no particular radio: the bands of an FTA-850 */
static unsigned char noimg[CFG_SIZE];

static void show_station(const unsigned char *img, const station *s, int allow833)
{
    if (!img) { memset(noimg, 0xff, sizeof noimg); img = noimg; }
    int k;
    char kind[32], la[16], lo[16], tag[20];
    mbentry e;

    sta_kind_desc(s, kind, sizeof kind);
    sta_entry(s, -1, 0, 0, &e);
    cfg_fmt_pos(1, e.lat_deg, e.lat_mmm, la, sizeof la);
    cfg_fmt_pos(0, e.lon_deg, e.lon_mmm, lo, sizeof lo);
    printf("%-5s %-5s %-15s %s%s%s%s  %s%c %s%c\n", s->ident, s->code, kind, s->name,
           s->city[0] ? ", " : "", s->city, s->region[0] ? " " : "", la, e.ns, lo, e.ew);
    for (k = 0; k < s->nfreq; k++) {
        const stafreq *q = sta_freq(s, k);
        int u = sta_freq_usable(img, q->khz, allow833);

        sta_tag(s, k, tag, sizeof tag);
        printf("      %-6s %-20s %3d.%03d  %-14s %s\n", q->type, q->desc, q->khz / 1000, q->khz % 1000, tag,
               u > 0 ? "" : u < 0 ? "(needs 8.33 kHz)" : "(outside the radio's bands)");
    }
}

static int do_search(const char *argv0, const char *query)
{
    static stahit hits[400];
    int n, i;

    if (!need_stations(argv0)) return 0;
    n = sta_search(query, hits, 400);
    if (!n) { printf("Nothing matches %s\n", query); return 1; }
    printf("%d match%s%s\n", n, n == 1 ? "" : "es", n == 400 ? " (first 400)" : "");
    for (i = 0; i < n; i++) show_station(NULL, sta_get(hits[i].sta), 1);
    return 1;
}

static int do_nearest(const char *argv0, const char *a, const char *b)
{
    static stahit hits[60];
    static double dist[60];
    double la, lo;
    int n, i;
    char what[460];

    if (!need_stations(argv0)) return 0;
    if (b) {
        la = atof(a);
        lo = atof(b);
        snprintf(what, sizeof what, "%s %s", a, b);
    } else if (!strcmp(a, "here")) {
        ami_long rla = ami_latitude(), rlo = ami_longitude();

        if (!rla && !rlo) { fprintf(stderr, "This computer's position is unknown: set latitude and longitude in ami.cfg or allow the lookup\n"); return 0; }
        la = (double)rla / (double)AMI_LONG_MAX * 90;
        lo = (double)rlo / (double)AMI_LONG_MAX * 180;
        snprintf(what, sizeof what, "this computer, %.4f %.4f, %.0f ft (from ami.cfg if set there, else its internet address: approximate)", la, lo,
                 (double)ami_altitude() / (double)AMI_LONG_MAX * 100000 * 3.28084);
    } else {
        const station *s;

        n = sta_search(a, hits, 60);
        if (!n || hits[0].rank != 0) { fprintf(stderr, "No station has the code %s\n", a); return 0; }
        s = sta_get(hits[0].sta);
        la = s->lat;
        lo = s->lon;
        snprintf(what, sizeof what, "%s %s", s->ident, s->name);
    }
    n = sta_nearest(la, lo, hits, dist, 60);
    printf("%d stations around %s, nearest first\n", n, what);
    for (i = 0; i < n; i++) {
        printf("%6.1f nm  ", dist[i]);
        show_station(NULL, sta_get(hits[i].sta), 1);
    }
    return 1;
}

/* add a station's frequencies to a file's memory book */
static int do_add(const char *argv0, const char *file, const char *ident, const char *types, int group, int scan)
{
    unsigned char *img = malloc(CFG_SIZE);
    char err[256];
    stahit hits[400];
    const station *s;
    int n, i, k, added = 0;

    if (!img) return 0;
    if (!need_stations(argv0)) return 0;
    if (!cfg_load(file, img, err, sizeof err)) { fprintf(stderr, "%s\n", err); return 0; }
    n = sta_search(ident, hits, 400);
    for (i = 0; i < n && hits[i].rank == 0; i++) ;
    if (!i) { fprintf(stderr, "No station has the code %s (try search)\n", ident); return 0; }
    if (i > 1) {
        fprintf(stderr, "%s is ambiguous; these stations have that code:\n", ident);
        for (k = 0; k < i; k++) show_station(img, sta_get(hits[k].sta), 1);
        fflush(stdout);
        fprintf(stderr, "Use the four letter ICAO code for an airport\n");
        return 0;
    }
    s = sta_get(hits[0].sta);
    if (!types) {
        added = sta_add(img, s, -1, group, scan, err, sizeof err);
        if (err[0]) fprintf(stderr, "%s\n", err);
    } else {
        char list[256], *t, *save;

        snprintf(list, sizeof list, "%s", types);
        for (t = strtok_r(list, ",", &save); t; t = strtok_r(NULL, ",", &save)) {
            int found = 0;

            for (k = 0; k < s->nfreq; k++) {
                if (strcasecmp(sta_freq(s, k)->type, t)) continue;
                found = 1;
                added += sta_add(img, s, k, group, scan, err, sizeof err);
                if (err[0]) fprintf(stderr, "%s\n", err);
            }
            if (!found) fprintf(stderr, "%s has no %s frequency\n", s->ident, t);
        }
    }
    if (!added) { fprintf(stderr, "Nothing added\n"); return 0; }
    if (!cfg_save(file, img, err, sizeof err)) { fprintf(stderr, "%s\n", err); return 0; }
    printf("Added %d memor%s from %s to %s\n", added, added == 1 ? "y" : "ies", s->ident, file);
    show(img);
    return 1;
}

static void usage(void)
{
    fprintf(stderr, "usage: fta850cli [-p PORT] [--sim [850|750|550|450]] info | read FILE | write FILE | show FILE | new FILE [MODEL] |"
                    " logstat | log FILE | erase | search QUERY | nearest CODE|LAT LON|here |"
                    " add FILE IDENT [TYPES] [-g N] [-scan]\n");
    exit(2);
}

int main(int argc, char *argv[])
{
    radio_state r;
    const char *port = NULL, *cmd = NULL, *file = NULL, *arg3 = NULL, *arg4 = NULL;
    int sim = 0, i, ok, group = 0, scan = 0;
    unsigned char *img;
    char err[256];

    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--sim")) {
            int k = i + 1 < argc ? atoi(argv[i + 1]) : 0;

            sim = k == 750 ? MODEL_FTA750 : k == 550 ? MODEL_FTA550 : k == 450 ? MODEL_FTA450 : MODEL_FTA850;
            if (k == 850 || k == 750 || k == 550 || k == 450) i++;
        }
        else if (!strcmp(argv[i], "-p") && i + 1 < argc) port = argv[++i];
        else if (!strcmp(argv[i], "-g") && i + 1 < argc) group = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-scan")) scan = 1;
        else if (!cmd) cmd = argv[i];
        else if (!file) file = argv[i];
        else if (!arg3) arg3 = argv[i];
        else if (!arg4) arg4 = argv[i];
        else usage();
    }
    if (!cmd) usage();
    if (group < 0 || group > NGROUPS) { fprintf(stderr, "Group is 0 (all) or 1 to %d\n", NGROUPS); return 1; }
    if (!strcmp(cmd, "search")) {
        if (!file) usage();
        return do_search(argv[0], file) ? 0 : 1;
    }
    if (!strcmp(cmd, "nearest")) {
        if (!file) usage();
        return do_nearest(argv[0], file, arg3) ? 0 : 1;
    }
    if (!strcmp(cmd, "add")) {
        if (!file || !arg3) usage();
        return do_add(argv[0], file, arg3, arg4, group, scan) ? 0 : 1;
    }
    img = malloc(CFG_SIZE);
    if (!img) return 1;

    if (!strcmp(cmd, "show")) {
        if (!file) usage();
        if (!cfg_load(file, img, err, sizeof err)) { fprintf(stderr, "%s\n", err); return 1; }
        if (err[0]) fprintf(stderr, "%s\n", err);
        show(img);
        return 0;
    }
    if (!strcmp(cmd, "new")) {
        const cfgmodel *m = cfg_model_by_code(MODEL_FTA850);

        if (!file) usage();
        if (arg3) {
            int k = atoi(arg3);

            m = cfg_model_by_code(k == 850 ? MODEL_FTA850 : k == 750 ? MODEL_FTA750 : k == 550 ? MODEL_FTA550 : k == 450 ? MODEL_FTA450 : 0);
            if (!m) { fprintf(stderr, "The model is 850, 750, 550 or 450\n"); return 1; }
        }
        cfg_defaults(img, m);
        if (!cfg_save(file, img, err, sizeof err)) { fprintf(stderr, "%s\n", err); return 1; }
        printf("Wrote a factory default %s image to %s\n", m->longname, file);
        return 0;
    }

    radio_init(&r, port, sim);
    if (!wait_connected(&r, 15)) { fprintf(stderr, "No radio in CP mode: %s\n", r.status); return 1; }
    {
        const cfgmodel *m = cfg_model_by_code(r.model);

        printf("%s%s on %s, firmware %s\n", r.sim ? "Simulated " : "", m ? m->longname : "Unknown model", r.port, r.version);
    }
    ok = 1;
    if (!strcmp(cmd, "info")) {
        /* already shown */
    } else if (!strcmp(cmd, "read")) {
        if (!file) usage();
        ok = run(&r, OP_READ, NULL, NULL);
        if (ok) {
            if (!cfg_save(file, r.img, err, sizeof err)) { fprintf(stderr, "%s\n", err); ok = 0; }
            else { printf("Saved %s\n", file); show(r.img); }
        }
    } else if (!strcmp(cmd, "write")) {
        if (!file) usage();
        if (!cfg_load(file, img, err, sizeof err)) { fprintf(stderr, "%s\n", err); return 1; }
        if (err[0]) fprintf(stderr, "%s\n", err);
        ok = run(&r, OP_WRITE, img, cfg_write_areas_of(img, 0));
    } else if (!strcmp(cmd, "logstat")) {
        ok = run(&r, OP_LOGSTAT, NULL, NULL);
    } else if (!strcmp(cmd, "log")) {
        logpt *pts;
        int n;

        if (!file) usage();
        ok = run(&r, OP_LOGREAD, NULL, NULL);
        if (ok) {
            n = gpslog_decode(r.log, r.loglen, &pts);
            if (!gpslog_export(file, pts, n, err, sizeof err)) { fprintf(stderr, "%s\n", err); ok = 0; }
            else printf("Wrote %d points to %s\n", n, file);
            free(pts);
        }
    } else if (!strcmp(cmd, "erase")) {
        ok = run(&r, OP_LOGERASE, NULL, NULL);
    } else usage();
    radio_shutdown(&r);
    return ok ? 0 : 1;
}
