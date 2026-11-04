/*
 * Configuration image codec for the FTA-850L and the FTA-750L family. See
 * cfg.h for the layouts. Every function finds the image's model from its
 * header and works from that model's table; an image nobody recognises is
 * treated as an FTA-850's, so a blank buffer behaves.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "cfg.h"

/* ---------------------------------------------------------------- choice tables */

static const char *const n_onoff[]   = { "Off", "On" };
static const char *const n_used[]    = { "Not used", "Used" };
static const unsigned char v01[]     = { 0, 1 };
static const unsigned char v_0to2[]  = { 0, 1, 2 };
static const unsigned char v_1to3[]  = { 1, 2, 3 };
static const unsigned char v_0to4[]  = { 0, 1, 2, 3, 4 };
static const unsigned char v_1to4[]  = { 1, 2, 3, 4 };
static const unsigned char v_0to5[]  = { 0, 1, 2, 3, 4, 5 };
static const unsigned char v_1to5[]  = { 1, 2, 3, 4, 5 };
static const unsigned char v_1to7[]  = { 1, 2, 3, 4, 5, 6, 7 };
static const unsigned char v_0to7[]  = { 0, 1, 2, 3, 4, 5, 6, 7 };
static const unsigned char v_0to15[] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 };
static const unsigned char v_1to30[] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20,
                                         21, 22, 23, 24, 25, 26, 27, 28, 29, 30 };
static const char *const n_dimmer[]  = { "Level 1", "Level 2", "Level 3", "Level 4", "Level 5", "Level 6", "Level 7" };
static const char *const n_dimmer7[] = { "Off", "Level 1", "Level 2", "Level 3", "Level 4", "Level 5" };
static const char *const n_lamp[]    = { "Continuous", "5 sec", "10 sec", "30 sec", "60 sec", "120 sec", "180 sec", "EXP PO/Key On" };
static const char *const n_lamp7[]   = { "Off", "Continuous", "5 sec", "10 sec", "30 sec", "60 sec", "120 sec", "300 sec", "EXP PO/Key On" };
static const unsigned char v_lamp7[] = { 0, 2, 1, 4, 5, 6, 7, 8, 3 };
static const char *const n_beep[]    = { "Off", "1", "2", "3", "4", "5", "6", "7" };
static const char *const n_beep7[]   = { "Off", "1", "2", "3", "4", "5" };
static const char *const n_tot[]     = { "1 min", "2 min", "3 min", "4 min", "5 min" };
static const char *const n_tot7[]    = { "Off", "1 min", "2 min", "3 min", "4 min", "5 min" };
static const char *const n_batsave[] = { "Off", "50%", "70%", "80%", "90%" };
static const char *const n_afpitch[] = { "Normal", "LOW CUT", "HIGH CUT", "HIGH AND LOW CUT" };
static const char *const n_lock[]    = { "Key Lock", "Dial Lock", "ALL" };
static const char *const n_pttlock[] = { "ALL Unlock", "INTERNAL Lock", "EXTERNAL Lock", "ALL Lock" };
static const unsigned char v_pttlock[] = { 17, 16, 1, 0 };
static const char *const n_mic[]     = { "EXT MIC", "INT MIC" };
static const char *const n_sidetone[] = { "Off", "MIN", "Level 1", "Level 2", "MAX" };
static const char *const n_resume[]  = { "0 sec", "1 sec", "2 sec", "3 sec", "5 sec" };
static const unsigned char v_resume[] = { 0, 1, 2, 3, 5 };
static const char *const n_scanstop[] = { "BUSY STOP", "5 sec STOP", "10 sec STOP" };
static const char *const n_scanband[] = { "ALL BAND", "COMM BAND", "NAVI BAND" };
static const char *const n_ncrx[]    = { "Off", "Level 1", "Level 2", "Level 3", "Level 4" };
static const char *const n_voxlevel[] = { "MIN", "Level 1", "Level 2", "Level 3", "MAX" };
static const char *const n_voxdelay[] = { "0.5 sec", "1.0 sec", "1.5 sec", "2.0 sec", "3.0 sec" };
static const unsigned char v_voxdelay7[] = { 0, 1, 2, 3, 5 };
static const char *const n_gpssave[] = { "Off", "Auto", "50%", "75%", "90%" };
static const char *const n_locfmt[]  = { "DDD MM.MMM", "DDD MM'SS\"", "DDD.DDDDD" };
static const char *const n_locfmt7[] = { "DDD MM'SS\"", "DDD MM.MM", "DDD MM.MMM" };
static const char *const n_timedisp[] = { "UTC", "Local" };
static const char *const n_timefmt[] = { "12H", "24H" };
static const char *const n_dist[]    = { "Nautical Mile", "Statute Mile", "Kilometer" };
static const char *const n_speed[]   = { "knots", "mph", "km/h" };
static const char *const n_alt[]     = { "Feet", "Meters" };
static const char *const n_logint[]  = { "1 sec", "3 sec", "5 sec", "10 sec", "30 sec" };
static const char *const n_arr_nm[]  = { "164 feet", "328 feet", "656 feet", "984 feet", "1640 feet", "1969 feet",
    "2297 feet", "2625 feet", "2953 feet", "0.54 mile", "0.81 mile", "1.08 mile", "1.62 mile", "2.16 mile", "2.70 mile", "3.24 mile" };
static const char *const n_arr_sm[]  = { "164 feet", "328 feet", "656 feet", "984 feet", "1640 feet", "1969 feet",
    "2297 feet", "2625 feet", "2953 feet", "0.62 mile", "0.93 mile", "1.24 mile", "1.86 mile", "2.48 mile", "3.11 mile", "3.73 mile" };
static const char *const n_arr_km[]  = { "50 m", "100 m", "200 m", "300 m", "500 m", "600 m", "700 m", "800 m",
    "900 m", "1 km", "1.5 km", "2 km", "3 km", "4 km", "5 km", "6 km" };

/* UTC offset codes: 24..1 are -12:00 .. -0:30, 0 is 0:00, 33..60 are +0:30 .. +14:00 */
#define NTZ 53

/* ---------------------------------------------------------------- FTA-850L */

static const setting s850[] = {
    /* configuration */
    { "Dimmer",           0x0110, K_ENUM, 0, 7, n_dimmer, v_1to7, 0, 0 },
    { "Lamp",             0x0111, K_ENUM, 0, 8, n_lamp, v_0to7, 0, 0 },
    { "Key beep",         0x0112, K_ENUM, 0, 8, n_beep, v_0to7, 0, 0 },
    { "Timer alarm",      0x0114, K_ENUM, 0, 2, n_onoff, v01, 0, 0 },
    { "Battery save",     0x0115, K_ENUM, 0, 5, n_batsave, v_0to4, 0, 0 },
    { "Lock select",      0x0117, K_ENUM, 0, 3, n_lock, v_1to3, 0, 0 },
    { "PTT lock",         0x01DC, K_ENUM, 0, 4, n_pttlock, v_pttlock, 0, 0 },
    /* COMM */
    { "COMM band",        0x01DD, K_BIT,  1, 2, n_used, v01, 0x80, 0 },
    { "8.33 kHz step",    0x012B, K_ENUM, 1, 2, n_onoff, v01, 0, 0 },
    { "Weather band",     0x01DD, K_BIT,  1, 2, n_used, v01, 0x02, 0 },
    { "Weather alert",    0x012A, K_ENUM, 1, 2, n_onoff, v01, 0, 0 },
    { "Emergency 121.5",  0x0120, K_ENUM, 1, 2, n_onoff, v01, 0, 0 },
    { "Noise limiter",    0x0133, K_ENUM, 1, 2, n_onoff, v01, 0, 0 },
    { "Noise cancel TX",  0x0136, K_ENUM, 1, 2, n_onoff, v01, 0, 0 },
    { "Noise cancel RX",  0x0137, K_ENUM, 1, 5, n_ncrx, v_0to4, 0, 0 },
    { "Dual watch freq",  0x010A, K_FREQ, 1, 0, NULL, NULL, 0, 0 },
    { "Split TX freq",    0x0127, K_FREQ, 1, 0, NULL, NULL, 0, 0 },
    { "Scan",             0x0122, K_ENUM, 1, 2, n_onoff, v01, 0, 0 },
    { "Scan resume",      0x0124, K_ENUM, 1, 5, n_resume, v_resume, 0, 0 },
    { "Scan stop type",   0x0123, K_ENUM, 1, 3, n_scanstop, v_0to2, 0, 0 },
    { "PTT scan stop",    0x0125, K_ENUM, 1, 2, n_onoff, v01, 0, 0 },
    { "Dual watch",       0x0121, K_ENUM, 1, 2, n_onoff, v01, 0, 0 },
    { "Scan band",        0x0134, K_ENUM, 1, 3, n_scanband, v_0to2, 0, 0 },
    { "TX time out",      0x0113, K_ENUM, 1, 5, n_tot, v_1to5, 0, 0 },
    { "MIC select",       0x0119, K_ENUM, 1, 2, n_mic, v01, 0, 0 },
    { "Side tone",        0x011A, K_SIDETONE, 1, 5, n_sidetone, v_0to4, 0, 0 },
    { "AF pitch",         0x0116, K_ENUM, 1, 4, n_afpitch, v_1to4, 0, 0 },
    { "VOX",              0x0130, K_ENUM, 1, 2, n_onoff, v01, 0, 0 },
    { "VOX level",        0x0131, K_ENUM, 1, 5, n_voxlevel, v_0to4, 0, 0 },
    { "VOX delay",        0x0132, K_ENUM, 1, 5, n_voxdelay, v_0to4, 0, 0 },
    /* GPS */
    { "GPS unit power",   0x0140, K_ENUM, 2, 2, n_onoff, v01, 0, NEEDS_GPS },
    { "GPS power save",   0x0141, K_ENUM, 2, 5, n_gpssave, v_0to4, 0, NEEDS_GPS },
    { "Location format",  0x0142, K_ENUM, 2, 3, n_locfmt, v_0to2, 0, NEEDS_GPS },
    { "Time display",     0x0143, K_TIMEDISP, 2, 2, n_timedisp, v01, 0, NEEDS_GPS },
    { "Time format",      0x0143, K_TIMEFMT, 2, 2, n_timefmt, v01, 0, NEEDS_GPS },
    { "UTC offset",       0x0143, K_TIMEOFF, 2, NTZ, NULL, NULL, 0, NEEDS_GPS },
    { "Distance unit",    0x0145, K_ENUM, 2, 3, n_dist, v_0to2, 0, NEEDS_GPS },
    { "Speed unit",       0x0144, K_ENUM, 2, 3, n_speed, v_0to2, 0, NEEDS_GPS },
    { "Altitude unit",    0x0146, K_ENUM, 2, 2, n_alt, v01, 0, NEEDS_GPS },
    { "Arrival range",    0x0151, K_ARRIVAL, 2, 16, n_arr_nm, v_0to15, 0, NEEDS_GPS },
    { "Recall GPS + F",   0x012D, K_ENUM, 2, 2, n_onoff, v01, 0, NEEDS_GPS },
    { "Pinning",          0x0147, K_ENUM, 2, 2, n_onoff, v01, 0, NEEDS_GPS },
    { "SBAS (WAAS)",      0x0148, K_ENUM, 2, 2, n_onoff, v01, 0, NEEDS_GPS },
    { "Logger interval",  0x014A, K_ENUM, 2, 5, n_logint, v_1to5, 0, NEEDS_GPS },
};

/* what the vendor software writes back; everything else stays as the radio has it */
static const int wa850[] = {
    0x0102, 0x01B0, 0x01DC, 0x01E0, 0x0200, 0x0220, 0x0300, 0x0400, 0x0406, 0x0420,
    0x0500, 0x05A0, 0x0600, 0x0680, 0x0700, 0x0980, 0x0A00, 0x0C80, 0x0D00, 0x0F80,
    0x1000, 0x1280, 0x12C0, 0x1580, 0x1600, 0x2400, 0x2900, 0x2B80, 0x3000, 0x7B00,
    0x7C00, 0x7E00, -1
};
static const int wa850f[] = {
    0x0103, 0x010D, 0x010E, 0x011D, 0x0120, 0x012F, 0x0130, 0x0139, 0x013E, 0x014D,
    0x0150, 0x0152, 0x0160, 0x0167, 0x0170, 0x01A2, 0x01AE, 0x01B0, 0x01DC, 0x01DD,
    0x0300, 0x030A, 0x0310, 0x031A, 0x0320, 0x032A, 0x0330, 0x033A, 0x0340, 0x034A,
    0x0350, 0x035A, 0x0360, 0x036A, 0x0370, 0x037A, 0x0380, 0x038A, 0x0400, 0x0406,
    0x0420, 0x04C0, 0x0500, 0x05A0, 0x0600, 0x0680, 0x0700, 0x0940, 0x0A00, 0x0C80,
    0x0D00, 0x0F80, 0x1000, 0x1280, 0x12C0, 0x1532, 0x1540, 0x1572, 0x1600, 0x1632,
    0x1640, 0x1672, 0x1680, 0x16B2, 0x16C0, 0x16F2, 0x1700, 0x1732, 0x1740, 0x1772,
    0x1780, 0x17B2, 0x17C0, 0x17F2, 0x1800, 0x1832, 0x1840, 0x1872, 0x1900, 0x1905,
    0x1910, 0x1915, 0x1920, 0x1925, 0x1930, 0x1935, 0x1940, 0x1945, 0x1950, 0x1955,
    0x1960, 0x1965, 0x1970, 0x1975, 0x1980, 0x1985, 0x1990, 0x1995, 0x19B0, 0x19D0,
    0x1A00, 0x2400, 0x2900, 0x2B80, 0x3000, 0x7B00, -1
};

/* the bytes each reset takes from the factory image, as the vendor's masks */
static const int r850_function[] = { 0x0106, 0x0107, 0x0126, 0x0127, 0x0149, 0x014a, -1 };
static const int r850_memory[] = { 0x0104, 0x0107, 0x0150, 0x0152, 0x0160, 0x0167, 0x0170, 0x01a2,
    0x0300, 0x030a, 0x0310, 0x031a, 0x0320, 0x032a, 0x0330, 0x033a, 0x0340, 0x034a, 0x0350, 0x035a,
    0x0360, 0x036a, 0x0370, 0x037a, 0x0380, 0x038a, 0x0600, 0x0680, 0x0700, 0x0940, 0x0a00, 0x0c80,
    0x0d00, 0x0f80, 0x1000, 0x1280, 0x12c0, 0x1580, 0x1600, 0x1880, 0x1900, 0x1905, 0x1910, 0x1915,
    0x1920, 0x1925, 0x1930, 0x1935, 0x1940, 0x1945, 0x1950, 0x1955, 0x1960, 0x1965, 0x1970, 0x1975,
    0x1980, 0x1985, 0x1990, 0x1995, 0x19b0, 0x19d0, 0x1a00, 0x2400, 0x2900, 0x2b80, 0x3000, 0x7b00, -1 };
static const int r850_comm[] = { 0x0104, 0x0107, 0x0109, 0x010d, 0x0113, 0x0114, 0x0116, 0x0117,
    0x0119, 0x011c, 0x0120, 0x0121, 0x0123, 0x012d, 0x0130, 0x0135, 0x0136, 0x0139, 0x013e, 0x0140,
    0x0160, 0x0167, 0x0500, 0x05a0, 0x0600, 0x0680, -1 };
static const int r850_gps[] = { 0x0104, 0x0105, 0x0107, 0x0108, 0x012d, 0x012e, 0x0140, 0x014d, 0x0151, 0x0152, -1 };
static const int r850_config[] = { 0x0104, 0x0109, 0x010e, 0x0113, 0x0114, 0x0116, 0x0117, 0x0118,
    0x011c, 0x011d, 0x0135, 0x0136, 0x01dc, 0x01dd, 0x0400, 0x0406, 0x0420, 0x04c0, -1 };
static const int r850_factory[] = { 0x0103, 0x010d, 0x010e, 0x011d, 0x0120, 0x012f, 0x0130, 0x0139,
    0x013e, 0x014d, 0x0150, 0x0152, 0x0160, 0x0167, 0x0170, 0x01a2, 0x01ae, 0x01b0, 0x01dc, 0x01dd,
    0x0300, 0x030a, 0x0310, 0x031a, 0x0320, 0x032a, 0x0330, 0x033a, 0x0340, 0x034a, 0x0350, 0x035a,
    0x0360, 0x036a, 0x0370, 0x037a, 0x0380, 0x038a, 0x0400, 0x0406, 0x0420, 0x04c0, 0x0500, 0x05a0,
    0x0600, 0x0680, 0x0700, 0x0940, 0x0a00, 0x0c80, 0x0d00, 0x0f80, 0x1000, 0x1280, 0x12c0, 0x1532,
    0x1540, 0x1572, 0x1600, 0x1632, 0x1640, 0x1672, 0x1680, 0x16b2, 0x16c0, 0x16f2, 0x1700, 0x1732,
    0x1740, 0x1772, 0x1780, 0x17b2, 0x17c0, 0x17f2, 0x1800, 0x1832, 0x1840, 0x1872, 0x1900, 0x1905,
    0x1910, 0x1915, 0x1920, 0x1925, 0x1930, 0x1935, 0x1940, 0x1945, 0x1950, 0x1955, 0x1960, 0x1965,
    0x1970, 0x1975, 0x1980, 0x1985, 0x1990, 0x1995, 0x19b0, 0x19d0, 0x1a00, 0x2400, 0x2900, 0x2b80,
    0x3000, 0x7b00, -1 };
static const int *const r850[] = { r850_function, r850_memory, r850_comm, r850_gps, r850_config, r850_factory };
static const char *const r850_names[] = { "Function", "Memory", "COMM", "GPS", "Configuration", "Factory" };
static const char *const r850_short[] = { "Function", "Memory", "COMM", "GPS", "Config", "Factory" };

/* ---------------------------------------------------------------- FTA-750L family */

static const setting s750[] = {
    /* configuration */
    { "Dimmer",           0x10, K_ENUM, 0, 6, n_dimmer7, v_0to5, 0, 0 },
    { "Lamp",             0x11, K_ENUM, 0, 9, n_lamp7, v_lamp7, 0, 0 },
    { "Contrast",         0x12, K_ENUM, 0, 30, NULL, v_1to30, 0, 0 },
    { "Key beep",         0x13, K_ENUM, 0, 6, n_beep7, v_0to5, 0, 0 },
    { "Timer alarm",      0x15, K_ENUM, 0, 2, n_onoff, v01, 0, 0 },
    { "Battery save",     0x16, K_ENUM, 0, 5, n_batsave, v_0to4, 0, 0 },
    { "Lock select",      0x18, K_ENUM, 0, 3, n_lock, v_1to3, 0, 0 },
    /* COMM */
    { "COMM band",        0x7E, K_BIT,  1, 2, n_used, v01, 0x80, 0 },
    { "NAV band",         0x7E, K_BIT,  1, 2, n_used, v01, 0x20, NEEDS_NAV },
    { "Expansion band",   0x7E, K_BIT,  1, 2, n_used, v01, 0x04, 0 },
    { "Weather band",     0x7E, K_BIT,  1, 2, n_used, v01, 0x02, 0 },
    { "8.33 kHz step",    0x2B, K_ENUM, 1, 2, n_onoff, v01, 0, 0 },
    { "Weather alert",    0x2A, K_ENUM, 1, 2, n_onoff, v01, 0, 0 },
    { "Emergency 121.5",  0x20, K_ENUM, 1, 2, n_onoff, v01, 0, 0 },
    { "Noise limiter",    0x33, K_ENUM, 1, 2, n_onoff, v01, 0, 0 },
    { "Dual watch freq",  0x0B, K_FREQ, 1, 0, NULL, NULL, 0, 0 },
    { "Split TX freq",    0x27, K_FREQ, 1, 0, NULL, NULL, 0, NEEDS_NAV },
    { "Scan",             0x22, K_ENUM, 1, 2, n_onoff, v01, 0, 0 },
    { "Scan resume",      0x24, K_ENUM, 1, 5, n_resume, v_resume, 0, 0 },
    { "PTT scan stop",    0x25, K_ENUM, 1, 2, n_onoff, v01, 0, 0 },
    { "Dual watch",       0x21, K_ENUM, 1, 2, n_onoff, v01, 0, 0 },
    { "Scan band",        0x34, K_ENUM, 1, 3, n_scanband, v_0to2, 0, NEEDS_NAV },
    { "TX time out",      0x14, K_ENUM, 1, 6, n_tot7, v_0to5, 0, 0 },
    { "MIC select",       0x1A, K_ENUM, 1, 2, n_mic, v01, 0, 0 },
    { "Side tone",        0x1B, K_SIDETONE, 1, 5, n_sidetone, v_0to4, 0, 0 },
    { "AF pitch",         0x17, K_ENUM, 1, 4, n_afpitch, v_1to4, 0, 0 },
    { "VOX",              0x30, K_ENUM, 1, 2, n_onoff, v01, 0, 0 },
    { "VOX level",        0x31, K_ENUM, 1, 5, n_voxlevel, v_0to4, 0, 0 },
    { "VOX delay",        0x32, K_ENUM, 1, 5, n_voxdelay, v_voxdelay7, 0, 0 },
    /* GPS */
    { "GPS unit power",   0x40, K_ENUM, 2, 2, n_onoff, v01, 0, NEEDS_GPS },
    { "GPS power save",   0x41, K_ENUM, 2, 5, n_gpssave, v_0to4, 0, NEEDS_GPS },
    { "Location format",  0x42, K_ENUM, 2, 3, n_locfmt7, v_0to2, 0, NEEDS_GPS },
    { "Time display",     0x43, K_TIMEDISP, 2, 2, n_timedisp, v01, 0, NEEDS_GPS },
    { "Time format",      0x43, K_TIMEFMT, 2, 2, n_timefmt, v01, 0, NEEDS_GPS },
    { "UTC offset",       0x43, K_TIMEOFF, 2, NTZ, NULL, NULL, 0, NEEDS_GPS },
    { "Distance unit",    0x45, K_ENUM, 2, 3, n_dist, v_0to2, 0, NEEDS_GPS },
    { "Speed unit",       0x44, K_ENUM, 2, 3, n_speed, v_0to2, 0, NEEDS_GPS },
    { "Altitude unit",    0x46, K_ENUM, 2, 2, n_alt, v01, 0, NEEDS_GPS },
    { "Pinning",          0x47, K_ENUM, 2, 2, n_onoff, v01, 0, NEEDS_GPS },
    { "SBAS (WAAS)",      0x48, K_ENUM, 2, 2, n_onoff, v01, 0, NEEDS_GPS },
    { "Logger interval",  0x51, K_ENUM, 2, 5, n_logint, v_1to5, 0, NEEDS_GPS },
};

/* what YCE01 writes back (one table serves a factory write as well) */
static const int wa750[] = {
    0x0002, 0x003D, 0x003E, 0x007F, 0x00A0, 0x00BB, 0x00C0, 0x00F0, 0x00FB, 0x0200,
    0x0270, 0x0300, 0x03C0, 0x0E40, 0x0F00, 0x1700, 0x1900, 0x3E80, -1
};

/* YCE01's reset masks: the bytes each reset takes from the factory image */
static const int r750_function[] = { 0x0005, 0x0006, 0x0026, 0x0027, 0x0050, 0x0051, 0x00A0, 0x00C0, -1 };
static const int r750_memory[] = { 0x0004, 0x0006, 0x00A0, 0x00B9, 0x0140, 0x01C0,
    0x0270, 0x027A, 0x0280, 0x028A, 0x0290, 0x029A, 0x02A0, 0x02AA, 0x02B0, 0x02BA, 0x02C0, 0x02CA,
    0x02D0, 0x02DA, 0x02E0, 0x02EA, 0x02F0, 0x02FA, 0x03C0, 0x0400, 0x0700, 0x1700, 0x1900, 0x3E80, -1 };
static const int r750_commgps[] = { 0x000B, 0x000E, 0x0014, 0x0015, 0x0017, 0x0018, 0x001A, 0x001D,
    0x0020, 0x0021, 0x0023, 0x002B, 0x002C, 0x002F, 0x0030, 0x0035, 0x003E, 0x0049, 0x0050, 0x0052, -1 };
static const int r750_config[] = { 0x0010, 0x0014, 0x0015, 0x0017, 0x0018, 0x001A, 0x001D, 0x0020, 0x002D, 0x002F, -1 };
static const int r750_factory[] = { 0x0002, 0x0021, 0x0023, 0x007E, 0x00A0, 0x00BB, 0x00C0, 0x00F0,
    0x00FB, 0x0200, 0x0270, 0x0300, 0x03C0, 0x0E40, 0x0F00, 0x1700, 0x1900, 0x3E80, -1 };
static const int *const r750[] = { r750_function, r750_memory, r750_commgps, r750_config, r750_factory };
static const char *const r750_names[] = { "Function", "Memory", "COMM and GPS", "Configuration", "Factory" };
static const char *const r750_short[] = { "Function", "Memory", "COMM/GPS", "Config", "Factory" };

/* ---------------------------------------------------------------- the models */

#define LAYOUT850 \
    0x0100, 0x7FFE, \
    0x3000, 0x1600, 64, 0x1500, 0x1540, 0x0170, \
    0x0300, 0x0500, 0x01AE, 0x0A00, 0x0A40, \
    0x1900, 0x1A00, 0x2900, \
    0x01DD, 0x01D0, 0x01F0, \
    0x012B, 0x012C, 0x0126, 0x0127, 0x0149, 0x0140, 0x0134, 0x0145, 0x011A, 0x011B, \
    0x10, 2, 0x12, 0x13, 0x16, 0x1A, 0x1F, 0x20, 0x2F, -1, \
    s850, (int)(sizeof s850 / sizeof s850[0]), wa850, wa850f, r850

#define LAYOUT750 \
    0x0000, 0x3FFE, \
    0x1900, 0x0D00, 32, 0x03C0, 0x03E0, 0x00A0, \
    0x0270, 0x0400, 0x00B9, 0x01C0, 0x0700, \
    -1, -1, -1, \
    0x007E, -1, -1, \
    0x002B, 0x002C, 0x0026, 0x0027, 0x0050, 0x0040, 0x0034, 0x0045, 0x001B, 0x001C, \
    0x10, 1, 0x11, 0x12, 0x15, 0x19, 0x1E, 0x20, 0x2F, 0x0D, \
    s750, (int)(sizeof s750 / sizeof s750[0]), wa750, wa750, r750

static const cfgmodel models[] = {
    /* code, name, longname, cpkey, size, mb_max, tag_max, nav, gps, routes, pos, course, id, group_edit_from, recent_n, resets */
    { MODEL_FTA850, "FTA-850", "FTA-850L", "SQL",  0x8000, 400, 14, 1, 1, 1, 1, 0, 1, 1, 9, 6, r850_names, r850_short, LAYOUT850 },
    { MODEL_FTA750, "FTA-750", "FTA-750L", "MENU", 0x4000, 200, 15, 1, 1, 0, 1, 1, 0, 2, 8, 5, r750_names, r750_short, LAYOUT750 },
    { MODEL_FTA550, "FTA-550", "FTA-550L", "MENU", 0x4000, 200, 15, 1, 0, 0, 1, 1, 0, 2, 8, 5, r750_names, r750_short, LAYOUT750 },
    { MODEL_FTA450, "FTA-450", "FTA-450L", "MENU", 0x4000, 200, 15, 0, 0, 0, 0, 1, 0, 2, 8, 5, r750_names, r750_short, LAYOUT750 },
};
#define NMODELS (int)(sizeof models / sizeof models[0])

const int cfg_wx_freq[WX_MAX] = { 162550, 162400, 162475, 162425, 162450,
                                  162500, 162525, 161650, 161775, 163275 };

/* ---------------------------------------------------------------- helpers */

static int get16(const unsigned char *img, int off)
{
    return img[off] * 256 + img[off + 1];
}

static void set16(unsigned char *img, int off, int v)
{
    img[off] = (unsigned char)(v >> 8);
    img[off + 1] = (unsigned char)v;
}

const cfgmodel *cfg_model_by_code(int code)
{
    int i;

    for (i = 0; i < NMODELS; i++) if (models[i].code == code) return &models[i];
    return NULL;
}

const cfgmodel *cfg_model_list(int i)
{
    return i >= 0 && i < NMODELS ? &models[i] : NULL;
}

const cfgmodel *cfg_model_of(const unsigned char *img)
{
    int i;

    for (i = 0; i < NMODELS; i++) if (get16(img, models[i].o_model) == models[i].code) return &models[i];
    return NULL;
}

const cfgmodel *cfg_model_or_default(const unsigned char *img)
{
    const cfgmodel *m = cfg_model_of(img);

    return m ? m : &models[0];
}

#define M(img) cfg_model_or_default(img)

/* BCD digits of n bytes as a string; returns 0 if any nibble is not a digit
   (an empty field of all F gives an empty string and 1) */
static int get_bcd(const unsigned char *img, int off, int n, char *out)
{
    int i, allf = 1, ok = 1;

    for (i = 0; i < n; i++) {
        int hi = img[off + i] >> 4, lo = img[off + i] & 15;

        out[2 * i] = (char)('0' + hi);
        out[2 * i + 1] = (char)('0' + lo);
        if (hi > 9 || lo > 9) ok = 0;
        if (img[off + i] != 0xff) allf = 0;
    }
    out[2 * n] = 0;
    if (allf) { out[0] = 0; return 1; }
    return ok;
}

static void set_bcd(unsigned char *img, int off, int n, const char *digits)
{
    int i;

    if (!digits || !*digits) { memset(img + off, 0xff, (size_t)n); return; }
    for (i = 0; i < n; i++)
        img[off + i] = (unsigned char)((digits[2 * i] - '0') * 16 + (digits[2 * i + 1] - '0'));
}

static void get_str(const unsigned char *img, int off, int n, char *out)
{
    int i;

    for (i = 0; i < n && img[off + i] != 0xff && img[off + i] != 0; i++) out[i] = (char)img[off + i];
    out[i] = 0;
    while (i > 0 && out[i - 1] == ' ') out[--i] = 0;
}

static void set_str(unsigned char *img, int off, int n, const char *s)
{
    int i;

    memset(img + off, 0xff, (size_t)n);
    for (i = 0; i < n && s[i]; i++) img[off + i] = (unsigned char)s[i];
}

static int get_bit(const unsigned char *img, int off, int i)
{
    return (img[off + i / 8] >> (7 - i % 8)) & 1;
}

static void set_bit(unsigned char *img, int off, int i, int v)
{
    if (v) img[off + i / 8] |= (unsigned char)(0x80 >> (i % 8));
    else img[off + i / 8] &= (unsigned char)~(0x80 >> (i % 8));
}

/* ---------------------------------------------------------------- image */

int cfg_model(const unsigned char *img)
{
    const cfgmodel *m = cfg_model_of(img);

    return m ? m->code : 0;
}

int cfg_check(const unsigned char *img, char *err, size_t errlen)
{
    const cfgmodel *m = cfg_model_of(img);
    int f;

    if (!m) {
        snprintf(err, errlen, "Not a radio image: model code %04X at 0000, %04X at 0100 (FTA-850 0352, FTA-750 02EE, FTA-550 0226, FTA-450 01C2)",
                 get16(img, 0), get16(img, 0x100));
        return 0;
    }
    f = get16(img, m->o_footer);
    if (f != m->code) {
        snprintf(err, errlen, "The %s image's footer says %04X, not %04X", m->name, f, m->code);
        return 0;
    }
    err[0] = 0;
    return 1;
}

void cfg_flash_id(const unsigned char *img, char *out, size_t outlen)
{
    const cfgmodel *m = M(img);
    char s[16];

    if (m->o_flashid < 0) { if (outlen) out[0] = 0; return; }
    get_str(img, m->o_flashid, 10, s);
    snprintf(out, outlen, "%s", s);
}

void cfg_serial(const unsigned char *img, char *out, size_t outlen)
{
    const cfgmodel *m = M(img);
    char s[20];

    if (m->o_serial < 0) { if (outlen) out[0] = 0; return; }
    get_str(img, m->o_serial, 16, s);
    snprintf(out, outlen, "%s", s);
}

/* ---------------------------------------------------------------- settings */

const setting *cfg_settings_of(const unsigned char *img, int *n)
{
    const cfgmodel *m = M(img);

    if (n) *n = m->nsettings;
    return m->settings;
}

int cfg_setting_shown(const unsigned char *img, int sid)
{
    const cfgmodel *m = M(img);
    int needs;

    if (sid < 0 || sid >= m->nsettings) return 0;
    needs = m->settings[sid].needs;
    if ((needs & NEEDS_NAV) && !m->has_nav) return 0;
    if ((needs & NEEDS_GPS) && !m->has_gps) return 0;
    return 1;
}

int cfg_sid833(const unsigned char *img)
{
    const cfgmodel *m = M(img);
    int i;

    for (i = 0; i < m->nsettings; i++) if (m->settings[i].off == m->o_833 && m->settings[i].kind == K_ENUM) return i;
    return 0;
}

static int tz_code(int v)
{
    if (v < 24) return 24 - v;
    if (v == 24) return 0;
    return 33 + (v - 25);
}

static int tz_index(int code)
{
    if (code == 0) return 24;
    if (code >= 1 && code <= 24) return 24 - code;
    if (code >= 33 && code <= 60) return 25 + (code - 33);
    return -1;
}

int cfg_get(const unsigned char *img, int sid)
{
    const cfgmodel *m = M(img);
    const setting *s;
    int i, b;
    char d[16];

    if (sid < 0 || sid >= m->nsettings) return -1;
    s = &m->settings[sid];
    b = img[s->off];
    switch (s->kind) {
        case K_ENUM:
        case K_ARRIVAL:
            if (s->off == m->o_scanband && b == 0xff) b = 0;
            for (i = 0; i < s->n; i++) if (s->vals[i] == b) return i;
            /* the 750 family stores 4 for a 3 s VOX delay in some firmware; YCE01 reads it as 5 */
            if (m->code != MODEL_FTA850 && s->vals == v_voxdelay7 && b == 4) return 4;
            return -1;
        case K_BIT:
            return (b & s->mask) ? 1 : 0;
        case K_FREQ:
            if (!get_bcd(img, s->off, 3, d) || !d[0]) return 0;
            return atoi(d);
        case K_SIDETONE:
            if (img[m->o_stsel] == 0) return 0;
            if (img[m->o_stsel] == 1 && img[m->o_stlevel] >= 1 && img[m->o_stlevel] <= 4) return img[m->o_stlevel];
            return -1;
        case K_TIMEDISP: return (b & 0x80) ? 1 : 0;
        case K_TIMEFMT:  return (b & 0x40) ? 1 : 0;
        case K_TIMEOFF:  return tz_index(b & 0x3f);
    }
    return -1;
}

void cfg_set(unsigned char *img, int sid, int v)
{
    const cfgmodel *m = M(img);
    const setting *s;
    char d[16];

    if (sid < 0 || sid >= m->nsettings) return;
    s = &m->settings[sid];
    switch (s->kind) {
        case K_ENUM:
        case K_ARRIVAL:
            if (v < 0 || v >= s->n) return;
            img[s->off] = s->vals[v];
            if (s->off == m->o_833 && v == 0) img[m->o_fstep] = 0;
            if (s->off == m->o_gpspower && v == 0) img[m->o_logger] = 0;
            break;
        case K_BIT:
            if (v) img[s->off] |= (unsigned char)s->mask;
            else img[s->off] &= (unsigned char)~s->mask;
            break;
        case K_FREQ:
            if (v <= 0) set_bcd(img, s->off, 3, NULL);
            else { snprintf(d, sizeof d, "%06d", v); set_bcd(img, s->off, 3, d); }
            if (s->off == m->o_splitfreq && v <= 0) img[m->o_splitmode] = 0;
            break;
        case K_SIDETONE:
            if (v == 0) { img[m->o_stsel] = 0; img[m->o_stlevel] = 1; }
            else { img[m->o_stsel] = 1; img[m->o_stlevel] = (unsigned char)v; }
            break;
        case K_TIMEDISP:
            img[s->off] = (unsigned char)((img[s->off] & 0x7f) | (v ? 0x80 : 0));
            break;
        case K_TIMEFMT:
            img[s->off] = (unsigned char)((img[s->off] & 0xbf) | (v ? 0x40 : 0));
            break;
        case K_TIMEOFF:
            if (v < 0 || v >= NTZ) return;
            img[s->off] = (unsigned char)((img[s->off] & 0xc0) | tz_code(v));
            break;
    }
}

const char *cfg_choice(const unsigned char *img, int sid, int v, char *buf, size_t buflen)
{
    const cfgmodel *m = M(img);
    const setting *s;
    int h;

    if (sid < 0 || sid >= m->nsettings) { snprintf(buf, buflen, "?"); return buf; }
    s = &m->settings[sid];
    if (s->kind == K_FREQ) {
        if (v <= 0) snprintf(buf, buflen, "---.---");
        else cfg_fmt_freq(v, buf, buflen);
        return buf;
    }
    if (v < 0 || v >= s->n) { snprintf(buf, buflen, "?"); return buf; }
    if (s->kind == K_TIMEOFF) {
        h = v - 24;   /* half hours from UTC */
        snprintf(buf, buflen, "%s%d:%02d", h < 0 ? "-" : h > 0 ? "+" : "", abs(h) / 2, (abs(h) % 2) * 30);
        return buf;
    }
    if (s->kind == K_ARRIVAL) {
        /* named in the distance unit in use */
        int u = img[m->o_distu];

        return (u == 1 ? n_arr_sm : u == 2 ? n_arr_km : n_arr_nm)[v];
    }
    if (s->names) return s->names[v];
    snprintf(buf, buflen, "%d", s->vals[v]);
    return buf;
}

/* ---------------------------------------------------------------- memory book */

int cfg_mb_count(const unsigned char *img)
{
    const cfgmodel *m = M(img);
    int i;

    for (i = 0; i < m->mb_max; i++) if (!get_bit(img, m->o_mbused, i)) break;
    return i;
}

static int rec_addr(const cfgmodel *m, const unsigned char *img, int p)
{
    int a = m->addr_len == 2 ? get16(img, p + m->r_addr) : img[p + m->r_addr];

    return a > m->mb_max ? 0 : a;
}

static void rec_get(const cfgmodel *m, const unsigned char *img, int p, mbentry *e)
{
    char d[16];

    memset(e, 0, sizeof *e);
    e->course = -1;
    e->addr = rec_addr(m, img, p);
    if (get_bcd(img, p + m->r_freq, 3, d) && d[0]) e->freq = atoi(d);
    e->group = img[p + m->r_group];
    if (e->group > NGROUPS) e->group = 0;
    get_str(img, p + m->r_tag, m->tag_max, e->tag);
    if (get_bcd(img, p + m->r_lat, 4, d) && d[0]) {
        e->lat_deg = (d[0] - '0') * 10 + (d[1] - '0');
        e->lat_mmm = atoi(d + 2) / 10;
        e->haspos = 1;
    }
    if (get_bcd(img, p + m->r_lon, 5, d) && d[0]) {
        e->lon_deg = (d[1] - '0') * 100 + (d[2] - '0') * 10 + (d[3] - '0');
        e->lon_mmm = atoi(d + 4) / 10;
    }
    switch (img[p + m->r_hemi]) {
        case 1: e->ns = 'N'; e->ew = 'E'; break;
        case 2: e->ns = 'N'; e->ew = 'W'; break;
        case 3: e->ns = 'S'; e->ew = 'E'; break;
        case 4: e->ns = 'S'; e->ew = 'W'; break;
        default: e->ns = 'N'; e->ew = 'W'; e->haspos = 0; break;
    }
    if (!m->has_pos) e->haspos = 0;
    e->shift = img[p + m->r_shift] == 1;
    if (m->r_course >= 0 && get_bcd(img, p + m->r_course, 3, d) && d[0]) {
        d[5] = 0;   /* "dddDD0": degrees and hundredths */
        e->course = atoi(d);
    }
}

static void rec_set(const cfgmodel *m, unsigned char *img, int p, const mbentry *e)
{
    char d[32];

    if (!e->used) {
        memset(img + p, 0xff, MB_RECLEN);
        img[p + m->r_group] = 0;
        return;
    }
    if (m->addr_len == 2) set16(img, p + m->r_addr, e->addr);
    else img[p + m->r_addr] = (unsigned char)e->addr;
    img[p + m->r_group] = (unsigned char)e->group;
    if (e->freq > 0) { snprintf(d, sizeof d, "%06d", e->freq); set_bcd(img, p + m->r_freq, 3, d); }
    else set_bcd(img, p + m->r_freq, 3, NULL);
    set_str(img, p + m->r_tag, m->tag_max, e->tag);
    if (e->haspos && m->has_pos) {
        snprintf(d, sizeof d, "%02d%02d%03d0", e->lat_deg, e->lat_mmm / 1000, e->lat_mmm % 1000);
        set_bcd(img, p + m->r_lat, 4, d);
        snprintf(d, sizeof d, "0%03d%02d%03d0", e->lon_deg, e->lon_mmm / 1000, e->lon_mmm % 1000);
        set_bcd(img, p + m->r_lon, 5, d);
        img[p + m->r_hemi] = (unsigned char)(e->ns == 'N' ? (e->ew == 'E' ? 1 : 2) : (e->ew == 'E' ? 3 : 4));
    } else {
        memset(img + p + m->r_lat, 0xff, 9);
        img[p + m->r_hemi] = 0xff;
    }
    img[p + m->r_shift] = (unsigned char)(e->shift ? 1 : 0);
    if (m->r_course >= 0) {
        if (e->course >= 0) { snprintf(d, sizeof d, "%05d0", e->course % 100000); set_bcd(img, p + m->r_course, 3, d); }
        else set_bcd(img, p + m->r_course, 3, NULL);
    }
}

void cfg_mb_get(const unsigned char *img, int i, mbentry *e)
{
    const cfgmodel *m = M(img);

    rec_get(m, img, m->o_mbook + MB_RECLEN * i, e);
    e->used = get_bit(img, m->o_mbused, i);
    e->scan = get_bit(img, m->o_mbscan, i);
    if (!get_bit(img, m->o_mbfreq, i)) e->freq = 0;
    if (!get_bit(img, m->o_mbpos, i)) e->haspos = 0;
}

void cfg_mb_set(unsigned char *img, int i, const mbentry *e)
{
    const cfgmodel *m = M(img);
    int g;

    rec_set(m, img, m->o_mbook + MB_RECLEN * i, e);
    set_bit(img, m->o_mbused, i, e->used);
    set_bit(img, m->o_mbfreq, i, e->used && e->freq > 0);
    set_bit(img, m->o_mbpos, i, e->used && e->haspos && m->has_pos);
    set_bit(img, m->o_mbscan, i, e->used && e->scan);
    for (g = 1; g <= NGROUPS; g++) set_bit(img, m->o_mbused + m->mb_bitstride * g, i, e->used && e->group == g);
}

void cfg_tag_fit(const unsigned char *img, char *tag)
{
    const cfgmodel *m = M(img);

    if ((int)strlen(tag) > m->tag_max) tag[m->tag_max] = 0;
}

int cfg_mb_find(const unsigned char *img, int addr)
{
    const cfgmodel *m = M(img);
    int i, n = cfg_mb_count(img);

    if (addr <= 0) return -1;
    for (i = 0; i < n; i++) if (rec_addr(m, img, m->o_mbook + MB_RECLEN * i) == addr) return i;
    return -1;
}

/* the next memory number after addr whose entry has a position; dir -1 goes
   back; 0 for addr starts at the ends; 0 if there is none */
int cfg_mb_next_pos(const unsigned char *img, int addr, int dir)
{
    int i, n = cfg_mb_count(img), best = 0, bestaddr = 0;
    mbentry e;

    for (i = 0; i < n; i++) {
        cfg_mb_get(img, i, &e);
        if (!e.haspos) continue;
        if (dir >= 0) {
            if (e.addr > addr && (!best || e.addr < bestaddr)) { best = 1; bestaddr = e.addr; }
        } else {
            if ((addr == 0 || e.addr < addr) && (!best || e.addr > bestaddr)) { best = 1; bestaddr = e.addr; }
        }
    }
    return bestaddr;
}

static int cmpchar(int x, int y)
{
    static const char marks[] = " *+,/&-[].";
    const char *px, *py;

    if (x == y) return 0;
    if (x == ' ') return -1;
    if (y == ' ') return 1;
    if (isdigit(x)) return isdigit(y) ? x - y : -1;
    if (isdigit(y)) return 1;
    if (isalpha(x)) return isalpha(y) ? x - y : -1;
    if (isalpha(y)) return 1;
    px = strchr(marks, x); py = strchr(marks, y);
    if (px) return (py && px < py) ? -1 : 1;
    if (py) return 1;
    return x - y;
}

static int cmpnames(const char *s, const char *t, int ix, int iy)
{
    int i, c;

    for (i = 0; s[i] && t[i]; i++) {
        c = cmpchar(toupper((unsigned char)s[i]), toupper((unsigned char)t[i]));
        if (c) return c;
    }
    if (s[i] || t[i]) return s[i] ? 1 : -1;
    return ix - iy;
}

typedef struct { mbentry e; int oldidx; } sortent;

static int cmpent(const void *a, const void *b)
{
    const sortent *x = a, *y = b;

    return cmpnames(x->e.tag, y->e.tag, x->oldidx, y->oldidx);
}

int cfg_mb_normalize(unsigned char *img, const mbentry *keep)
{
    const cfgmodel *m = M(img);
    sortent *v = malloc(MB_MAX * sizeof *v);
    int i, n = 0, found = -1, k;
    mbentry blank, r;

    if (!v) return -1;
    for (i = 0; i < m->mb_max; i++) {
        if (!get_bit(img, m->o_mbused, i)) continue;
        cfg_mb_get(img, i, &v[n].e);
        v[n].oldidx = i;
        n++;
    }
    qsort(v, (size_t)n, sizeof *v, cmpent);
    memset(&blank, 0, sizeof blank);
    for (i = 0; i < m->mb_max; i++) {
        if (i < n) {
            cfg_mb_set(img, i, &v[i].e);
            if (keep && found < 0 && v[i].e.addr == keep->addr) found = i;
        } else cfg_mb_set(img, i, &blank);
    }
    /* recent memories that refer to a number no longer in the book are blanked */
    for (k = -1; k < m->recent_n; k++) {
        int p = k < 0 ? m->o_lastmem : m->o_recent + 64 * k;

        rec_get(m, img, p, &r);
        if (r.addr && cfg_mb_find(img, r.addr) < 0) { memset(&r, 0, sizeof r); r.course = -1; rec_set(m, img, p, &r); }
    }
    free(v);
    return found;
}

int cfg_mb_add(unsigned char *img, mbentry *e)
{
    const cfgmodel *m = M(img);
    int i, a;
    unsigned char *taken;

    for (i = 0; i < m->mb_max; i++) if (!get_bit(img, m->o_mbused, i)) break;
    if (i >= m->mb_max) return -1;
    /* the lowest memory number not in use */
    taken = calloc(MB_MAX + 1, 1);
    if (!taken) return -1;
    for (a = 0; a < i; a++) {
        int num = rec_addr(m, img, m->o_mbook + MB_RECLEN * a);

        if (num >= 1 && num <= m->mb_max) taken[num] = 1;
    }
    for (a = 1; a <= m->mb_max && taken[a]; a++) ;
    free(taken);
    if (a > m->mb_max) return -1;
    e->used = 1;
    e->addr = a;
    cfg_tag_fit(img, e->tag);
    if (!m->has_pos) e->haspos = 0;
    cfg_mb_set(img, i, e);
    return cfg_mb_normalize(img, e);
}

int cfg_mb_delete(unsigned char *img, int i, char *err, size_t errlen)
{
    mbentry e, blank;
    int r;
    route rt;

    cfg_mb_get(img, i, &e);
    r = cfg_rt_uses(img, e.addr);
    if (r >= 0) {
        cfg_rt_get(img, r, &rt);
        snprintf(err, errlen, "Memory %d is used by route %s; change the route first", e.addr, rt.name);
        return 0;
    }
    memset(&blank, 0, sizeof blank);
    cfg_mb_set(img, i, &blank);
    cfg_mb_normalize(img, NULL);
    err[0] = 0;
    return 1;
}

void cfg_mb_clear(unsigned char *img)
{
    const cfgmodel *m = M(img);
    mbentry blank;
    int i;

    cfg_rt_clear(img);
    memset(&blank, 0, sizeof blank);
    for (i = 0; i < m->mb_max; i++) cfg_mb_set(img, i, &blank);
    cfg_mb_normalize(img, NULL);
}

int cfg_mb_valid(const unsigned char *img, const mbentry *e, int allow833, char *err, size_t errlen)
{
    const cfgmodel *m = M(img);

    if (!e->tag[0]) { snprintf(err, errlen, "The name is empty"); return 0; }
    if (!cfg_tag_ok(e->tag)) { snprintf(err, errlen, "The name has a character the radio cannot show"); return 0; }
    if ((int)strlen(e->tag) > m->tag_max) { snprintf(err, errlen, "The name is longer than %d characters", m->tag_max); return 0; }
    if (e->freq > 0 && !cfg_freq_ok(img, e->freq, allow833)) {
        snprintf(err, errlen, "%d.%03d MHz is not a %s channel", e->freq / 1000, e->freq % 1000, m->has_nav ? "COMM or NAVI" : "COMM");
        return 0;
    }
    if (e->freq <= 0 && !(e->haspos && m->has_pos)) {
        snprintf(err, errlen, m->has_pos ? "An entry needs a frequency or a position" : "An entry needs a frequency");
        return 0;
    }
    if (e->haspos && (e->lat_deg > 89 || e->lat_mmm >= 60000 || e->lon_deg > 179 || e->lon_mmm >= 60000)) {
        snprintf(err, errlen, "The position is out of range");
        return 0;
    }
    if (e->course >= 36000) { snprintf(err, errlen, "A runway course is 0 to 359.99 degrees"); return 0; }
    err[0] = 0;
    return 1;
}

/* ---------------------------------------------------------------- routes */

int cfg_rt_count(const unsigned char *img)
{
    const cfgmodel *m = M(img);
    int i;

    if (!m->has_routes) return 0;
    for (i = 0; i < ROUTE_MAX; i++) if (!get_bit(img, m->o_rtused, i)) break;
    return i;
}

void cfg_rt_get(const unsigned char *img, int i, route *r)
{
    const cfgmodel *m = M(img);
    int p, k, v, tmp[VIA_MAX], n = 0, g;

    memset(r, 0, sizeof *r);
    if (!m->has_routes) return;
    p = m->o_routes + RT_RECLEN * i;
    r->used = get_bit(img, m->o_rtused, i);
    get_str(img, m->o_rtnames + 16 * i, m->tag_max, r->name);
    for (g = 1; g <= NGROUPS; g++) if (get_bit(img, m->o_rtused + 16 * g, i)) { r->group = g; break; }
    v = get16(img, p);
    r->from = v == 0xffff ? 0 : v;
    for (k = 0; k < VIA_MAX; k++) {
        v = get16(img, p + 2 + 2 * k);
        if (v != 0xffff && v >= 1 && v <= m->mb_max) tmp[n++] = v;
    }
    /* stored last first */
    for (k = 0; k < n; k++) r->via[k] = tmp[n - 1 - k];
    r->nvia = n;
    v = get16(img, p + 62);
    r->to = v == 0xffff ? 0 : v;
    if (!r->from) r->used = 0;
}

void cfg_rt_set(unsigned char *img, int i, const route *r)
{
    const cfgmodel *m = M(img);
    int p, k, g;

    if (!m->has_routes) return;
    p = m->o_routes + RT_RECLEN * i;
    if (!r->used) {
        memset(img + p, 0xff, RT_RECLEN);
        memset(img + m->o_rtnames + 16 * i, 0xff, 16);
        set_bit(img, m->o_rtused, i, 0);
        for (g = 1; g <= NGROUPS; g++) set_bit(img, m->o_rtused + 16 * g, i, 0);
        return;
    }
    set16(img, p, r->from ? r->from : 0xffff);
    for (k = 0; k < VIA_MAX; k++) set16(img, p + 2 + 2 * k, k < r->nvia ? r->via[r->nvia - 1 - k] : 0xffff);
    set16(img, p + 62, r->to ? r->to : 0xffff);
    set_str(img, m->o_rtnames + 16 * i, m->tag_max, r->name);
    set_bit(img, m->o_rtused, i, 1);
    for (g = 1; g <= NGROUPS; g++) set_bit(img, m->o_rtused + 16 * g, i, r->group == g);
}

typedef struct { route r; int oldidx; } sortrt;

static int cmprt(const void *a, const void *b)
{
    const sortrt *x = a, *y = b;

    return cmpnames(x->r.name, y->r.name, x->oldidx, y->oldidx);
}

int cfg_rt_normalize(unsigned char *img, const route *keep)
{
    const cfgmodel *m = M(img);
    sortrt v[ROUTE_MAX];
    int i, n = 0, found = -1;
    route blank;

    if (!m->has_routes) return -1;
    for (i = 0; i < ROUTE_MAX; i++) {
        if (!get_bit(img, m->o_rtused, i)) continue;
        cfg_rt_get(img, i, &v[n].r);
        if (!v[n].r.used) continue;
        v[n].oldidx = i;
        n++;
    }
    qsort(v, (size_t)n, sizeof *v, cmprt);
    memset(&blank, 0, sizeof blank);
    for (i = 0; i < ROUTE_MAX; i++) {
        if (i < n) {
            cfg_rt_set(img, i, &v[i].r);
            if (keep && found < 0 && !strcmp(v[i].r.name, keep->name) && v[i].r.from == keep->from && v[i].r.to == keep->to) found = i;
        } else cfg_rt_set(img, i, &blank);
    }
    return found;
}

int cfg_rt_add(unsigned char *img, const route *r)
{
    const cfgmodel *m = M(img);
    int i;
    route n = *r;

    if (!m->has_routes) return -1;
    for (i = 0; i < ROUTE_MAX; i++) if (!get_bit(img, m->o_rtused, i)) break;
    if (i >= ROUTE_MAX) return -1;
    n.used = 1;
    cfg_rt_set(img, i, &n);
    return cfg_rt_normalize(img, &n);
}

void cfg_rt_delete(unsigned char *img, int i)
{
    route blank;

    memset(&blank, 0, sizeof blank);
    cfg_rt_set(img, i, &blank);
    cfg_rt_normalize(img, NULL);
}

void cfg_rt_clear(unsigned char *img)
{
    route blank;
    int i;

    if (!M(img)->has_routes) return;
    memset(&blank, 0, sizeof blank);
    for (i = 0; i < ROUTE_MAX; i++) cfg_rt_set(img, i, &blank);
}

static int mem_has_pos(const unsigned char *img, int addr)
{
    int i = cfg_mb_find(img, addr);
    mbentry e;

    if (i < 0) return 0;
    cfg_mb_get(img, i, &e);
    return e.haspos;
}

int cfg_rt_valid(const unsigned char *img, const route *r, char *err, size_t errlen)
{
    int k;

    if (!r->name[0]) { snprintf(err, errlen, "The route name is empty"); return 0; }
    if (!cfg_tag_ok(r->name)) { snprintf(err, errlen, "The name has a character the radio cannot show"); return 0; }
    if (r->group < 1 || r->group > NGROUPS) { snprintf(err, errlen, "A route needs a group"); return 0; }
    if (!r->from || !mem_has_pos(img, r->from)) { snprintf(err, errlen, "The start must be a memory with a position"); return 0; }
    if (!r->to || !mem_has_pos(img, r->to)) { snprintf(err, errlen, "The destination must be a memory with a position"); return 0; }
    for (k = 0; k < r->nvia; k++)
        if (!mem_has_pos(img, r->via[k])) { snprintf(err, errlen, "Via point %d is not a memory with a position", k + 1); return 0; }
    err[0] = 0;
    return 1;
}

int cfg_rt_uses(const unsigned char *img, int addr)
{
    int i, k, n = cfg_rt_count(img);
    route r;

    if (addr <= 0) return -1;
    for (i = 0; i < n; i++) {
        cfg_rt_get(img, i, &r);
        if (r.from == addr || r.to == addr) return i;
        for (k = 0; k < r.nvia; k++) if (r.via[k] == addr) return i;
    }
    return -1;
}

/* ---------------------------------------------------------------- weather, groups */

void cfg_wx_get(const unsigned char *img, int i, char *tag, int *scan)
{
    const cfgmodel *m = M(img);

    get_str(img, m->o_wxnames + 16 * i, m->tag_max, tag);
    *scan = get_bit(img, m->o_wxscan, i);
}

void cfg_wx_set(unsigned char *img, int i, const char *tag, int scan)
{
    const cfgmodel *m = M(img);

    set_str(img, m->o_wxnames + 16 * i, m->tag_max, tag);
    set_bit(img, m->o_wxscan, i, scan);
}

void cfg_group_name(const unsigned char *img, int g, char *out, size_t outlen)
{
    const cfgmodel *m = M(img);
    char s[16];

    if (g <= 0 || g > NGROUPS) { snprintf(out, outlen, "ALL"); return; }
    get_str(img, m->o_groups + 16 * (g - 1), GRP_MAX, s);
    snprintf(out, outlen, "%s", s[0] ? s : "(blank)");
}

void cfg_set_group_name(unsigned char *img, int g, const char *name)
{
    const cfgmodel *m = M(img);

    if (g < m->group_edit_from || g > NGROUPS) return;
    set_str(img, m->o_groups + 16 * (g - 1), GRP_MAX, name);
}

/* ---------------------------------------------------------------- checks and formats */

int cfg_tag_ok(const char *s)
{
    for (; *s; s++)
        if (!isalnum((unsigned char)*s) && !strchr(" *+,/&-[].", *s)) return 0;
    return 1;
}

int cfg_freq_ok(const unsigned char *img, int khz, int allow833)
{
    static const int m25[] = { 0, 25, 50, 75 };
    static const int m833[] = { 5, 10, 15, 30, 35, 40, 55, 60, 65, 80, 85, 90 };
    const cfgmodel *m = M(img);
    int r = khz % 100, i, ok = 0;

    if (khz < (m->has_nav ? 108000 : 118000) || khz >= 137000) return 0;
    for (i = 0; i < 4; i++) if (m25[i] == r) ok = 1;
    if (allow833) for (i = 0; i < 12; i++) if (m833[i] == r) ok = 1;
    return ok;
}

int cfg_parse_freq(const char *s)
{
    char ip[8], fp[8];
    int ni = 0, nf = 0, dot = 0;

    for (; *s; s++) {
        if (*s == '.' || *s == ',') { if (dot) return -1; dot = 1; continue; }
        if (*s == ' ') continue;
        if (!isdigit((unsigned char)*s)) return -1;
        if (!dot) { if (ni < 7) ip[ni++] = *s; }
        else if (nf < 3) fp[nf++] = *s;
    }
    if (!dot && ni == 6) { memcpy(fp, ip + 3, 3); nf = 3; ni = 3; }
    if (ni != 3) return -1;
    while (nf < 3) fp[nf++] = '0';
    ip[3] = 0; fp[3] = 0;
    return atoi(ip) * 1000 + atoi(fp);
}

void cfg_fmt_freq(int khz, char *out, size_t outlen)
{
    if (khz <= 0) { if (outlen) out[0] = 0; }
    else snprintf(out, outlen, "%d.%03d", khz / 1000, khz % 1000);
}

/* "37 19.583", "3719.583", "37.3264" (decimal degrees) or "37 19 35" */
int cfg_parse_pos(const char *s, int lat, int *deg, int *mmm)
{
    int dd = lat ? 2 : 3, nd = 0, i, sep = 0;
    char d[16], frac[8];
    int nfr = 0, dot = 0, ngroups = 1, g2 = 0, g3 = 0;
    const char *p;

    for (p = s, i = 0; p[i] && (isdigit((unsigned char)p[i]) || p[i] == '.'); i++)
        if (p[i] == '.') dot = i;
    if (!p[i] && dot == dd && i > dd) {
        double v = atof(p);

        if (v < 0 || v >= (lat ? 90 : 180)) return 0;
        *deg = (int)v;
        *mmm = (int)((v - *deg) * 60000 + 0.5);
        if (*mmm >= 60000) { *mmm = 0; (*deg)++; }
        return 1;
    }
    dot = 0;
    for (p = s; *p; p++) {
        if (isdigit((unsigned char)*p)) {
            if (dot) { if (nfr < 3) frac[nfr++] = *p; }
            else if (nd < 15) d[nd++] = *p;
            sep = 0;
        } else if (*p == '.') { if (dot) return 0; dot = 1; }
        else if (strchr(" :'\"-", *p) || (unsigned char)*p >= 0x80) {
            if (!sep && !dot && nd > 0) { ngroups++; sep = 1; if (ngroups == 2) g2 = nd; else if (ngroups == 3) g3 = nd; }
            else if (!sep && dot) { sep = 1; }
        } else return 0;
    }
    d[nd] = 0;
    if (nd < dd + 1) return 0;
    if (ngroups >= 3 && g3 > g2) {
        int ss;

        *deg = 0; for (i = 0; i < dd && i < nd; i++) *deg = *deg * 10 + (d[i] - '0');
        *mmm = 0; for (i = dd; i < g3; i++) *mmm = *mmm * 10 + (d[i] - '0');
        ss = 0; for (i = g3; i < nd; i++) ss = ss * 10 + (d[i] - '0');
        if (*mmm >= 60 || ss >= 60) return 0;
        *mmm = *mmm * 1000 + (ss * 1000 + 30) / 60;
    } else {
        int mm;

        *deg = 0; for (i = 0; i < dd; i++) *deg = *deg * 10 + (d[i] - '0');
        mm = 0; for (i = dd; i < nd && i < dd + 2; i++) mm = mm * 10 + (d[i] - '0');
        if (nd > dd + 2) for (i = dd + 2; i < nd && nfr < 3; i++) frac[nfr++] = d[i];
        while (nfr < 3) frac[nfr++] = '0';
        frac[3] = 0;
        if (mm >= 60) return 0;
        *mmm = mm * 1000 + atoi(frac);
    }
    if (*deg >= (lat ? 90 : 180)) return 0;
    return 1;
}

void cfg_fmt_pos(int lat, int deg, int mmm, char *out, size_t outlen)
{
    snprintf(out, outlen, lat ? "%02d %02d.%03d" : "%03d %02d.%03d", deg, mmm / 1000, mmm % 1000);
}

/* ---------------------------------------------------------------- write areas, resets, files */

const int *cfg_write_areas_of(const unsigned char *img, int factory)
{
    const cfgmodel *m = M(img);

    return factory ? m->write_areas_factory : m->write_areas;
}

void cfg_defaults(unsigned char *img, const cfgmodel *m)
{
    if (!m) m = &models[0];
    memset(img, 0xff, CFG_SIZE);
    if (m->code == MODEL_FTA850) cfg_defaults_850(img);
    else {
        cfg_defaults_750(img);
        set16(img, m->o_model, m->code);
        set16(img, m->o_footer, m->code);
    }
}

int cfg_nresets(const unsigned char *img)
{
    return M(img)->nresets;
}

const char *cfg_reset_name(const unsigned char *img, int which, int shortname)
{
    const cfgmodel *m = M(img);

    if (which < 0 || which >= m->nresets) return "";
    return shortname ? m->reset_short[which] : m->reset_names[which];
}

void cfg_reset(unsigned char *img, int which)
{
    const cfgmodel *m = M(img);
    unsigned char *def = malloc(CFG_SIZE);
    const int *r;

    if (!def || which < 0 || which >= m->nresets) { free(def); return; }
    cfg_defaults(def, m);
    for (r = m->resets[which]; r[0] >= 0; r += 2) memcpy(img + r[0], def + r[0], (size_t)(r[1] - r[0]));
    free(def);
}

int cfg_load(const char *path, unsigned char *img, char *err, size_t errlen)
{
    FILE *f = fopen(path, "rb");
    size_t n;
    const cfgmodel *m;

    if (!f) { snprintf(err, errlen, "Cannot open %s", path); return 0; }
    memset(img, 0xff, CFG_SIZE);
    n = fread(img, 1, CFG_SIZE, f);
    fclose(f);
    if (n != 0x4000 && n != 0x8000) { snprintf(err, errlen, "%s is not a 16 or 32 kB radio image", path); return 0; }
    m = cfg_model_of(img);
    if (m && (size_t)m->size != n) { snprintf(err, errlen, "%s is %zu bytes, not the %d of an %s image", path, n, m->size, m->name); return 0; }
    return cfg_check(img, err, errlen);
}

int cfg_save(const char *path, const unsigned char *img, char *err, size_t errlen)
{
    const cfgmodel *m = M(img);
    FILE *f = fopen(path, "wb");

    if (!f) { snprintf(err, errlen, "Cannot write %s", path); return 0; }
    if (fwrite(img, 1, (size_t)m->size, f) != (size_t)m->size) { fclose(f); snprintf(err, errlen, "Write error on %s", path); return 0; }
    fclose(f);
    err[0] = 0;
    return 1;
}
