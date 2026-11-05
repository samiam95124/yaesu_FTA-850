/*
 * Station database, see stations.h. The OurAirports files are parsed once
 * into arrays; closed airports and empty records are left out. Frequencies
 * are kept in kHz, positions in degrees.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <math.h>
#include <unistd.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <windows.h>
#endif

#include "stations.h"

static station *stas;
static int      nstas, stacap;
static stafreq *freqs;
static int      nfreqs, freqcap;
static int      loaded;

/* --------------------------------------------------------------- csv */

#define MAXCOL 24

/* split one CSV line in place into its columns; returns the count. Quoted
   fields may hold commas and doubled quotes */
static int csv_split(char *line, char *col[], int maxcol)
{
    int n = 0;
    char *p = line, *o;

    while (n < maxcol) {
        o = col[n++] = p;
        if (*p == '"') {
            p++;
            for (;;) {
                if (!*p) break;
                if (*p == '"') {
                    if (p[1] == '"') { *o++ = '"'; p += 2; continue; }
                    p++;
                    break;
                }
                *o++ = *p++;
            }
            while (*p && *p != ',') p++;
        } else {
            while (*p && *p != ',') *o++ = *p++;
        }
        if (*p == ',') { *o = 0; p++; }
        else { *o = 0; break; }
    }
    return n;
}

static void trim_nl(char *s)
{
    size_t n = strlen(s);

    while (n > 0 && (s[n - 1] == '\n' || s[n - 1] == '\r')) s[--n] = 0;
}

static void copy(char *dst, size_t len, const char *src)
{
    snprintf(dst, len, "%s", src);
}

static int find_col(char *col[], int n, const char *name)
{
    int i;

    for (i = 0; i < n; i++) if (!strcmp(col[i], name)) return i;
    return -1;
}

/* ------------------------------------------------------------- tables */

static station *new_station(void)
{
    if (nstas == stacap) {
        stacap = stacap ? stacap * 2 : 4096;
        stas = realloc(stas, stacap * sizeof *stas);
        if (!stas) { fprintf(stderr, "out of memory\n"); exit(1); }
    }
    memset(&stas[nstas], 0, sizeof stas[nstas]);
    return &stas[nstas++];
}

static stafreq *new_freq(void)
{
    if (nfreqs == freqcap) {
        freqcap = freqcap ? freqcap * 2 : 4096;
        freqs = realloc(freqs, freqcap * sizeof *freqs);
        if (!freqs) { fprintf(stderr, "out of memory\n"); exit(1); }
    }
    memset(&freqs[nfreqs], 0, sizeof freqs[nfreqs]);
    return &freqs[nfreqs++];
}

static int kind_of(const char *type)
{
    if (!strcmp(type, "large_airport")) return SK_LARGE;
    if (!strcmp(type, "medium_airport")) return SK_MEDIUM;
    if (!strcmp(type, "small_airport")) return SK_SMALL;
    if (!strcmp(type, "seaplane_base")) return SK_SEAPLANE;
    if (!strcmp(type, "heliport")) return SK_HELIPORT;
    if (!strcmp(type, "balloonport")) return SK_BALLOON;
    return -1;
}

/* OurAirports ids and the station index they got, for linking frequencies */
typedef struct { long id; int sta; } idmap;
static idmap *ids;
static int    nids;

static int cmp_id(const void *a, const void *b)
{
    long x = ((const idmap *)a)->id, y = ((const idmap *)b)->id;

    return x < y ? -1 : x > y;
}

static int sta_of_id(long id)
{
    idmap key = { id, 0 }, *m = bsearch(&key, ids, nids, sizeof *ids, cmp_id);

    return m ? m->sta : -1;
}

/* a link from each station to its run of frequencies: the frequency file
   is sorted by airport, so runs are built as they come */
typedef struct { int sta, k; } frqlink;

static int cmp_link(const void *a, const void *b)
{
    const frqlink *x = a, *y = b;

    return x->sta != y->sta ? x->sta - y->sta : x->k - y->k;
}

static int load_airports(const char *path, char *err, size_t errlen)
{
    FILE *f = fopen(path, "r");
    char line[2048], *col[MAXCOL];
    int n, c_id, c_ident, c_type, c_name, c_lat, c_lon, c_region, c_city, c_icao, c_iata, c_gps, c_local;
    station *s;

    if (!f) { snprintf(err, errlen, "Cannot open %s", path); return 0; }
    if (!fgets(line, sizeof line, f)) { fclose(f); snprintf(err, errlen, "%s is empty", path); return 0; }
    trim_nl(line);
    n = csv_split(line, col, MAXCOL);
    c_id = find_col(col, n, "id"); c_ident = find_col(col, n, "ident"); c_type = find_col(col, n, "type");
    c_name = find_col(col, n, "name"); c_lat = find_col(col, n, "latitude_deg"); c_lon = find_col(col, n, "longitude_deg");
    c_region = find_col(col, n, "iso_region"); c_city = find_col(col, n, "municipality");
    c_icao = find_col(col, n, "icao_code"); c_iata = find_col(col, n, "iata_code");
    c_gps = find_col(col, n, "gps_code"); c_local = find_col(col, n, "local_code");
    if (c_id < 0 || c_ident < 0 || c_type < 0 || c_name < 0 || c_lat < 0 || c_lon < 0) {
        fclose(f);
        snprintf(err, errlen, "%s does not look like the OurAirports airports file", path);
        return 0;
    }
    while (fgets(line, sizeof line, f)) {
        int kind, i;
        const char *code;

        trim_nl(line);
        n = csv_split(line, col, MAXCOL);
        if (n <= c_lon) continue;
        kind = kind_of(col[c_type]);
        if (kind < 0 || !col[c_ident][0]) continue;
        s = new_station();
        s->kind = kind;
        /* the ident: ICAO code when there is one, else what OurAirports uses */
        if (c_icao >= 0 && col[c_icao][0]) copy(s->ident, sizeof s->ident, col[c_icao]);
        else if (c_gps >= 0 && col[c_gps][0]) copy(s->ident, sizeof s->ident, col[c_gps]);
        else copy(s->ident, sizeof s->ident, col[c_ident]);
        /* the other code people know it by */
        code = "";
        if (c_iata >= 0 && col[c_iata][0]) code = col[c_iata];
        else if (c_local >= 0 && col[c_local][0]) code = col[c_local];
        else if (strcmp(col[c_ident], s->ident)) code = col[c_ident];
        if (strcmp(code, s->ident)) copy(s->code, sizeof s->code, code);
        copy(s->name, sizeof s->name, col[c_name]);
        if (c_city >= 0) copy(s->city, sizeof s->city, col[c_city]);
        if (c_region >= 0) copy(s->region, sizeof s->region, col[c_region]);
        s->lat = atof(col[c_lat]);
        s->lon = atof(col[c_lon]);
        for (i = 0; s->ident[i]; i++) s->ident[i] = toupper((unsigned char)s->ident[i]);
        for (i = 0; s->code[i]; i++) s->code[i] = toupper((unsigned char)s->code[i]);
        if (nids % 4096 == 0) {
            ids = realloc(ids, (nids + 4096) * sizeof *ids);
            if (!ids) { fprintf(stderr, "out of memory\n"); exit(1); }
        }
        ids[nids].id = atol(col[c_id]);
        ids[nids].sta = nstas - 1;
        nids++;
    }
    fclose(f);
    qsort(ids, nids, sizeof *ids, cmp_id);
    return 1;
}

static int load_freqs(const char *path, char *err, size_t errlen)
{
    FILE *f = fopen(path, "r");
    char line[1024], *col[MAXCOL];
    int n, c_ref, c_type, c_desc, c_mhz, i, k;
    frqlink *links = NULL;
    int nlinks = 0, cap = 0;
    stafreq *tmp;

    if (!f) { snprintf(err, errlen, "Cannot open %s", path); return 0; }
    if (!fgets(line, sizeof line, f)) { fclose(f); snprintf(err, errlen, "%s is empty", path); return 0; }
    trim_nl(line);
    n = csv_split(line, col, MAXCOL);
    c_ref = find_col(col, n, "airport_ref"); c_type = find_col(col, n, "type");
    c_desc = find_col(col, n, "description"); c_mhz = find_col(col, n, "frequency_mhz");
    if (c_ref < 0 || c_type < 0 || c_mhz < 0) {
        fclose(f);
        snprintf(err, errlen, "%s does not look like the OurAirports frequencies file", path);
        return 0;
    }
    while (fgets(line, sizeof line, f)) {
        int sta, khz;
        stafreq *q;

        trim_nl(line);
        n = csv_split(line, col, MAXCOL);
        if (n <= c_mhz) continue;
        sta = sta_of_id(atol(col[c_ref]));
        khz = (int)lround(atof(col[c_mhz]) * 1000);
        if (sta < 0 || khz <= 0) continue;
        q = new_freq();
        copy(q->type, sizeof q->type, col[c_type]);
        if (c_desc >= 0) copy(q->desc, sizeof q->desc, col[c_desc]);
        for (i = 0; q->type[i]; i++) q->type[i] = toupper((unsigned char)q->type[i]);
        q->khz = khz;
        if (nlinks == cap) {
            cap = cap ? cap * 2 : 4096;
            links = realloc(links, cap * sizeof *links);
            if (!links) { fprintf(stderr, "out of memory\n"); exit(1); }
        }
        links[nlinks].sta = sta;
        links[nlinks].k = nfreqs - 1;
        nlinks++;
    }
    fclose(f);
    /* order the frequencies by station so each station has one run */
    qsort(links, nlinks, sizeof *links, cmp_link);
    tmp = malloc((nfreqs ? nfreqs : 1) * sizeof *tmp);
    if (!tmp) { fprintf(stderr, "out of memory\n"); exit(1); }
    for (i = 0; i < nlinks; i++) {
        tmp[i] = freqs[links[i].k];
        k = links[i].sta;
        if (stas[k].nfreq == 0) stas[k].freq0 = i;
        stas[k].nfreq++;
    }
    free(freqs);
    freqs = tmp;
    nfreqs = nlinks;
    freqcap = nlinks;
    free(links);
    return 1;
}

static int load_navaids(const char *path, char *err, size_t errlen)
{
    FILE *f = fopen(path, "r");
    char line[1024], *col[MAXCOL];
    int n, c_ident, c_name, c_type, c_khz, c_lat, c_lon, c_country, c_apt, i;

    if (!f) { snprintf(err, errlen, "Cannot open %s", path); return 0; }
    if (!fgets(line, sizeof line, f)) { fclose(f); snprintf(err, errlen, "%s is empty", path); return 0; }
    trim_nl(line);
    n = csv_split(line, col, MAXCOL);
    c_ident = find_col(col, n, "ident"); c_name = find_col(col, n, "name"); c_type = find_col(col, n, "type");
    c_khz = find_col(col, n, "frequency_khz"); c_lat = find_col(col, n, "latitude_deg");
    c_lon = find_col(col, n, "longitude_deg"); c_country = find_col(col, n, "iso_country");
    c_apt = find_col(col, n, "associated_airport");
    if (c_ident < 0 || c_name < 0 || c_type < 0 || c_khz < 0 || c_lat < 0 || c_lon < 0) {
        fclose(f);
        snprintf(err, errlen, "%s does not look like the OurAirports navaids file", path);
        return 0;
    }
    while (fgets(line, sizeof line, f)) {
        station *s;
        stafreq *q;

        trim_nl(line);
        n = csv_split(line, col, MAXCOL);
        if (n <= c_lon || !col[c_ident][0]) continue;
        s = new_station();
        s->kind = SK_NAVAID;
        copy(s->ident, sizeof s->ident, col[c_ident]);
        copy(s->code, sizeof s->code, col[c_type]);
        copy(s->name, sizeof s->name, col[c_name]);
        if (c_apt >= 0) copy(s->city, sizeof s->city, col[c_apt]);
        if (c_country >= 0) copy(s->region, sizeof s->region, col[c_country]);
        s->lat = atof(col[c_lat]);
        s->lon = atof(col[c_lon]);
        for (i = 0; s->ident[i]; i++) s->ident[i] = toupper((unsigned char)s->ident[i]);
        if (atoi(col[c_khz]) > 0) {
            q = new_freq();
            copy(q->type, sizeof q->type, col[c_type]);
            copy(q->desc, sizeof q->desc, col[c_name]);
            q->khz = atoi(col[c_khz]);
            s->nfreq = 1;
            s->freq0 = nfreqs - 1;
        }
    }
    fclose(f);
    return 1;
}

/* ---------------------------------------------------------------- api */

static int is_dir(const char *p)
{
    struct stat st;

    return stat(p, &st) == 0 && S_ISDIR(st.st_mode);
}

static int has_data(const char *dir)
{
    char p[1200];

    snprintf(p, sizeof p, "%s/airports.csv", dir);
    return is_dir(dir) && access(p, R_OK) == 0;
}

/* the last path separator: either kind on Windows */
static char *last_sep(char *s)
{
    char *p = strrchr(s, '/');
#ifdef _WIN32
    char *q = strrchr(s, '\\');

    if (q && (!p || q > p)) p = q;
#endif
    return p;
}

int sta_find_dir(const char *argv0, char *dir, size_t dirlen)
{
    const char *e = getenv("FTA850_DATA"), *home = getenv("HOME");
    char exe[1024], *p;
    long n;

    if (e && has_data(e)) { snprintf(dir, dirlen, "%s", e); return 1; }
#ifdef _WIN32
    n = (long)GetModuleFileNameA(NULL, exe, sizeof exe - 1);
#else
    n = (long)readlink("/proc/self/exe", exe, sizeof exe - 1);
#endif
    if (n > 0) {
        exe[n] = 0;
        p = last_sep(exe);
        if (p) { *p = 0; snprintf(dir, dirlen, "%s/data", exe); if (has_data(dir)) return 1; }
    }
    if (argv0 && last_sep((char *)argv0)) {
        snprintf(exe, sizeof exe, "%s", argv0);
        p = last_sep(exe);
        *p = 0;
        snprintf(dir, dirlen, "%s/data", exe);
        if (has_data(dir)) return 1;
    }
    snprintf(dir, dirlen, "data");
    if (has_data(dir)) return 1;
    if (home) {
        snprintf(dir, dirlen, "%s/.local/share/fta850/data", home);
        if (has_data(dir)) return 1;
    }
    dir[0] = 0;
    return 0;
}

int sta_load(const char *dir, char *err, size_t errlen)
{
    char p[1200];

    err[0] = 0;
    if (loaded) return nstas;
    snprintf(p, sizeof p, "%s/airports.csv", dir);
    if (!load_airports(p, err, errlen)) return -1;
    snprintf(p, sizeof p, "%s/airport-frequencies.csv", dir);
    if (!load_freqs(p, err, errlen)) return -1;
    snprintf(p, sizeof p, "%s/navaids.csv", dir);
    if (!load_navaids(p, err, errlen)) return -1;
    free(ids);
    ids = NULL;
    nids = 0;
    loaded = 1;
    return nstas;
}

int sta_loaded(void) { return loaded; }
int sta_count(void) { return nstas; }
const station *sta_get(int i) { return i >= 0 && i < nstas ? &stas[i] : NULL; }

const stafreq *sta_freq(const station *s, int k)
{
    return s && k >= 0 && k < s->nfreq ? &freqs[s->freq0 + k] : NULL;
}

const char *sta_kind_name(int kind)
{
    static const char *names[SK_KINDS] = {
        "Large airport", "Medium airport", "Small airport", "Seaplane base", "Heliport", "Balloonport", "Navaid"
    };

    return kind >= 0 && kind < SK_KINDS ? names[kind] : "?";
}

void sta_kind_desc(const station *s, char *out, size_t outlen)
{
    if (s->kind == SK_NAVAID) snprintf(out, outlen, "%s", s->code);
    else snprintf(out, outlen, "%s", sta_kind_name(s->kind));
}

/* ------------------------------------------------------------- search */

static int kind_order(const station *s)
{
    return s->kind;    /* large airports first, navaids last */
}

static int cmp_hit(const void *a, const void *b)
{
    const stahit *x = a, *y = b;
    const station *p = &stas[x->sta], *q = &stas[y->sta];
    int d;

    if (x->rank != y->rank) return x->rank - y->rank;
    d = kind_order(p) - kind_order(q);
    if (d) return d;
    d = strcmp(p->ident, q->ident);
    if (d) return d;
    return strcmp(p->name, q->name);
}

#ifdef _WIN32
/* MinGW has no strcasestr */
static const char *strcasestr(const char *hay, const char *needle)
{
    size_t n = strlen(needle);

    for (; *hay; hay++)
        if (!strncasecmp(hay, needle, n)) return hay;
    return n ? NULL : hay;
}
#endif

/* case-blind substring test */
static int has_word(const char *hay, const char *needle)
{
    return strcasestr(hay, needle) != NULL;
}

int sta_search(const char *query, stahit *hits, int max)
{
    char q[64];
    int n = 0, i, ql;

    snprintf(q, sizeof q, "%s", query);
    while (q[0] == ' ') memmove(q, q + 1, strlen(q));
    ql = (int)strlen(q);
    while (ql > 0 && q[ql - 1] == ' ') q[--ql] = 0;
    if (!ql || !loaded) return 0;
    for (i = 0; q[i]; i++) q[i] = toupper((unsigned char)q[i]);
    for (i = 0; i < nstas && n < max; i++) {
        const station *s = &stas[i];
        int rank = -1;

        if (!strcmp(s->ident, q) || (s->code[0] && !strcmp(s->code, q))) rank = 0;
        else if (ql >= 2 && (!strncmp(s->ident, q, ql) || (s->code[0] && !strncmp(s->code, q, ql)))) rank = 1;
        else if (ql >= 3 && (has_word(s->name, q) || (s->city[0] && has_word(s->city, q)))) rank = 2;
        if (rank < 0) continue;
        hits[n].sta = i;
        hits[n].rank = rank;
        n++;
    }
    qsort(hits, n, sizeof *hits, cmp_hit);
    return n;
}

/* ---------------------------------------------------------------- adding */

/* the short form of a frequency type for a memory name */
static const char *short_type(const char *t)
{
    static const struct { const char *from, *to; } map[] = {
        { "UNICOM", "UNI" }, { "UNIC", "UNI" }, { "RADIO", "RDO" }, { "INFO", "INFO" },
        { "VOR-DME", "VOR" }, { "VORTAC", "VOR" }, { "NDB-DME", "NDB" }, { "TACAN", "TAC" },
        { NULL, NULL }
    };
    int i;

    for (i = 0; map[i].from; i++) if (!strcasecmp(t, map[i].from)) return map[i].to;
    return t;
}

void sta_tag(const station *s, int k, char *out, size_t outlen)
{
    const stafreq *q = sta_freq(s, k);
    char t[STA_DESC], buf[64];
    int i, j;

    if (!q) snprintf(buf, sizeof buf, "%s", s->ident);
    else {
        snprintf(t, sizeof t, "%s", short_type(q->type));
        snprintf(buf, sizeof buf, "%s-%s", s->ident, t);
    }
    /* only characters the radio shows */
    for (i = j = 0; buf[i] && j < TAG_MAX; i++) {
        int c = toupper((unsigned char)buf[i]);

        if (isalnum(c) || strchr(" *+,/&-[].", c)) out[j++] = c;
    }
    out[j] = 0;
    if (outlen <= (size_t)j) out[outlen - 1] = 0;
}

int sta_freq_usable(const unsigned char *img, int khz, int allow833)
{
    if (cfg_freq_ok(img, khz, allow833)) return 1;
    if (cfg_freq_ok(img, khz, 1)) return -1;
    return 0;
}

static void to_dm(double v, int *deg, int *mmm)
{
    double a = fabs(v);
    int d = (int)a;
    int m = (int)lround((a - d) * 60000);

    if (m >= 60000) { d++; m -= 60000; }
    *deg = d;
    *mmm = m;
}

void sta_entry(const station *s, int k, int group, int scan, mbentry *e)
{
    const stafreq *q = sta_freq(s, k);

    memset(e, 0, sizeof *e);
    e->used = 1;
    e->course = -1;
    sta_tag(s, k, e->tag, sizeof e->tag);
    e->freq = q ? q->khz : 0;
    e->group = group;
    e->scan = q ? scan : 0;
    e->haspos = 1;
    e->ns = s->lat < 0 ? 'S' : 'N';
    e->ew = s->lon < 0 ? 'W' : 'E';
    to_dm(s->lat, &e->lat_deg, &e->lat_mmm);
    to_dm(s->lon, &e->lon_deg, &e->lon_mmm);
}

/* is an entry with this name and frequency already in the book */
static int in_book(const unsigned char *img, const mbentry *e)
{
    int i, n = cfg_mb_count(img);
    mbentry x, t = *e;

    cfg_tag_fit(img, t.tag);    /* the book holds names cut to the model's length */
    for (i = 0; i < n; i++) {
        cfg_mb_get(img, i, &x);
        if (!strcmp(x.tag, t.tag) && x.freq == e->freq) return 1;
    }
    return 0;
}

int sta_in_book(const unsigned char *img, const station *s, int k)
{
    mbentry e;

    sta_entry(s, k, 0, 0, &e);
    return in_book(img, &e);
}

int sta_add_waypoint(unsigned char *img, const station *s, int group, char *err, size_t errlen)
{
    mbentry e;

    err[0] = 0;
    if (!cfg_model_or_default(img)->has_pos) { snprintf(err, errlen, "This radio keeps no positions"); return 0; }
    sta_entry(s, -1, group, 0, &e);
    if (in_book(img, &e)) { snprintf(err, errlen, "%s is already in the memory book", e.tag); return 0; }
    if (cfg_mb_add(img, &e) < 0) { snprintf(err, errlen, "The memory book is full"); return 0; }
    return 1;
}

int sta_add(unsigned char *img, const station *s, int k, int group, int scan, char *err, size_t errlen)
{
    int allow833 = cfg_get(img, cfg_sid833(img)) == 1, added = 0, first, last, i;
    mbentry e;

    err[0] = 0;
    if (!s->nfreq) return sta_add_waypoint(img, s, group, err, errlen);
    if (k >= 0) first = last = k;
    else { first = 0; last = s->nfreq - 1; }
    for (i = first; i <= last; i++) {
        const stafreq *q = sta_freq(s, i);
        int u = sta_freq_usable(img, q->khz, allow833);

        if (u <= 0) {
            if (k >= 0) snprintf(err, errlen, u < 0 ? "%d.%03d MHz needs 8.33 kHz on in SETUP" : "%d.%03d MHz is outside the radio's bands", q->khz / 1000, q->khz % 1000);
            continue;
        }
        sta_entry(s, i, group, scan, &e);
        if (in_book(img, &e)) { snprintf(err, errlen, "%s %d.%03d is already in the memory book", e.tag, e.freq / 1000, e.freq % 1000); continue; }
        if (cfg_mb_add(img, &e) < 0) { snprintf(err, errlen, "The memory book is full"); break; }
        added++;
    }
    if (!added && !err[0]) snprintf(err, errlen, "%s has no frequency the radio can use", s->ident);
    return added;
}

/* ------------------------------------------------------------- nearest */

double sta_distance_nm(double lat1, double lon1, double lat2, double lon2)
{
    const double r = M_PI / 180;
    double dlat = (lat2 - lat1) * r, dlon = (lon2 - lon1) * r;
    double a = sin(dlat / 2) * sin(dlat / 2) + cos(lat1 * r) * cos(lat2 * r) * sin(dlon / 2) * sin(dlon / 2);

    return 2 * asin(sqrt(a)) * 3440.065;
}

typedef struct { int sta; double d; } neardist;

static int cmp_near(const void *a, const void *b)
{
    double x = ((const neardist *)a)->d, y = ((const neardist *)b)->d;

    return x < y ? -1 : x > y;
}

int sta_nearest(double lat, double lon, stahit *hits, double *dist, int max)
{
    neardist *nd = malloc((nstas ? nstas : 1) * sizeof *nd);
    int i, n = 0;

    if (!nd || !loaded) { free(nd); return 0; }
    for (i = 0; i < nstas; i++) {
        const station *s = &stas[i];

        if ((s->kind == SK_HELIPORT || s->kind == SK_BALLOON) && !s->nfreq) continue;
        nd[n].sta = i;
        nd[n].d = sta_distance_nm(lat, lon, s->lat, s->lon);
        n++;
    }
    qsort(nd, n, sizeof *nd, cmp_near);
    if (n > max) n = max;
    for (i = 0; i < n; i++) {
        hits[i].sta = nd[i].sta;
        hits[i].rank = 0;
        dist[i] = nd[i].d;
    }
    free(nd);
    return n;
}
