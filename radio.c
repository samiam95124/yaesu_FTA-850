/*
 * FTA-850L link. See radio.h for the protocol.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <math.h>
#include <ctype.h>
#include <time.h>
#include <errno.h>
#ifdef _WIN32
#include <windows.h>
#include <initguid.h>
#include <setupapi.h>
#include <devguid.h>
#include <devpkey.h>
#else
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
#include <termios.h>
#include <sys/ioctl.h>
#endif

#include "radio.h"
#include "cfg.h"

#define REPLY_MS   2000   /* the vendor's time for a reply */
#define RETRIES    5      /* ... and tries per command */
#define KEEP_TRIES 2      /* tries of the idle sync before the radio counts as gone */
#define DETECT_MS  1200   /* time allowed for the answer to "?" */
#define RETRY_MS   2000   /* between looks for the radio */
#define KEEP_MS    3000   /* idle sync interval */
#define BACKOFF_MS 20     /* first wait when the radio reports busy, doubling */
#define CHUNK      64     /* bytes per config transfer, as the vendor uses */
#define LOG_MAX    (1024 * 1024)

typedef enum { W_NONE, W_CMDOK, W_CEPSD, W_CEPDT, W_CVRDQ, W_LOGSTAT, W_LOGLINE, W_LOGERASE } waitk;

typedef enum {
    S_NOPORT, S_DETECT, S_DETECT_WAIT, S_HS_SYNC1, S_HS_SYNC2, S_HS_VER,
    S_HS_STAT1, S_HS_RD1, S_HS_STAT2, S_HS_RD2, S_HS_STAT3, S_HS_RD3, S_READY, S_KEEP,
    S_RD_STAT, S_RD_DATA,
    S_WR_STAT_H, S_WR_RD_H, S_WR_STAT_F, S_WR_RD_F, S_WR_STAT, S_WR_DATA,
    S_LG_STAT, S_LG_STATUS, S_LG_READ, S_LG_ERASE_STAT, S_LG_ERASE
} lstate;

/* link private state: one radio per program */
static char   rx[16384];
static int    rxlen;
static lstate st = S_NOPORT;
static waitk  awaiting;
static long   deadline, resend_at, next_retry, next_keep;
static int    tries, backoff;
static char   lastcmd[320];
static int    lastlen;
static int    cur, range_i, chunk;       /* transfer position */
static unsigned char hdr[2], ftr[2];     /* model code read at the handshake and before a write */
static const cfgmodel *wr_model;         /* the model of the image being written */
static int    sim_model;                 /* model code the simulator plays */
static int    log_expected, log_got;     /* $PMTKLOX lines */
static unsigned char *logbuf;
static size_t loglen, logcap;

/* simulator */
static unsigned char sim_img[CFG_SIZE];
static unsigned char *sim_log;
static size_t sim_loglen;
static int    sim_records, sim_ticks, sim_ready;

static long now_ms(void)
{
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long)ts.tv_sec * 1000L + ts.tv_nsec / 1000000L;
}

const char *radio_opname(radio_op op)
{
    switch (op) {
        case OP_READ: return "Read from radio";
        case OP_WRITE: return "Program to radio";
        case OP_LOGSTAT: return "Logger status";
        case OP_LOGREAD: return "Logger download";
        case OP_LOGERASE: return "Logger erase";
        default: return "Idle";
    }
}

/* ---------------------------------------------------------------- port */

/* The serial port, non-blocking, in two editions. POSIX: a termios device,
   found by the radio's USB identity where Linux keeps one in
   /dev/serial/by-id, else the first CDC serial port (ttyACM* on Linux,
   cu.usbmodem* on macOS). Windows: a COM port on the usbser driver, found
   through SetupAPI by the radio's USB vendor and product (26aa:0024), its USB
   product string ("FTA-750"), or failing those any usbser port. port_desc is
   the identity it was found by, where the model number is read from. */

static char port_desc[128];   /* the identity the port was found by */
static char port_err[256];    /* why the last open failed, for the status line */

#ifdef _WIN32

static HANDLE hport = INVALID_HANDLE_VALUE;

static int port_is_open(void)
{
    return hport != INVALID_HANDLE_VALUE;
}

/* The COM port to use: the one named want, if given, else the best candidate
   for the radio among the ports present. Fills out with its name and
   port_desc with the USB product string. */
static int find_port(const char *want, char *out, size_t outlen)
{
    HDEVINFO h;
    SP_DEVINFO_DATA d;
    DWORD i, sz;
    int best = 0;

    if (want && !strncmp(want, "\\\\.\\", 4)) want += 4;
    h = SetupDiGetClassDevsA(&GUID_DEVCLASS_PORTS, NULL, NULL, DIGCF_PRESENT);
    if (h == INVALID_HANDLE_VALUE) return 0;
    d.cbSize = sizeof d;
    for (i = 0; SetupDiEnumDeviceInfo(h, i, &d); i++) {
        char hw[512] = "", svc[64] = "", name[32] = "", desc[128];
        WCHAR bus[128] = L"";
        DEVPROPTYPE pt;
        HKEY k;
        int score = 0, j;

        k = SetupDiOpenDevRegKey(h, &d, DICS_FLAG_GLOBAL, 0, DIREG_DEV, KEY_READ);
        if (k == INVALID_HANDLE_VALUE) continue;
        sz = sizeof name - 1;
        if (RegQueryValueExA(k, "PortName", NULL, NULL, (BYTE *)name, &sz) != ERROR_SUCCESS) name[0] = 0;
        RegCloseKey(k);
        if (!name[0]) continue;
        SetupDiGetDeviceRegistryPropertyA(h, &d, SPDRP_HARDWAREID, NULL, (BYTE *)hw, sizeof hw - 1, NULL);
        SetupDiGetDeviceRegistryPropertyA(h, &d, SPDRP_SERVICE, NULL, (BYTE *)svc, sizeof svc - 1, NULL);
        SetupDiGetDevicePropertyW(h, &d, &DEVPKEY_Device_BusReportedDeviceDesc, &pt, (BYTE *)bus, sizeof bus - 2, &sz, 0);
        for (j = 0; bus[j] && j < (int)sizeof desc - 1; j++) desc[j] = bus[j] < 128 ? (char)bus[j] : '?';
        desc[j] = 0;
        for (j = 0; hw[j]; j++) hw[j] = (char)toupper((unsigned char)hw[j]);
        if (strstr(hw, "VID_26AA&PID_0024")) score = 3;
        else if (strstr(desc, "FTA-")) score = 2;
        else if (!strcasecmp(svc, "usbser")) score = 1;
        if (want) {
            if (strcasecmp(name, want)) continue;
            score = 4;
        }
        if (score > best) {
            best = score;
            snprintf(out, outlen, "%s", name);
            snprintf(port_desc, sizeof port_desc, "%s", desc);
        }
        if (score == 4) break;
    }
    SetupDiDestroyDeviceInfoList(h);
    return best > 0;
}

static int port_open(const char *path)
{
    char full[160];
    DCB dcb;
    COMMTIMEOUTS to;
    DWORD e;

    /* COM10 and up need the device prefix, and it does no harm below */
    if (!strncmp(path, "\\\\.\\", 4)) snprintf(full, sizeof full, "%s", path);
    else snprintf(full, sizeof full, "\\\\.\\%s", path);
    hport = CreateFileA(full, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    if (hport == INVALID_HANDLE_VALUE) {
        e = GetLastError();
        if (e == ERROR_ACCESS_DENIED) snprintf(port_err, sizeof port_err, "Cannot open %s: in use by another program", path);
        else if (e == ERROR_FILE_NOT_FOUND) snprintf(port_err, sizeof port_err, "Cannot open %s: no such port", path);
        else snprintf(port_err, sizeof port_err, "Cannot open %s: Windows error %lu", path, (unsigned long)e);
        return 0;
    }
    memset(&dcb, 0, sizeof dcb);
    dcb.DCBlength = sizeof dcb;
    GetCommState(hport, &dcb);
    dcb.BaudRate = CBR_115200;
    dcb.ByteSize = 8;
    dcb.Parity = NOPARITY;
    dcb.StopBits = ONESTOPBIT;
    dcb.fBinary = TRUE;
    dcb.fParity = FALSE;
    dcb.fOutxCtsFlow = FALSE;
    dcb.fOutxDsrFlow = FALSE;
    dcb.fDsrSensitivity = FALSE;
    dcb.fTXContinueOnXoff = TRUE;
    dcb.fOutX = FALSE;
    dcb.fInX = FALSE;
    dcb.fErrorChar = FALSE;
    dcb.fNull = FALSE;
    dcb.fAbortOnError = FALSE;
    /* DTR up, as Linux leaves it on open: the radio was seen to answer that
       way; with DTR low it may keep quiet */
    dcb.fDtrControl = DTR_CONTROL_ENABLE;
    dcb.fRtsControl = RTS_CONTROL_ENABLE;
    /* reads return what is there at once; writes may wait a little */
    to.ReadIntervalTimeout = MAXDWORD;
    to.ReadTotalTimeoutMultiplier = 0;
    to.ReadTotalTimeoutConstant = 0;
    to.WriteTotalTimeoutMultiplier = 0;
    to.WriteTotalTimeoutConstant = 1000;
    if (!SetCommState(hport, &dcb) || !SetCommTimeouts(hport, &to)) {
        snprintf(port_err, sizeof port_err, "Cannot open %s: not a serial port that can be set up", path);
        CloseHandle(hport);
        hport = INVALID_HANDLE_VALUE;
        return 0;
    }
    PurgeComm(hport, PURGE_RXCLEAR | PURGE_TXCLEAR);
    return 1;
}

static void port_close(void)
{
    if (hport != INVALID_HANDLE_VALUE) CloseHandle(hport);
    hport = INVALID_HANDLE_VALUE;
}

static void port_flush_in(void)
{
    if (hport != INVALID_HANDLE_VALUE) PurgeComm(hport, PURGE_RXCLEAR);
}

/* what the port has: the byte count, 0 for nothing yet, -1 when it is gone */
static int port_read(char *buf, int n)
{
    DWORD got = 0;

    if (hport == INVALID_HANDLE_VALUE) return -1;
    if (!ReadFile(hport, buf, (DWORD)n, &got, NULL)) return -1;
    return (int)got;
}

static void send_raw(const char *s, int n)
{
    DWORD put;

    if (hport == INVALID_HANDLE_VALUE) return;
    if (!WriteFile(hport, s, (DWORD)n, &put, NULL)) { /* a dead port shows up as a timeout */ }
}

#else /* POSIX */

static int fd = -1;

static int port_is_open(void)
{
    return fd >= 0;
}

/* The port to use: want, if given, else the radio by its USB identity (Linux
   keeps a stable name for it), else the first CDC serial port: ttyACM* on
   Linux, cu.usbmodem* on macOS. */
static int find_port(const char *want, char *out, size_t outlen)
{
    static const char *prefixes[] = { "ttyACM", "cu.usbmodem", NULL };
    DIR *d;
    struct dirent *e;
    int i, found = 0;

    if (want) {
        snprintf(out, outlen, "%s", want);
        snprintf(port_desc, sizeof port_desc, "%s", want);
        return 1;
    }
    d = opendir("/dev/serial/by-id");
    if (d) {
        while ((e = readdir(d)) != NULL) {
            if (strstr(e->d_name, "FTA-") || strstr(e->d_name, "YAESU")) {
                snprintf(out, outlen, "/dev/serial/by-id/%s", e->d_name);
                found = 1;
                break;
            }
        }
        closedir(d);
        if (found) { snprintf(port_desc, sizeof port_desc, "%s", out); return 1; }
    }
    d = opendir("/dev");
    if (!d) return 0;
    for (i = 0; prefixes[i] && !found; i++) {
        rewinddir(d);
        while ((e = readdir(d)) != NULL) {
            if (!strncmp(e->d_name, prefixes[i], strlen(prefixes[i]))) {
                snprintf(out, outlen, "/dev/%s", e->d_name);
                found = 1;
                break;
            }
        }
    }
    closedir(d);
    if (found) snprintf(port_desc, sizeof port_desc, "%s", out);
    return found;
}

static int port_open(const char *path)
{
    struct termios t;
    int f;

    f = open(path, O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (f < 0) {
        if (errno == EACCES) snprintf(port_err, sizeof port_err, "No permission for %s (join the dialout group)", path);
        else snprintf(port_err, sizeof port_err, "Cannot open %s: %s", path, strerror(errno));
        return 0;
    }
    if (tcgetattr(f, &t) < 0) {
        snprintf(port_err, sizeof port_err, "Cannot open %s: %s", path, strerror(errno));
        close(f);
        return 0;
    }
    cfmakeraw(&t);
    cfsetispeed(&t, B115200);
    cfsetospeed(&t, B115200);
    t.c_cflag |= (CLOCAL | CREAD);
    t.c_cflag &= ~(PARENB | CSTOPB | CRTSCTS);
    t.c_cflag = (t.c_cflag & ~CSIZE) | CS8;
    t.c_cc[VMIN] = 0;
    t.c_cc[VTIME] = 0;
    if (tcsetattr(f, TCSANOW, &t) < 0) {
        snprintf(port_err, sizeof port_err, "Cannot open %s: %s", path, strerror(errno));
        close(f);
        return 0;
    }
    /* DTR stays asserted, as the kernel leaves it on open: the radio was seen
       to answer that way; with DTR low it may keep quiet */
    tcflush(f, TCIOFLUSH);
    fd = f;
    return 1;
}

static void port_close(void)
{
    if (fd >= 0) close(fd);
    fd = -1;
}

static void port_flush_in(void)
{
    if (fd >= 0) tcflush(fd, TCIFLUSH);
}

/* what the port has: the byte count, 0 for nothing yet, -1 when it is gone */
static int port_read(char *buf, int n)
{
    ssize_t got;

    if (fd < 0) return -1;
    got = read(fd, buf, (size_t)n);
    if (got < 0) return errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR ? 0 : -1;
    return (int)got;
}

static void send_raw(const char *s, int n)
{
    if (fd < 0) return;
    if (write(fd, s, (size_t)n) < 0) { /* a dead port shows up as a timeout */ }
}

#endif

/* a # command: fields joined by tabs, the checksum and CR LF */
static int build(char *out, size_t outlen, const char *type, const char *a1, const char *a2, const char *a3)
{
    int n, i;
    unsigned char cs = 0;

    n = snprintf(out, outlen, "%s\t", type);
    if (a1) n += snprintf(out + n, outlen - (size_t)n, "%s\t", a1);
    if (a2) n += snprintf(out + n, outlen - (size_t)n, "%s\t", a2);
    if (a3) n += snprintf(out + n, outlen - (size_t)n, "%s\t", a3);
    for (i = 0; i < n; i++) cs ^= (unsigned char)out[i];
    n += snprintf(out + n, outlen - (size_t)n, "%02X\r\n", cs);
    return n;
}

/* send a command and wait for its reply */
static void issue(const char *cmd, int n, waitk w, lstate s)
{
    memcpy(lastcmd, cmd, (size_t)n);
    lastlen = n;
    awaiting = w;
    st = s;
    tries = 0;
    backoff = BACKOFF_MS;
    resend_at = 0;
    deadline = now_ms() + REPLY_MS;
    send_raw(cmd, n);
}

static void issue_cmd(const char *type, const char *a1, const char *a2, const char *a3, waitk w, lstate s)
{
    char buf[320];
    int n = build(buf, sizeof buf, type, a1, a2, a3);

    issue(buf, n, w, s);
}

static void issue_status(lstate s)
{
    issue_cmd("#CEPSR", "00", NULL, NULL, W_CEPSD, s);
}

static void issue_read(int addr, int len, lstate s)
{
    char a[8], l[8];

    snprintf(a, sizeof a, "%04X", addr);
    snprintf(l, sizeof l, "%02X", len);
    issue_cmd("#CEPRD", a, l, NULL, W_CEPDT, s);
}

static void ack(void)
{
    send_raw("#CMDOK\r\n", 8);
}

/* ---------------------------------------------------------------- state changes */

static void set_status(radio_state *r, const char *s)
{
    snprintf(r->status, sizeof r->status, "%s", s);
}

static void finish(radio_state *r, int ok, const char *msg)
{
    r->finished = 1;
    r->ok = ok;
    snprintf(r->result, sizeof r->result, "%s", msg);
    r->op = OP_NONE;
    awaiting = W_NONE;
    resend_at = 0;
    if (!r->sim) {
        st = S_READY;
        next_keep = now_ms() + KEEP_MS;
    }
}

static void disconnect(radio_state *r, const char *why)
{
    port_close();
    rxlen = 0;
    st = S_NOPORT;
    awaiting = W_NONE;
    resend_at = 0;
    r->connected = 0;
    r->mode = RM_NONE;
    r->version[0] = 0;
    r->model = 0;
    r->img_size = 0;
    if (r->op != OP_NONE) finish(r, 0, why);
    set_status(r, why);
    next_retry = now_ms() + RETRY_MS;
}

static void start_detect(radio_state *r)
{
    rxlen = 0;
    port_flush_in();
    send_raw("?", 1);
    st = S_DETECT_WAIT;
    deadline = now_ms() + DETECT_MS;
    (void)r;
}

static void try_open(radio_state *r)
{
    char path[128];

    if (!r->port_fixed) {
        if (!find_port(NULL, path, sizeof path)) {
            set_status(r, "No radio found on USB");
            next_retry = now_ms() + RETRY_MS;
            return;
        }
        snprintf(r->port, sizeof r->port, "%s", path);
    } else {
        /* the given port: looked up only for its identity */
        port_desc[0] = 0;
        find_port(r->port, path, sizeof path);
    }
    /* the USB name tells the model, except that an FTA-850 says FTA-750 */
    {
        const char *p = strstr(port_desc, "FTA-");

        if (!p) p = strstr(r->port, "FTA-");
        r->usb_model = p ? atoi(p + 4) : 0;
        if (r->usb_model == 550 || r->usb_model == 450)
            snprintf(r->cphint, sizeof r->cphint, "switch it off, then hold MENU while switching it on");
        else if (r->usb_model == 750)
            snprintf(r->cphint, sizeof r->cphint, "switch it off, then hold SQL (FTA-850) or MENU (FTA-750) while switching it on");
        else
            snprintf(r->cphint, sizeof r->cphint, "switch it off, then hold SQL (FTA-850) or MENU (FTA-750/550/450) while switching it on");
    }
    if (!port_open(r->port)) {
        set_status(r, port_err);
        next_retry = now_ms() + RETRY_MS;
        return;
    }
    r->mode = RM_NONE;
    st = S_DETECT;
    next_retry = now_ms();
}

/* the session is up and the model code is in hand */
static void model_found(radio_state *r, int code)
{
    const cfgmodel *m = cfg_model_by_code(code);

    r->model = code;
    r->img_size = m ? m->size : 0;
    r->connected = 1;
    if (m) snprintf(r->status, sizeof r->status, "%s in CP mode, firmware %s", m->name, r->version);
    else snprintf(r->status, sizeof r->status, "Unknown model %04X in CP mode, firmware %s: it can only be read raw", code, r->version);
    st = S_READY;
    next_keep = now_ms() + KEEP_MS;
}

/* the next step after a reply arrived */
static void advance(radio_state *r)
{
    const int *a;
    char lbuf[8], abuf[8], hex[CHUNK * 2 + 1];
    int i, end;
    unsigned char data[CHUNK];

    awaiting = W_NONE;
    switch (st) {
        case S_HS_SYNC1:
            issue("#CMDSY\r\n", 8, W_CMDOK, S_HS_SYNC2);
            break;
        case S_HS_SYNC2:
            issue_cmd("#CVRRQ", NULL, NULL, NULL, W_CVRDQ, S_HS_VER);
            break;
        case S_HS_VER:
            /* which model: the 16 kB models keep the code at 0000. The FTA-850 has
               FFFF there and keeps its code at 7FFE; Yaesu's files carry it at 0100
               as well, but the radio itself holds 0101 there (firmware 02.02.00),
               so that address is only the last resort */
            issue_status(S_HS_STAT1);
            break;
        case S_HS_STAT1: issue_read(0x0000, 2, S_HS_RD1); break;
        case S_HS_RD1:
            if (hdr[0] == 0xff && hdr[1] == 0xff) { issue_status(S_HS_STAT2); break; }
            model_found(r, hdr[0] * 256 + hdr[1]);
            break;
        case S_HS_STAT2: issue_read(0x7FFE, 2, S_HS_RD2); break;
        case S_HS_RD2:
            if (!cfg_model_by_code(hdr[0] * 256 + hdr[1])) { issue_status(S_HS_STAT3); break; }
            model_found(r, hdr[0] * 256 + hdr[1]);
            break;
        case S_HS_STAT3: issue_read(0x0100, 2, S_HS_RD3); break;
        case S_HS_RD3:
            model_found(r, hdr[0] * 256 + hdr[1]);
            break;
        case S_KEEP:
            st = S_READY;
            next_keep = now_ms() + KEEP_MS;
            break;

        case S_RD_STAT:
            issue_read(cur, CHUNK, S_RD_DATA);
            break;
        case S_RD_DATA:
            cur += CHUNK;
            r->progress = cur;
            if (cur >= r->img_size) { r->img_model = cfg_model(r->img); finish(r, 1, "Read from radio complete"); }
            else issue_status(S_RD_STAT);
            break;

        case S_WR_STAT_H: issue_read(wr_model->o_model, 2, S_WR_RD_H); break;
        case S_WR_RD_H:   issue_status(S_WR_STAT_F); break;
        case S_WR_STAT_F: issue_read(wr_model->o_footer, 2, S_WR_RD_F); break;
        case S_WR_RD_F:
            /* the footer is the radio's own copy of the code. The header is shown
               but not required: an FTA-850 keeps 0101 at 0100, not 0352 */
            if (ftr[0] * 256 + ftr[1] != wr_model->code) {
                char m[128];

                snprintf(m, sizeof m, "The radio holds model code %02X%02X/%02X%02X and the image is for an %s (%04X): not written",
                         hdr[0], hdr[1], ftr[0], ftr[1], wr_model->name, wr_model->code);
                finish(r, 0, m);
                break;
            }
            range_i = 0;
            cur = r->wareas[0];
            issue_status(S_WR_STAT);
            break;
        case S_WR_STAT:
            a = r->wareas + 2 * range_i;
            end = a[1];
            chunk = end - cur < CHUNK ? end - cur : CHUNK;
            memcpy(data, r->wimg + cur, (size_t)chunk);
            for (i = 0; i < chunk; i++) snprintf(hex + 2 * i, 3, "%02X", data[i]);
            snprintf(abuf, sizeof abuf, "%04X", cur);
            snprintf(lbuf, sizeof lbuf, "%02X", chunk);
            issue_cmd("#CEPWR", abuf, lbuf, hex, W_CMDOK, S_WR_DATA);
            break;
        case S_WR_DATA:
            cur += chunk;
            r->progress += chunk;
            a = r->wareas + 2 * range_i;
            if (cur >= a[1]) {
                range_i++;
                if (r->wareas[2 * range_i] < 0) { finish(r, 1, "Program to radio complete"); break; }
                cur = r->wareas[2 * range_i];
            }
            issue_status(S_WR_STAT);
            break;

        case S_LG_STAT:
            issue("$PMTK183*38\r\n", 13, W_LOGSTAT, S_LG_STATUS);
            break;
        case S_LG_ERASE_STAT:
            issue("$PMTK184,1*22\r\n", 15, W_LOGERASE, S_LG_ERASE);
            break;
        default:
            break;
    }
}

static void step_failed(radio_state *r, const char *what)
{
    char m[200];

    if (st == S_DETECT_WAIT) return;
    if (st == S_HS_SYNC1 || st == S_HS_SYNC2 || st == S_HS_VER || st == S_HS_STAT1 || st == S_HS_RD1 ||
        st == S_HS_STAT2 || st == S_HS_RD2 || st == S_HS_STAT3 || st == S_HS_RD3 || st == S_KEEP) {
        snprintf(m, sizeof m, "Radio stopped answering (%s)", what);
        disconnect(r, m);
        return;
    }
    snprintf(m, sizeof m, "%s failed: %s", radio_opname(r->op), what);
    finish(r, 0, m);
}

/* ---------------------------------------------------------------- replies */

static int checksum_ok(const char *line)
{
    const char *t = strrchr(line, '\t');
    unsigned char cs = 0;
    const char *p;
    unsigned v;

    if (!t || strlen(t + 1) != 2) return 0;
    for (p = line; p <= t; p++) cs ^= (unsigned char)*p;
    if (sscanf(t + 1, "%2x", &v) != 1) return 0;
    return v == cs;
}

static int nmea_ok(const char *line)
{
    const char *star = strrchr(line, '*');
    const char *p;
    unsigned char cs = 0;
    unsigned v;

    if (!star || line[0] != '$') return 0;
    for (p = line + 1; p < star; p++) cs ^= (unsigned char)*p;
    if (sscanf(star + 1, "%2x", &v) != 1) return 0;
    return v == cs;
}

static int split(char *line, char *f[], int max, char sep)
{
    int n = 0;
    char *p = line, *q;

    while (p && n < max) {
        f[n++] = p;
        q = strchr(p, sep);
        if (q) *q++ = 0;
        p = q;
    }
    return n;
}

static void log_append(const char *hexword)
{
    size_t n = strlen(hexword) / 2, i;
    unsigned v;

    if (loglen + n > logcap) {
        size_t nc = logcap ? logcap * 2 : 65536;
        unsigned char *p;

        if (nc > LOG_MAX) return;
        p = realloc(logbuf, nc);
        if (!p) return;
        logbuf = p;
        logcap = nc;
    }
    for (i = 0; i < n; i++) {
        if (sscanf(hexword + 2 * i, "%2x", &v) != 1) return;
        logbuf[loglen++] = (unsigned char)v;
    }
}

static void log_line(radio_state *r, char *line)
{
    char *f[64];
    int n, k;
    char copy[8192];

    if (!nmea_ok(line)) return;
    *strrchr(line, '*') = 0;
    snprintf(copy, sizeof copy, "%s", line);
    n = split(copy, f, 64, ',');
    if (!strcmp(f[0], "$PMTK010") && n >= 2 && !strcmp(f[1], "002")) {
        /* the GPS module restarted: repeat the request */
        if (awaiting == W_LOGSTAT || awaiting == W_LOGERASE) { send_raw(lastcmd, lastlen); deadline = now_ms() + REPLY_MS; }
        return;
    }
    if (!strcmp(f[0], "$PMTKLOG")) {
        if (n == 11) {
            r->log_records = atoi(f[9]);
            r->log_pct = atoi(f[10]);
        }
        return;   /* the acknowledgement follows */
    }
    if (!strcmp(f[0], "$PMTK001") && n >= 3) {
        if (!strcmp(f[1], "183") && awaiting == W_LOGSTAT) {
            if (r->op == OP_LOGSTAT) {
                char m[96];

                snprintf(m, sizeof m, "Logger holds %d records (%d%% of its memory)", r->log_records, r->log_pct);
                finish(r, 1, m);
                return;
            }
            if (r->log_records < 1) { finish(r, 0, "The logger holds no data"); return; }
            loglen = 0;
            log_expected = 0;
            log_got = 0;
            r->progress = 0;
            r->total = 0;
            issue("$PMTK622,1*29\r\n", 15, W_LOGLINE, S_LG_READ);
        } else if (!strcmp(f[1], "622") && awaiting == W_LOGLINE) {
            char m[96];

            free(r->log);
            r->log = malloc(loglen ? loglen : 1);
            if (r->log) memcpy(r->log, logbuf, loglen);
            r->loglen = r->log ? loglen : 0;
            snprintf(m, sizeof m, "Logger download complete: %d lines, %lu bytes", log_got, (unsigned long)loglen);
            finish(r, 1, m);
        } else if (!strcmp(f[1], "184") && awaiting == W_LOGERASE) {
            r->log_records = 0;
            r->log_pct = 0;
            finish(r, 1, "Logger erased");
        }
        return;
    }
    if (!strcmp(f[0], "$PMTKLOX") && awaiting == W_LOGLINE && n >= 2) {
        if (!strcmp(f[1], "0") && n >= 3) {
            log_expected = atoi(f[2]);
            r->total = log_expected;
        } else if (!strcmp(f[1], "1") && n >= 3) {
            for (k = 3; k < n; k++) log_append(f[k]);
            log_got++;
            r->progress = log_got;
        }
        tries = 0;
        deadline = now_ms() + REPLY_MS;
    }
}

static void handle_line(radio_state *r, char *line)
{
    char *f[8];
    int n;

    if (line[0] == '$') { log_line(r, line); return; }
    if (line[0] != '#') return;
    if (!strncmp(line, "#CMDOK", 6)) {
        if (awaiting == W_CMDOK) { tries = 0; advance(r); }
        return;
    }
    if (!strncmp(line, "#CMDER", 6) || !strncmp(line, "#CMDUN", 6) || !strncmp(line, "#CMDSM", 6)) {
        if (awaiting != W_NONE) {
            if (++tries >= RETRIES) step_failed(r, line);
            else { send_raw(lastcmd, lastlen); deadline = now_ms() + REPLY_MS; }
        }
        return;
    }
    if (!strncmp(line, "#CEPSD", 6)) {
        if (!checksum_ok(line)) return;
        ack();
        n = split(line, f, 8, '\t');
        if (awaiting != W_CEPSD || n < 2) return;
        if (!strcmp(f[1], "00")) { tries = 0; advance(r); return; }
        /* busy: ask again after a while, backing off */
        if (++tries >= RETRIES) { step_failed(r, "radio not ready"); return; }
        resend_at = now_ms() + backoff;
        deadline = resend_at + REPLY_MS;
        backoff *= 2;
        return;
    }
    if (!strncmp(line, "#CEPDT", 6)) {
        unsigned addr, len, v;
        int i;
        unsigned char data[256];

        if (!checksum_ok(line)) return;
        ack();
        n = split(line, f, 8, '\t');
        if (awaiting != W_CEPDT || n < 5) return;
        if (sscanf(f[1], "%4x", &addr) != 1 || sscanf(f[2], "%2x", &len) != 1 || len > 255) return;
        if (strlen(f[3]) != 2 * len) return;
        for (i = 0; i < (int)len; i++) { if (sscanf(f[3] + 2 * i, "%2x", &v) != 1) return; data[i] = (unsigned char)v; }
        if (st == S_RD_DATA) {
            if ((int)addr != cur || len != CHUNK) return;
            memcpy(r->img + cur, data, CHUNK);
        } else if (st == S_HS_RD1 || st == S_HS_RD2 || st == S_HS_RD3) {
            if (addr != (st == S_HS_RD1 ? 0x0000u : st == S_HS_RD2 ? 0x7FFEu : 0x0100u) || len != 2) return;
            memcpy(hdr, data, 2);
        } else if (st == S_WR_RD_H) {
            if ((int)addr != wr_model->o_model || len != 2) return;
            memcpy(hdr, data, 2);
        } else if (st == S_WR_RD_F) {
            if ((int)addr != wr_model->o_footer || len != 2) return;
            memcpy(ftr, data, 2);
        } else return;
        tries = 0;
        advance(r);
        return;
    }
    if (!strncmp(line, "#CVRDQ", 6)) {
        ack();
        n = split(line, f, 8, '\t');
        if (n >= 2) snprintf(r->version, sizeof r->version, "%s", f[1]);
        if (awaiting == W_CVRDQ) { tries = 0; advance(r); }
        return;
    }
}

/* ---------------------------------------------------------------- simulator */

static void sim_build(int code)
{
    const cfgmodel *m = cfg_model_by_code(code);
    static const struct { const char *tag; int freq, group; int pos, latd, latm, lond, lonm; } s[] = {
        { "KRHV-ATIS", 125200, 1, 1, 37, 19583, 121, 49113 },
        { "KRHV-GND", 121650, 3, 0, 0, 0, 0, 0 },
        { "KRHV-TWR", 119800, 2, 0, 0, 0, 0, 0 },
        { "KRHV-UNI", 122950, 0, 0, 0, 0, 0, 0 },
        { "NOR-APR", 120100, 0, 0, 0, 0, 0, 0 },
        { "KPAO TWR", 118600, 2, 1, 37, 27683, 122, 6900 },
        { "KSQL TWR", 119000, 2, 1, 37, 30717, 122, 15000 },
        { "SJC VOR", 114100, 4, 1, 37, 22500, 121, 56700 },
        { "OAK VOR", 116800, 4, 1, 37, 43550, 122, 13400 },
        { "WOODSIDE VOR", 113900, 4, 1, 37, 23550, 122, 16850 },
        { "GUARD", 121500, 0, 0, 0, 0, 0, 0 },
        { "CTAF HAF", 122800, 0, 1, 37, 30800, 122, 30050 },
    };
    int i, n = (int)(sizeof s / sizeof s[0]);
    unsigned char *p;
    unsigned long t0 = 1748790000ul;   /* 2025-06-01 15:00 UTC */
    route rt;

    if (!m) m = cfg_model_by_code(MODEL_FTA850);
    sim_model = m->code;
    cfg_defaults(sim_img, m);
    /* the 750 family keeps group 1 as FLAG */
    cfg_set_group_name(sim_img, m->group_edit_from, "REID HILLV");
    cfg_set_group_name(sim_img, m->group_edit_from + 1, "TOWERS");
    cfg_set_group_name(sim_img, m->group_edit_from + 2, "GROUND");
    cfg_set_group_name(sim_img, m->group_edit_from + 3, "NAVAIDS");
    for (i = 0; i < n; i++) {
        mbentry e;

        memset(&e, 0, sizeof e);
        e.course = -1;
        snprintf(e.tag, sizeof e.tag, "%s", s[i].tag);
        e.freq = s[i].freq;
        if (!m->has_nav && e.freq < 118000) e.freq = 0;
        e.group = s[i].group ? s[i].group + m->group_edit_from - 1 : 0;
        e.scan = i % 3 == 0;
        e.haspos = s[i].pos && m->has_pos;
        e.ns = 'N'; e.ew = 'W';
        e.lat_deg = s[i].latd; e.lat_mmm = s[i].latm;
        e.lon_deg = s[i].lond; e.lon_mmm = s[i].lonm;
        if (m->has_course && i == 5) e.course = 31000;
        if (!e.freq && !e.haspos) continue;
        cfg_mb_add(sim_img, &e);
    }
    if (m->has_routes) {
        memset(&rt, 0, sizeof rt);
        snprintf(rt.name, sizeof rt.name, "RHV TO PAO");
        rt.group = 1;
        rt.from = 1;
        rt.to = 6;
        rt.nvia = 1;
        rt.via[0] = 8;
        cfg_rt_add(sim_img, &rt);
    }
    cfg_wx_set(sim_img, 0, "SF BAY", 1);
    cfg_wx_set(sim_img, 1, "MONTEREY", 0);

    /* a log: 120 points, a loop over the bay, in the radio's raw format */
    sim_records = 120;
    sim_loglen = 4096;
    sim_log = malloc(sim_loglen);
    if (!sim_log) return;
    memset(sim_log, 0xff, sim_loglen);
    memset(sim_log, 0, 64);
    for (i = 0; i < sim_records; i++) {
        double a = i * 2 * M_PI / sim_records;
        double lat = 37.46 + 0.08 * sin(a), lon = -122.11 + 0.10 * cos(a);
        unsigned long t = t0 + (unsigned long)i * 10;
        float fl;
        unsigned u;
        int k, hdg = (int)fmod(90 - a * 180 / M_PI + 360, 360);
        unsigned char cs = 0;

        p = sim_log + 64 + 20 * i;
        p[0] = (unsigned char)t; p[1] = (unsigned char)(t >> 8); p[2] = (unsigned char)(t >> 16); p[3] = (unsigned char)(t >> 24);
        p[4] = 2;
        fl = (float)lat; memcpy(&u, &fl, 4);
        p[5] = (unsigned char)u; p[6] = (unsigned char)(u >> 8); p[7] = (unsigned char)(u >> 16); p[8] = (unsigned char)(u >> 24);
        fl = (float)lon; memcpy(&u, &fl, 4);
        p[9] = (unsigned char)u; p[10] = (unsigned char)(u >> 8); p[11] = (unsigned char)(u >> 16); p[12] = (unsigned char)(u >> 24);
        p[13] = (unsigned char)(500 + i); p[14] = (unsigned char)((500 + i) >> 8);
        p[15] = 55; p[16] = 0;
        p[17] = (unsigned char)hdg; p[18] = (unsigned char)(hdg >> 8);
        for (k = 0; k < 19; k++) cs ^= p[k];
        p[19] = cs;
    }
    sim_ready = 1;
}

static int sim_poll(radio_state *r)
{
    if (r->op == OP_NONE) return 0;
    sim_ticks++;
    switch (r->op) {
        case OP_READ:
        case OP_WRITE:
            r->progress = sim_ticks * (r->total / 30);
            if (r->progress < r->total) return 1;
            r->progress = r->total;
            if (r->op == OP_READ) {
                memcpy(r->img, sim_img, CFG_SIZE);
                r->img_model = cfg_model(r->img);
                finish(r, 1, "Read from radio complete (simulated)");
            } else {
                if (cfg_model(r->wimg) != sim_model) {
                    finish(r, 0, "The image is for another model: not written");
                    return 1;
                }
                memcpy(sim_img, r->wimg, CFG_SIZE);
                finish(r, 1, "Program to radio complete (simulated)");
            }
            return 1;
        case OP_LOGSTAT:
            if (sim_ticks < 10) return 1;
            r->log_records = sim_records;
            r->log_pct = sim_records * 100 / 6432;
            {
                char m[96];

                snprintf(m, sizeof m, "Logger holds %d records (%d%% of its memory)", r->log_records, r->log_pct);
                finish(r, 1, m);
            }
            return 1;
        case OP_LOGREAD:
            r->total = 40;
            r->progress = sim_ticks;
            if (sim_ticks < 40) return 1;
            if (sim_records < 1) { finish(r, 0, "The logger holds no data"); return 1; }
            free(r->log);
            r->log = malloc(sim_loglen);
            if (r->log) memcpy(r->log, sim_log, sim_loglen);
            r->loglen = r->log ? sim_loglen : 0;
            r->log_records = sim_records;
            finish(r, 1, "Logger download complete (simulated)");
            return 1;
        case OP_LOGERASE:
            if (sim_ticks < 10) return 1;
            sim_records = 0;
            r->log_records = 0;
            r->log_pct = 0;
            finish(r, 1, "Logger erased (simulated)");
            return 1;
        default:
            return 0;
    }
}

/* ---------------------------------------------------------------- public */

void radio_init(radio_state *r, const char *port, int sim)
{
    memset(r, 0, sizeof *r);
    r->sim = sim != 0;
    if (sim) {
        const cfgmodel *m;

        if (!sim_ready) sim_build(sim);
        m = cfg_model_by_code(sim_model);
        r->connected = 1;
        r->mode = RM_CP;
        r->model = sim_model;
        r->img_size = m->size;
        snprintf(r->port, sizeof r->port, "simulator");
        snprintf(r->version, sizeof r->version, "03.01");
        snprintf(r->status, sizeof r->status, "Simulated %s in CP mode", m->name);
        snprintf(r->cphint, sizeof r->cphint, "hold %s while switching it on", m->cpkey);
        return;
    }
    if (port && *port) {
        snprintf(r->port, sizeof r->port, "%s", port);
        r->port_fixed = 1;
    }
    set_status(r, "Looking for the radio");
    try_open(r);
}

int radio_poll(radio_state *r)
{
    long    t = now_ms();
    char    buf[1024];
    int     n;
    char   *nl;
    int     changed = 0, i;

    if (r->sim) return sim_poll(r);

    if (!port_is_open()) {
        if (t >= next_retry) { try_open(r); changed = 1; }
        return changed;
    }

    /* gather input */
    while ((n = port_read(buf, sizeof buf)) > 0) {
        if (rxlen + n >= (int)sizeof rx) rxlen = 0;   /* garbage: drop it */
        memcpy(rx + rxlen, buf, (size_t)n);
        rxlen += n;
        changed = 1;
    }
    if (n < 0) {
        disconnect(r, "Radio unplugged");
        return 1;
    }

    /* looking for the radio: single characters, no line ends */
    if (st == S_DETECT || st == S_DETECT_WAIT) {
        int cp = 0, normal = 0;

        for (i = 0; i < rxlen; i++) {
            if (rx[i] == '@') cp = 1;
            else if (rx[i] == 'P' || rx[i] == '$') normal = 1;
        }
        if (cp) {
            rxlen = 0;
            r->mode = RM_CP;
            set_status(r, "Radio in CP mode, starting the session");
            send_raw("P", 1);
            send_raw("0", 1);
            send_raw("ACMD:002\r\n", 10);
            issue("#CMDSY\r\n", 8, W_CMDOK, S_HS_SYNC1);
            return 1;
        }
        if (normal) {
            rxlen = 0;
            if (r->mode != RM_NORMAL) changed = 1;
            r->mode = RM_NORMAL;
            snprintf(r->status, sizeof r->status, "Radio is on but not in CP mode: %s", r->cphint);
            if (st == S_DETECT_WAIT) { st = S_DETECT; next_retry = t + RETRY_MS; }
        }
        if (st == S_DETECT_WAIT && t > deadline) {
            if (r->mode != RM_NORMAL) {
                r->mode = RM_NONE;
                snprintf(r->status, sizeof r->status, "Port %s open, radio not answering (is it on, in CP mode?)",
                         r->port + (strncmp(r->port, "/dev/", 5) ? 0 : 5));
            }
            st = S_DETECT;
            next_retry = t + RETRY_MS;
            changed = 1;
        }
        if (st == S_DETECT && t >= next_retry) start_detect(r);
        return changed;
    }

    /* complete lines */
    while ((nl = memchr(rx, '\n', (size_t)rxlen)) != NULL) {
        int len = (int)(nl - rx);
        char line[8192];
        char *start;

        if (len >= (int)sizeof line) len = (int)sizeof line - 1;
        memcpy(line, rx, (size_t)len);
        line[len] = 0;
        if (len > 0 && line[len - 1] == '\r') line[len - 1] = 0;
        memmove(rx, nl + 1, (size_t)(rxlen - (int)(nl - rx) - 1));
        rxlen -= (int)(nl - rx) + 1;
        /* a stray character may precede a message */
        start = line;
        while (*start && *start != '#' && *start != '$') start++;
        if (*start) { handle_line(r, start); changed = 1; }
    }
    /* leftovers that cannot start a message */
    if (rxlen > 0 && !memchr(rx, '#', (size_t)rxlen) && !memchr(rx, '$', (size_t)rxlen)) rxlen = 0;

    /* a delayed repeat (radio busy) */
    if (resend_at && t >= resend_at) {
        resend_at = 0;
        send_raw(lastcmd, lastlen);
    }

    /* missed reply */
    if (awaiting != W_NONE && t > deadline) {
        int limit = st == S_KEEP ? KEEP_TRIES : RETRIES;

        if (++tries >= limit) step_failed(r, "no answer");
        else { send_raw(lastcmd, lastlen); deadline = t + REPLY_MS; }
        changed = 1;
    }

    /* idle: make sure the radio is still there */
    if (st == S_READY && t >= next_keep) issue("#CMDSY\r\n", 8, W_CMDOK, S_KEEP);

    return changed;
}

int radio_start(radio_state *r, radio_op op, const unsigned char *img, const int *areas)
{
    if (!r->connected || r->op != OP_NONE) return 0;
    if (!r->sim && st != S_READY && st != S_KEEP) return 0;
    if (op == OP_READ && r->img_size <= 0) {
        snprintf(r->result, sizeof r->result, "The radio's model is not known: it cannot be read");
        return 0;
    }
    if (op == OP_WRITE) {
        const cfgmodel *m;

        if (!img || !areas) return 0;
        m = cfg_model_of(img);
        if (!m) { snprintf(r->result, sizeof r->result, "The image is not for a known model: not written"); return 0; }
        if (r->model && m->code != r->model) {
            const cfgmodel *rm = cfg_model_by_code(r->model);

            snprintf(r->result, sizeof r->result, "The radio is an %s and the image is for an %s: not written",
                     rm ? rm->name : "unknown model", m->name);
            return 0;
        }
        if (!r->sim && m->code == MODEL_FTA850 && atof(r->version) < 2.01) {
            r->finished = 1;
            r->ok = 0;
            snprintf(r->result, sizeof r->result, "Firmware %s is too old to program (2.01 or later is needed)", r->version);
            return 0;
        }
        wr_model = m;
        r->wimg = img;
        r->wareas = areas;
    }
    r->op = op;
    r->finished = 0;
    r->progress = 0;
    r->total = (op == OP_READ || op == OP_WRITE) ? r->img_size : 0;
    r->result[0] = 0;
    if (op == OP_WRITE) {
        const int *a;

        r->total = 0;
        for (a = areas; a[0] >= 0; a += 2) r->total += a[1] - a[0];
    }
    if (r->sim) { sim_ticks = 0; return 1; }
    awaiting = W_NONE;
    switch (op) {
        case OP_READ:     cur = 0; issue_status(S_RD_STAT); break;
        case OP_WRITE:    issue_status(S_WR_STAT_H); break;
        case OP_LOGSTAT:
        case OP_LOGREAD:  issue_status(S_LG_STAT); break;
        case OP_LOGERASE: issue_status(S_LG_ERASE_STAT); break;
        default: r->op = OP_NONE; return 0;
    }
    return 1;
}

void radio_cancel(radio_state *r)
{
    if (r->op == OP_NONE) return;
    finish(r, 0, "Cancelled");
}

void radio_shutdown(radio_state *r)
{
    port_close();
    r->connected = 0;
    free(logbuf);
    logbuf = NULL;
    logcap = loglen = 0;
}
