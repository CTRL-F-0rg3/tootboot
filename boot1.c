/* boot1.c — TootBoot stage 1
 * Displays boot menu, detects systems, launches kernel
 * VGA text mode 80x25 | Purple bg + green-white text
 */

#include "boot1.h"
#include "multiboot.h"

/* ---------------------------------------------------------------------------
 * VGA
 * ---------------------------------------------------------------------------*/
#define VGA_BASE        ((volatile uint16_t *)0xB8000)
#define VGA_WIDTH       80
#define VGA_HEIGHT      25

/* VGA color nibbles */
#define VGA_BG_PURPLE   0x5   /* magenta/purple */
#define VGA_FG_WHITE    0xF
#define VGA_FG_GREEN    0xA   /* bright green */
#define VGA_FG_LGRAY    0x7

#define ATTR(bg, fg)    (uint8_t)(((bg) << 4) | (fg))

#define COLOR_NORMAL    ATTR(VGA_BG_PURPLE, VGA_FG_WHITE)
#define COLOR_SELECTED  ATTR(VGA_BG_PURPLE, VGA_FG_GREEN)
#define COLOR_BORDER    ATTR(VGA_BG_PURPLE, VGA_FG_GREEN)
#define COLOR_TITLE     ATTR(VGA_BG_PURPLE, VGA_FG_GREEN)
#define COLOR_DIM       ATTR(VGA_BG_PURPLE, VGA_FG_LGRAY)

typedef unsigned char  uint8_t;
typedef unsigned short uint16_t;
typedef unsigned int   uint32_t;
typedef unsigned long  uint64_t;

/* ---------------------------------------------------------------------------
 * Boot entry
 * ---------------------------------------------------------------------------*/
#define MAX_ENTRIES     16
#define TIMEOUT_SECS    5

typedef enum {
    OS_TANOS,
    OS_LINUX,
    OS_CUSTOM,
    OS_UNKNOWN
} OsType;

typedef struct {
    char    label[48];
    char    path[128];
    uint64_t load_addr;
    OsType  type;
    int     valid;
} BootEntry;

static BootEntry entries[MAX_ENTRIES];
static int       entry_count  = 0;
static int       selected     = 0;
static int       timeout      = TIMEOUT_SECS;

/* ---------------------------------------------------------------------------
 * VGA helpers
 * ---------------------------------------------------------------------------*/
static void vga_putchar(int x, int y, char c, uint8_t attr) {
    VGA_BASE[y * VGA_WIDTH + x] = (uint16_t)((attr << 8) | (uint8_t)c);
}

static void vga_puts(int x, int y, const char *s, uint8_t attr) {
    while (*s)
        vga_putchar(x++, y, *s++, attr);
}

static void vga_fill(char c, uint8_t attr) {
    for (int i = 0; i < VGA_WIDTH * VGA_HEIGHT; i++)
        VGA_BASE[i] = (uint16_t)((attr << 8) | (uint8_t)c);
}

static void vga_hline(int x, int y, int len, char c, uint8_t attr) {
    for (int i = 0; i < len; i++)
        vga_putchar(x + i, y, c, attr);
}

static int vga_strlen(const char *s) {
    int n = 0;
    while (s[n]) n++;
    return n;
}

static void vga_center(int y, const char *s, uint8_t attr) {
    int x = (VGA_WIDTH - vga_strlen(s)) / 2;
    if (x < 0) x = 0;
    vga_puts(x, y, s, attr);
}

/* Simple int-to-string for timeout counter */
static void vga_putint(int x, int y, int n, uint8_t attr) {
    char buf[12];
    int i = 10;
    buf[11] = '\0';
    if (n == 0) { vga_putchar(x, y, '0', attr); return; }
    while (n > 0 && i >= 0) {
        buf[i--] = '0' + (n % 10);
        n /= 10;
    }
    vga_puts(x, y, buf + i + 1, attr);
}

/* ---------------------------------------------------------------------------
 * Border & layout
 *
 *  Row 0     : top border
 *  Row 1     : "TootBoot" title
 *  Row 2     : subtitle
 *  Row 3     : separator
 *  Row 4..19 : entry list (max 16)
 *  Row 20    : separator
 *  Row 21    : key hints
 *  Row 22    : timeout bar
 *  Row 23    : separator
 *  Row 24    : bottom border
 * ---------------------------------------------------------------------------*/
#define BORDER_X1   2
#define BORDER_X2   77
#define ENTRY_X     5
#define ENTRY_Y     4
#define ENTRY_H     16

/* Unicode-ish box chars in CP437 */
#define CHAR_TL     '\xDA'   /* ┌ */
#define CHAR_TR     '\xBF'   /* ┐ */
#define CHAR_BL     '\xC0'   /* └ */
#define CHAR_BR     '\xD9'   /* ┘ */
#define CHAR_H      '\xC4'   /* ─ */
#define CHAR_V      '\xB3'   /* │ */
#define CHAR_LT     '\xC3'   /* ├ */
#define CHAR_RT     '\xB4'   /* ┤ */
#define CHAR_ARROW  '\x10'   /* ► */

static void draw_border(void) {
    /* top */
    vga_putchar(BORDER_X1,     0, CHAR_TL, COLOR_BORDER);
    vga_hline  (BORDER_X1 + 1, 0, BORDER_X2 - BORDER_X1 - 1, CHAR_H, COLOR_BORDER);
    vga_putchar(BORDER_X2,     0, CHAR_TR, COLOR_BORDER);

    /* sides */
    for (int r = 1; r <= 23; r++) {
        vga_putchar(BORDER_X1, r, CHAR_V, COLOR_BORDER);
        vga_putchar(BORDER_X2, r, CHAR_V, COLOR_BORDER);
    }

    /* separator after title */
    vga_putchar(BORDER_X1,     3, CHAR_LT, COLOR_BORDER);
    vga_hline  (BORDER_X1 + 1, 3, BORDER_X2 - BORDER_X1 - 1, CHAR_H, COLOR_BORDER);
    vga_putchar(BORDER_X2,     3, CHAR_RT, COLOR_BORDER);

    /* separator before hints */
    vga_putchar(BORDER_X1,     20, CHAR_LT, COLOR_BORDER);
    vga_hline  (BORDER_X1 + 1, 20, BORDER_X2 - BORDER_X1 - 1, CHAR_H, COLOR_BORDER);
    vga_putchar(BORDER_X2,     20, CHAR_RT, COLOR_BORDER);

    /* bottom */
    vga_putchar(BORDER_X1,     24, CHAR_BL, COLOR_BORDER);
    vga_hline  (BORDER_X1 + 1, 24, BORDER_X2 - BORDER_X1 - 1, CHAR_H, COLOR_BORDER);
    vga_putchar(BORDER_X2,     24, CHAR_BR, COLOR_BORDER);
}

static void draw_title(void) {
    vga_center(1, "TootBoot  v0.1.0", COLOR_TITLE);
    vga_center(2, "Use arrow keys to select  |  ENTER to boot", COLOR_DIM);
}

static void draw_hints(void) {
    vga_puts(ENTRY_X, 21, "[UP/DOWN] Navigate   [ENTER] Boot   [E] Edit   [C] Command line", COLOR_DIM);
}

static void draw_timeout(void) {
    vga_hline(BORDER_X1 + 1, 22, BORDER_X2 - BORDER_X1 - 1, ' ', COLOR_DIM);

    if (timeout > 0) {
        vga_puts(ENTRY_X, 22, "Auto-boot in ", COLOR_DIM);
        vga_putint(ENTRY_X + 13, 22, timeout, COLOR_SELECTED);
        vga_puts(ENTRY_X + 14, 22, "s  (any key to cancel)", COLOR_DIM);
    } else if (timeout == 0) {
        vga_puts(ENTRY_X, 22, "Booting...", COLOR_SELECTED);
    } else {
        vga_puts(ENTRY_X, 22, "Timeout cancelled.", COLOR_DIM);
    }
}

static void draw_entries(void) {
    for (int i = 0; i < ENTRY_H; i++) {
        int row = ENTRY_Y + i;
        /* clear row inside border */
        vga_hline(BORDER_X1 + 1, row, BORDER_X2 - BORDER_X1 - 1, ' ', COLOR_NORMAL);

        if (i >= entry_count) continue;

        uint8_t attr = (i == selected) ? COLOR_SELECTED : COLOR_NORMAL;

        if (i == selected)
            vga_putchar(ENTRY_X - 2, row, CHAR_ARROW, COLOR_SELECTED);
        else
            vga_putchar(ENTRY_X - 2, row, ' ', COLOR_NORMAL);

        /* OS type tag */
        const char *tag = "[ ??? ]";
        switch (entries[i].type) {
            case OS_TANOS:  tag = "[ TanOS ]"; break;
            case OS_LINUX:  tag = "[ Linux ]"; break;
            case OS_CUSTOM: tag = "[Custom ]"; break;
            default:        break;
        }
        vga_puts(ENTRY_X, row, tag, attr);
        vga_puts(ENTRY_X + 10, row, entries[i].label, attr);
    }
}

static void redraw(void) {
    draw_border();
    draw_title();
    draw_entries();
    draw_hints();
    draw_timeout();
}

/* ---------------------------------------------------------------------------
 * System detection
 * Scans partition table (MBR) for known signatures.
 * Each found OS is added to entries[].
 * ---------------------------------------------------------------------------*/

/* MBR partition entry */
typedef struct __attribute__((packed)) {
    uint8_t  status;
    uint8_t  chs_first[3];
    uint8_t  type;
    uint8_t  chs_last[3];
    uint32_t lba_start;
    uint32_t lba_size;
} MbrPartition;

/* Well-known partition type bytes */
#define PART_LINUX_DATA  0x83
#define PART_LINUX_SWAP  0x82
#define PART_FAT32       0x0B
#define PART_FAT32_LBA   0x0C
#define PART_NTFS        0x07
#define PART_TANOS       0x74   /* 't' — custom TanOS type */
#define PART_REDOX       0x72   /* 'r' — custom Redox type  */

static void add_entry(const char *label, uint64_t load_addr, OsType type) {
    if (entry_count >= MAX_ENTRIES) return;
    BootEntry *e = &entries[entry_count++];

    /* copy label */
    int i = 0;
    while (label[i] && i < 47) { e->label[i] = label[i]; i++; }
    e->label[i] = '\0';

    e->load_addr = load_addr;
    e->type      = type;
    e->valid     = 1;
}

static void detect_systems(void) {
    /* MBR lives at physical 0x7C00, partition table at offset 446 */
    uint8_t *mbr = (uint8_t *)0x7C00;

    /* Verify MBR signature */
    if (mbr[510] != 0x55 || mbr[511] != 0xAA) return;

    MbrPartition *parts = (MbrPartition *)(mbr + 446);

    for (int i = 0; i < 4; i++) {
        if (parts[i].lba_size == 0) continue;

        switch (parts[i].type) {
            case PART_TANOS:
                add_entry("TanOS", (uint64_t)parts[i].lba_start * 512, OS_TANOS);
                break;
            case PART_LINUX_DATA:
                add_entry("Linux", (uint64_t)parts[i].lba_start * 512, OS_LINUX);
                break;
            case PART_REDOX:
                add_entry("Redox OS", (uint64_t)parts[i].lba_start * 512, OS_CUSTOM);
                break;
            default:
                break;
        }
    }
}

/* ---------------------------------------------------------------------------
 * Config file entries (manual additions)
 * Format parsed from a flat config loaded by Boot0 into memory at 0x8000:
 *   ENTRY TanOS /boot/tanos.bin
 *   ENTRY Linux /boot/vmlinuz
 * ---------------------------------------------------------------------------*/
#define CONFIG_ADDRESS  ((char *)0x8000)
#define CONFIG_MAX      4096

static int str_startswith(const char *s, const char *prefix) {
    while (*prefix)
        if (*s++ != *prefix++) return 0;
    return 1;
}

static void skip_spaces(const char **p) {
    while (**p == ' ' || **p == '\t') (*p)++;
}

static int read_token(const char **p, char *out, int max) {
    int n = 0;
    while (**p && **p != ' ' && **p != '\t' && **p != '\n' && n < max - 1)
        out[n++] = *(*p)++;
    out[n] = '\0';
    return n;
}

static void parse_config(void) {
    const char *p = CONFIG_ADDRESS;
    const char *end = p + CONFIG_MAX;

    while (p < end && *p) {
        /* skip blank lines and comments */
        if (*p == '#' || *p == '\n' || *p == '\r') {
            while (*p && *p != '\n') p++;
            p++;
            continue;
        }

        if (str_startswith(p, "ENTRY")) {
            p += 5;
            skip_spaces(&p);

            char label[48], path[128];
            read_token(&p, label, 48);
            skip_spaces(&p);
            read_token(&p, path, 128);

            /* Determine type from label */
            OsType t = OS_CUSTOM;
            if (label[0]=='T' && label[1]=='a') t = OS_TANOS;
            else if (label[0]=='L' && label[1]=='i') t = OS_LINUX;

            if (entry_count < MAX_ENTRIES) {
                add_entry(label, 0, t);
                /* store path too */
                int i = 0;
                while (path[i] && i < 127) {
                    entries[entry_count - 1].path[i] = path[i];
                    i++;
                }
                entries[entry_count - 1].path[i] = '\0';
            }
        }

        while (p < end && *p && *p != '\n') p++;
        if (*p == '\n') p++;
    }
}

/* ---------------------------------------------------------------------------
 * Keyboard (PS/2 via port 0x60)
 * ---------------------------------------------------------------------------*/
#define KEY_UP    0x48
#define KEY_DOWN  0x50
#define KEY_ENTER 0x1C
#define KEY_E     0x12
#define KEY_C     0x2E

static inline uint8_t inb(uint16_t port) {
    uint8_t val;
    __asm__ volatile ("inb %1, %0" : "=a"(val) : "Nd"(port));
    return val;
}

static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile ("outb %0, %1" :: "a"(val), "Nd"(port));
}

/* ---------------------------------------------------------------------------
 * ATA PIO disk read — works natively in 64-bit, no mode switching needed
 * Reads 'sectors' sectors from LBA into buf using primary ATA channel
 * ---------------------------------------------------------------------------*/
/* Kernel is pre-loaded by boot0 into RAM at 0x9000
 * (boot0 loads 64 sectors = 32KB starting at sector 1)
 * boot1.bin is ~6KB, so kernel is at offset ~6KB = 0x9800 approx
 * We copy it to TANOS_LOAD_ADDR */
/* Kernel pre-loaded by boot0 into RAM.
 * boot0 loads 64 sectors (32KB) from sector 1 into 0x7E00.
 * Layout: [boot1 ~6KB][padding][kernel at sector 34 offset]
 * Sector 34 relative to sector 1 = offset (34-1)*512 = 16896 bytes from 0x7E00
 * So kernel is at 0x7E00 + 16896 = 0xC280... but we just copy from disk image offset.
 * Simpler: kernel is at fixed RAM address 0x7E00 + (TANOS_DISK_SECTOR-1)*512
 */
int disk_read(uint8_t drive, uint64_t lba, uint16_t sectors, void *buf) {
    (void)drive;
    uint32_t offset = (uint32_t)(lba - 1) * 512;
    uint8_t *src = (uint8_t *)((uint64_t)0x7E00 + offset);
    uint8_t *dst = (uint8_t *)buf;
    uint32_t bytes = (uint32_t)sectors * 512;
    for (uint32_t i = 0; i < bytes; i++)
        dst[i] = src[i];
    return 0;
}