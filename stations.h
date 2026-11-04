/*
 * Station database: airports, heliports and navaids with their frequencies
 * and positions, from the OurAirports public domain CSV files in the data
 * folder (see data/README.md). Used to look up a station by its three or
 * four letter code or its name and add its frequencies to the memory book.
 */
#ifndef STATIONS_H
#define STATIONS_H

#include "cfg.h"

#define STA_IDENT   8
#define STA_NAME    64
#define STA_CITY    40
#define STA_DESC    32
#define STA_MAXFREQ 40     /* frequencies shown for one station */

/* kinds of station */
enum { SK_LARGE, SK_MEDIUM, SK_SMALL, SK_SEAPLANE, SK_HELIPORT, SK_BALLOON, SK_NAVAID, SK_KINDS };

typedef struct {
    int    kind;
    char   ident[STA_IDENT];   /* ICAO or local code (KRHV), or navaid ident (SJC) */
    char   code[STA_IDENT];    /* IATA/local code when different (RHV), or navaid type (VOR-DME) */
    char   name[STA_NAME];
    char   city[STA_CITY];     /* municipality, or the navaid's associated airport */
    char   region[8];          /* US-CA */
    double lat, lon;           /* degrees, south and west negative */
    int    nfreq, freq0;       /* this station's frequencies: count and first index */
} station;

typedef struct {
    char type[STA_IDENT];      /* TWR, GND, ATIS, UNIC, CTAF, APP ... or VOR, NDB ... */
    char desc[STA_DESC];       /* "NORCAL APP" */
    int  khz;
} stafreq;

/* a search hit */
typedef struct {
    int sta;                   /* station index */
    int rank;                  /* 0 exact code, 1 code prefix, 2 name or city */
} stahit;

/* find the data folder: $FTA850_DATA, data by the executable, ./data,
   ~/.local/share/fta850/data. 0 if none; dir gets the path */
int  sta_find_dir(const char *argv0, char *dir, size_t dirlen);

/* load the database from a folder. Returns the number of stations, or -1
   with err set. Loading twice is harmless */
int  sta_load(const char *dir, char *err, size_t errlen);
int  sta_loaded(void);
int  sta_count(void);
const station *sta_get(int i);
const stafreq *sta_freq(const station *s, int k);
const char *sta_kind_name(int kind);          /* "Airport", "VOR-DME" ... */
void sta_kind_desc(const station *s, char *out, size_t outlen);  /* "Large airport", "VORTAC" */

/* search by code or name; hits sorted best first. Returns the number found,
   at most max */
int  sta_search(const char *query, stahit *hits, int max);

/* the memory name a station frequency gets: "KRHV-TWR", "SJC-VOR"; "KRHV"
   for a waypoint (k < 0) */
void sta_tag(const station *s, int k, char *out, size_t outlen);

/* can this frequency go into the radio of this image: 1 yes, 0 out of band,
   -1 needs 8.33 kHz on */
int  sta_freq_usable(const unsigned char *img, int khz, int allow833);

/* a memory book entry for frequency k of a station (k < 0: position only),
   in group g with scan s. The entry gets the station's position */
void sta_entry(const station *s, int k, int group, int scan, mbentry *e);

/* add a station's frequencies (k < 0: all usable ones, else that one) to an
   image. Returns the number added; err gets why the last one was not */
int  sta_add(unsigned char *img, const station *s, int k, int group, int scan, char *err, size_t errlen);

/* add a station as a position only (a waypoint for routes); 1 if added */
int  sta_add_waypoint(unsigned char *img, const station *s, int group, char *err, size_t errlen);

/* is frequency k of a station (k < 0: its waypoint) already in the book */
int  sta_in_book(const unsigned char *img, const station *s, int k);

/* great circle distance in nautical miles */
double sta_distance_nm(double lat1, double lon1, double lat2, double lon2);

/* the stations nearest a position, nearest first, with their distances in
   nautical miles. Heliports and balloonports without a frequency are left
   out. Returns the number found, at most max */
int  sta_nearest(double lat, double lon, stahit *hits, double *dist, int max);

#endif
