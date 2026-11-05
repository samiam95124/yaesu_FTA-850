/*
 * FTA-750 GPS logger: decoding the raw LOCUS dump and writing it out. See
 * gpslog.h.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>

#include "gpslog.h"

#define SECTOR 4096
#define HEADER 64
#define RECLEN 20

static float le_float(const unsigned char *p)
{
    unsigned u = p[0] | (unsigned)p[1] << 8 | (unsigned)p[2] << 16 | (unsigned)p[3] << 24;
    float f;

    memcpy(&f, &u, 4);
    return f;
}

int gpslog_decode(const unsigned char *raw, size_t len, logpt **out)
{
    size_t s, o;
    int n = 0, cap = 0;
    logpt *v = NULL, *p;

    *out = NULL;
    for (s = 0; s < len; s += SECTOR) {
        for (o = s + HEADER; o + RECLEN <= len && o + RECLEN <= s + SECTOR; o += RECLEN) {
            const unsigned char *r = raw + o;
            int k, allf = 1;
            unsigned long t;

            for (k = 0; k < RECLEN; k++) if (r[k] != 0xff) { allf = 0; break; }
            if (allf) continue;
            t = r[0] | (unsigned long)r[1] << 8 | (unsigned long)r[2] << 16 | (unsigned long)r[3] << 24;
            if (t == 0xfffffffful) continue;
            if (n == cap) {
                cap = cap ? cap * 2 : 256;
                p = realloc(v, (size_t)cap * sizeof *v);
                if (!p) { free(v); return 0; }
                v = p;
            }
            p = &v[n];
            p->time = t;
            p->fix = r[4];
            p->lat = le_float(r + 5);
            p->lon = le_float(r + 9);
            p->height = (short)(r[13] | r[14] << 8);
            p->speed = r[15] | r[16] << 8;
            p->heading = (short)(r[17] | r[18] << 8);
            if (isnan(p->lat) || isnan(p->lon) || fabs(p->lat) > 90 || fabs(p->lon) > 180) continue;
            n++;
        }
    }
    *out = v;
    return n;
}

static void utc(unsigned long t, struct tm *tm)
{
    time_t tt = (time_t)t;

#ifdef _WIN32
    gmtime_s(tm, &tt);   /* the C runtime's thread safe form; MinGW has no gmtime_r */
#else
    gmtime_r(&tt, tm);
#endif
}

static int nmea_cs(const char *s)
{
    int c = 0;

    for (; *s; s++) c ^= (unsigned char)*s;
    return c;
}

static void xml_escape_init(void) {}

int gpslog_export(const char *path, const logpt *p, int n, char *err, size_t errlen)
{
    const char *ext = strrchr(path, '.');
    FILE *f;
    int i;
    struct tm tm;
    char buf[256];

    xml_escape_init();
    if (!ext) ext = ".gpx";
    f = fopen(path, "w");
    if (!f) { snprintf(err, errlen, "Cannot write %s", path); return 0; }
    if (!strcasecmp(ext, ".nmea") || !strcasecmp(ext, ".nma") || !strcasecmp(ext, ".txt")) {
        for (i = 0; i < n; i++) {
            double la = fabs(p[i].lat), lo = fabs(p[i].lon);
            char lat[24], lon[24], tim[48], dat[24];

            utc(p[i].time, &tm);
            snprintf(tim, sizeof tim, "%02d%02d%02d.000", tm.tm_hour, tm.tm_min, tm.tm_sec);
            snprintf(dat, sizeof dat, "%02d%02d%02d", tm.tm_mday, tm.tm_mon + 1, tm.tm_year % 100);
            snprintf(lat, sizeof lat, "%02d%07.4f", (int)la, (la - (int)la) * 60);
            snprintf(lon, sizeof lon, "%03d%07.4f", (int)lo, (lo - (int)lo) * 60);
            snprintf(buf, sizeof buf, "GPGGA,%s,%s,%c,%s,%c,1,,,%d.0,M,,,,", tim, lat, p[i].lat >= 0 ? 'N' : 'S',
                     lon, p[i].lon >= 0 ? 'E' : 'W', p[i].height);
            fprintf(f, "$%s*%02X\r\n", buf, nmea_cs(buf));
            snprintf(buf, sizeof buf, "GPRMC,%s,A,%s,%c,%s,%c,%.1f,%d.0,%s,,,N", tim, lat, p[i].lat >= 0 ? 'N' : 'S',
                     lon, p[i].lon >= 0 ? 'E' : 'W', p[i].speed * 1.94384449, p[i].heading, dat);
            fprintf(f, "$%s*%02X\r\n", buf, nmea_cs(buf));
        }
    } else if (!strcasecmp(ext, ".kml")) {
        fprintf(f, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
                   "<kml xmlns=\"http://www.opengis.net/kml/2.2\" xmlns:gx=\"http://www.google.com/kml/ext/2.2\">\n"
                   "<Document><open>1</open><Folder><name>Track</name>\n"
                   "<Placemark><name>Track</name><description>Position 1 to Position %d</description>\n<gx:Track>\n", n);
        for (i = 0; i < n; i++) {
            utc(p[i].time, &tm);
            fprintf(f, "<when>%04d-%02d-%02dT%02d:%02d:%02dZ</when>\n", tm.tm_year + 1900, tm.tm_mon + 1,
                    tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);
        }
        for (i = 0; i < n; i++)
            fprintf(f, "<gx:coord>%.7f %.7f %d.0</gx:coord>\n", p[i].lon, p[i].lat, p[i].height);
        fprintf(f, "</gx:Track></Placemark>\n<Folder><name>Speed [km/h]</name><visibility>0</visibility><open>0</open>\n");
        for (i = 0; i < n; i++)
            fprintf(f, "<Placemark><name>Position %d</name><visibility>0</visibility>"
                       "<description>Speed : %.1f km/h</description><Point><coordinates>%.7f,%.7f,%d.0</coordinates></Point></Placemark>\n",
                    i + 1, p[i].speed * 3.6, p[i].lon, p[i].lat, p[i].height);
        fprintf(f, "</Folder></Folder></Document></kml>\n");
    } else if (!strcasecmp(ext, ".csv")) {
        fprintf(f, "time_utc,latitude,longitude,height_m,speed_mps,heading_deg,fix\n");
        for (i = 0; i < n; i++) {
            utc(p[i].time, &tm);
            fprintf(f, "%04d-%02d-%02d %02d:%02d:%02d,%.7f,%.7f,%d,%d,%d,%d\n", tm.tm_year + 1900, tm.tm_mon + 1,
                    tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec, p[i].lat, p[i].lon, p[i].height,
                    p[i].speed, p[i].heading, p[i].fix);
        }
    } else {
        fprintf(f, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
                   "<gpx version=\"1.1\" creator=\"fta750\" xmlns=\"http://www.topografix.com/GPX/1/1\">\n"
                   "<trk><name>FTA-750 log</name><trkseg>\n");
        for (i = 0; i < n; i++) {
            utc(p[i].time, &tm);
            fprintf(f, "<trkpt lat=\"%.7f\" lon=\"%.7f\"><ele>%d</ele><time>%04d-%02d-%02dT%02d:%02d:%02dZ</time>"
                       "<course>%d</course><speed>%d</speed></trkpt>\n", p[i].lat, p[i].lon, p[i].height,
                    tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec,
                    p[i].heading, p[i].speed);
        }
        fprintf(f, "</trkseg></trk></gpx>\n");
    }
    if (ferror(f)) { fclose(f); snprintf(err, errlen, "Write error on %s", path); return 0; }
    fclose(f);
    err[0] = 0;
    return 1;
}
