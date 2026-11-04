/*
 * Yaesu air band handheld link: the radio's USB port is a CDC-ACM virtual
 * serial port (26aa:0024) that names itself after the model, except that an
 * FTA-850 calls itself "FTA-750". Switched on with a key held (SQL on the
 * FTA-850, MENU on the FTA-750, 550 and 450) the radio is in "CP mode" and
 * answers the programming protocol that Yaesu's YCE46 and YCE01 software
 * and the Standard Horizon HX870 family share:
 *
 *   "?"                      in CP mode the radio answers "@"; in its normal
 *                            mode it answers "P" to "P" and sends a "P" on
 *                            its own every few seconds
 *   "P" "0" "ACMD:002\r\n"   start of every session, not answered
 *   #CMDSY                   sync, answered #CMDOK
 *   #CVRRQ                   firmware version, answered #CVRDQ <ver>
 *   #CEPSR 00                status, answered #CEPSD 00 (ready) or 01
 *   #CEPRD addr len          read config memory, answered #CEPDT addr len hex
 *   #CEPWR addr len hex      write config memory, answered #CMDOK
 *   $PMTK183 / 622,1 / 184,1 GPS logger status / dump / erase (NMEA style)
 *
 * Fields are tab separated and end with an XOR checksum of everything up to
 * and including the last tab, then CR LF. Data replies are acknowledged with
 * #CMDOK, or the radio repeats them.
 *
 * The config memory is 32 kB on the FTA-850 and 16 kB on the others; the
 * model is told from the model code read at the start of the session (at
 * 0000 on the 16 kB models, 0100 on the FTA-850). See cfg.h for the layouts.
 *
 * Everything is non-blocking and driven by radio_poll(), which the GUI calls
 * from a timer, so the window never waits on the port. One operation runs at
 * a time, as a sequence of request and reply steps with the vendor's timing:
 * 2 s per reply, 5 tries, a status poll before every transfer.
 */
#ifndef RADIO_H
#define RADIO_H

#include <stddef.h>

#define CFG_SIZE     0x8000   /* the largest image: every buffer is this big */

typedef enum { RM_NONE, RM_NORMAL, RM_CP } radio_mode;
typedef enum { OP_NONE, OP_READ, OP_WRITE, OP_LOGSTAT, OP_LOGREAD, OP_LOGERASE } radio_op;

typedef struct {
    /* link */
    int        sim;           /* simulated radio, no hardware */
    int        connected;     /* CP mode session is up: ready for operations */
    radio_mode mode;          /* what was last seen on the port */
    char       port[128];     /* device path in use, or wanted */
    int        port_fixed;    /* given by the user: no auto-detect */
    char       version[16];   /* firmware version from #CVRDQ */
    char       status[256];   /* short human readable link status */
    int        model;         /* model code of the connected radio, 0 unknown */
    int        img_size;      /* its image size in bytes, 0 unknown */
    int        usb_model;     /* model number the USB port names (750 for an 850 too), 0 unknown */
    char       cphint[128];   /* how to get this radio into CP mode */

    /* the operation in progress, if any */
    radio_op   op;
    int        progress, total;   /* bytes or lines done and expected */
    int        finished;          /* an operation just ended; the GUI clears this */
    int        ok;                /* ... and whether it succeeded */
    char       result[256];       /* ... with a message */

    /* data */
    unsigned char img[CFG_SIZE];  /* image read by OP_READ */
    int        img_model;         /* model code found in it */
    const unsigned char *wimg;    /* image given to OP_WRITE */
    const int *wareas;            /* ... and the areas of it to send */
    int        log_records;       /* GPS logger: records stored (OP_LOGSTAT) */
    int        log_pct;           /* ... and percent of the log memory used */
    unsigned char *log;           /* raw log after OP_LOGREAD, malloc'd */
    size_t     loglen;
} radio_state;

/* Open the link. port NULL or "" auto-detects. sim_model nonzero simulates a
   radio of that model code (MODEL_FTA850 and the others in cfg.h). */
void radio_init(radio_state *r, const char *port, int sim_model);

/* Service the link: call every ~50 ms. Returns nonzero if state changed. */
int radio_poll(radio_state *r);

/* Close the port. */
void radio_shutdown(radio_state *r);

/* Start an operation. For OP_WRITE img is the image to write and areas the
   list of ranges to send (cfg_write_areas_of), else both NULL. Returns 0 if
   the radio is not ready, the image is for another model, or something is
   running; result says why. */
int radio_start(radio_state *r, radio_op op, const unsigned char *img, const int *areas);

/* Abandon the operation in progress. */
void radio_cancel(radio_state *r);

/* Name of an operation, for messages. */
const char *radio_opname(radio_op op);

#endif
