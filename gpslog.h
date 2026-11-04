/*
 * The FTA-750's GPS logger: the MediaTek LOCUS log the radio's GPS module
 * keeps, as the radio dumps it ($PMTK622). Each 4 kB sector of the dump
 * starts with a 64 byte header and holds 20 byte records: UTC time
 * (seconds since 1970, little endian), fix byte, latitude and longitude
 * (IEEE floats), height (m), speed (m/s), heading (degrees), checksum.
 * Erased records are all FF.
 */
#ifndef GPSLOG_H
#define GPSLOG_H

#include <stddef.h>

typedef struct {
    unsigned long time;   /* seconds since 1970 UTC */
    int    fix;
    double lat, lon;      /* degrees, south and west negative */
    int    height;        /* metres */
    int    speed;         /* m/s */
    int    heading;       /* degrees */
} logpt;

/* Decode a raw dump. Returns the number of points; *out is malloc'd. */
int gpslog_decode(const unsigned char *raw, size_t len, logpt **out);

/* Write the points to a file; the format follows the extension: .nmea
   (GPGGA and GPRMC sentences, as the vendor software writes), .kml,
   .gpx or .csv. Returns nonzero on success. */
int gpslog_export(const char *path, const logpt *p, int n, char *err, size_t errlen);

#endif
