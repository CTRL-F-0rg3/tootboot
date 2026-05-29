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
#define ATA_DATA        0x1F0
#define ATA_ERROR       0x1F1
#define ATA_SECTOR_CNT  0x1F2
#define ATA_LBA_LO      0x1F3
#define ATA_LBA_MID     0x1F4
#define ATA_LBA_HI      0x1F5
#define ATA_DRIVE_HEAD  0x1F6
#define ATA_CMD         0x1F7
#define ATA_STATUS      0x1F7
#define ATA_ALT_STATUS  0x3F6   /* alternate status — reading doesn't clear IRQ */
#define ATA_CMD_READ    0x20
#define ATA_CMD_RESET   0x04    /* device control reset */

#define ATA_SR_BSY  0x80
#define ATA_SR_DRQ  0x08
#define ATA_SR_ERR  0x01

static inline uint16_t inw(uint16_t port) {
    uint16_t val;
    __asm__ volatile ("inw %1, %0" : "=a"(val) : "Nd"(port));
    return val;
}

/* 400ns delay — read alt status 4 times */
static void ata_delay400(void) {
    inb(ATA_ALT_STATUS);
    inb(ATA_ALT_STATUS);
    inb(ATA_ALT_STATUS);
    inb(ATA_ALT_STATUS);
}

static int ata_wait_bsy(void) {
    for (uint32_t i = 0; i < 0x100000; i++) {
        if (!(inb(ATA_ALT_STATUS) & ATA_SR_BSY)) return 0;
    }
    return -1;  /* timeout */
}

static int ata_wait_drq(void) {
    for (uint32_t i = 0; i < 0x100000; i++) {
        uint8_t s = inb(ATA_ALT_STATUS);
        if (s & ATA_SR_ERR) return -1;
        if (s & ATA_SR_DRQ) return 0;
    }
    return -1;  /* timeout */
}

static void ata_soft_reset(void) {
    outb(ATA_ALT_STATUS, ATA_CMD_RESET);  /* assert SRST */
    ata_delay400();
    outb(ATA_ALT_STATUS, 0);              /* clear SRST */
    ata_delay400();
    ata_wait_bsy();
}

int disk_read(uint8_t drive, uint64_t lba, uint16_t sectors, void *buf) {
    (void)drive;

    ata_soft_reset();

    /* Select master drive, LBA mode */
    outb(ATA_DRIVE_HEAD, 0xE0 | ((lba >> 24) & 0x0F));
    ata_delay400();

    if (ata_wait_bsy() != 0) return -1;

    outb(ATA_SECTOR_CNT, (uint8_t)sectors);
    outb(ATA_LBA_LO,     (uint8_t)(lba));
    outb(ATA_LBA_MID,    (uint8_t)(lba >> 8));
    outb(ATA_LBA_HI,     (uint8_t)(lba >> 16));
    outb(ATA_CMD,        ATA_CMD_READ);
    ata_delay400();

    uint16_t *ptr = (uint16_t *)buf;
    for (uint16_t s = 0; s < sectors; s++) {
        if (ata_wait_drq() != 0) return -1;
        for (int i = 0; i < 256; i++)
            ptr[i] = inw(ATA_DATA);
        ptr += 256;
        ata_delay400();
    }
    return 0;
}

/* Returns scancode or 0 if no key ready */
static uint8_t kb_poll(void) {
    if (inb(0x64) & 0x01)
        return inb(0x60);
    return 0;
}

/* ---------------------------------------------------------------------------
 * CMOS RTC timing — reads seconds register, works in any emulator
 * ---------------------------------------------------------------------------*/
#define CMOS_ADDR  0x70
#define CMOS_DATA  0x71
#define RTC_STATUS_A  0x0A
#define RTC_SECONDS   0x00

static uint8_t cmos_read(uint8_t reg) {
    outb(CMOS_ADDR, reg);
    return inb(CMOS_DATA);
}

static void rtc_wait_update(void) {
    /* Wait for RTC update-in-progress flag to clear */
    while (cmos_read(RTC_STATUS_A) & 0x80);
}

static uint8_t rtc_seconds(void) {
    rtc_wait_update();
    uint8_t s = cmos_read(RTC_SECONDS);
    /* Convert BCD to binary */
    return (s & 0x0F) + ((s >> 4) * 10);
}

/* PIC: remap IRQs so spurious IRQ0 doesn't cause exceptions in 64-bit */
static void pic_remap(void) {
    /* ICW1 */
    outb(0x20, 0x11);
    outb(0xA0, 0x11);
    /* ICW2: remap IRQ0-7 to 0x20, IRQ8-15 to 0x28 */
    outb(0x21, 0x20);
    outb(0xA1, 0x28);
    /* ICW3 */
    outb(0x21, 0x04);
    outb(0xA1, 0x02);
    /* ICW4 */
    outb(0x21, 0x01);
    outb(0xA1, 0x01);
    /* Mask all interrupts */
    outb(0x21, 0xFF);
    outb(0xA1, 0xFF);
}

/* ---------------------------------------------------------------------------
 * Boot dispatch
 * ---------------------------------------------------------------------------*/
/* Kernel load address — TanOS loads at 2MB */
#define TANOS_LOAD_ADDR   0x200000
/* Sector on disk where TanOS kernel lives (written by disk/justfile) */
#define TANOS_DISK_SECTOR 34
#define TANOS_DISK_SECTS  128   /* 64KB — enough for stub kernel */

static uint8_t boot_drive_num = 0x80;

static void __attribute__((noreturn)) boot_entry(BootEntry *e) {
    volatile uint16_t *vga_dbg = (volatile uint16_t *)0xB8000;

    switch (e->type) {

        case OS_TANOS: {
            /* Step 1: show 'L' — loading */
            vga_dbg[0] = 0x5F4C;

            uint8_t *dst = (uint8_t *)TANOS_LOAD_ADDR;
            disk_read(boot_drive_num, TANOS_DISK_SECTOR, TANOS_DISK_SECTS, dst);

            /* Step 2: show magic bytes for debug */
            uint32_t *magic = (uint32_t *)dst;
            vga_dbg[1] = 0x5F30 | ((*magic >> 24) & 0xFF);

            if (*magic != 0x544E4F53) {
                /* Show 'M' in red — magic mismatch */
                vga_dbg[0] = 0x4F4D;
                goto fail;
            }

            /* Step 3: show 'J' — jumping */
            vga_dbg[0] = 0x5F4A;

            uint64_t entry = *(uint64_t *)(dst + 16);
            void (*kentry)(void *) = (void (*)(void *))entry;
            kentry((void *)0);
            break;
        }

        case OS_LINUX:
        case OS_CUSTOM:
        default:
            vga_dbg[0] = 0x4F3F;  /* '?' red — unknown OS */
            goto fail;
    }

fail:
    vga_dbg[0] = 0x4F46;  /* 'F' red — fail */
    __asm__ volatile ("cli; hlt");
    __builtin_unreachable();
}

/* ---------------------------------------------------------------------------
 * Entry point — called from boot0.asm
 * rdi = TootBoot ABI header ptr
 * rsi = TOOTBOOT_MAGIC
 * rdx = PML4 address
 * ---------------------------------------------------------------------------*/
/* Boot drive passed from boot0 via rdi (TootBoot ABI header has it) */
void __attribute__((section(".boot1_entry"))) boot1_entry_point(void) {
    /* Clear screen with purple background */
    vga_fill(' ', COLOR_NORMAL);

    /* Discover systems — scan MBR partition table */
    detect_systems();
    parse_config();

    /* Always ensure TanOS is listed (sector 34 hardcoded for now) */
    if (entry_count == 0)
        add_entry("TanOS", 0, OS_TANOS);

    redraw();

    pic_remap();

    /* Wait for GTK/QEMU window to stabilize before reading keyboard.
     * Without this, QEMU sends spurious PS/2 bytes on window focus. */
    for (volatile uint32_t d = 0; d < 5000000; d++)
        __asm__ volatile ("pause");

    /* Drain PS/2 buffer after stabilization delay */
    while (inb(0x64) & 0x01) inb(0x60);

    /* Main loop — RTC seconds based countdown */
    uint8_t last_sec = rtc_seconds();

    while (1) {
        /* Poll keyboard — only act on key-press (not release) */
        uint8_t key = kb_poll();

        if (key && !(key & 0x80)) {
            switch (key) {
                case KEY_UP:
                    if (selected > 0) { selected--; redraw(); }
                    timeout = -1;
                    draw_timeout();
                    break;
                case KEY_DOWN:
                    if (selected < entry_count - 1) { selected++; redraw(); }
                    timeout = -1;
                    draw_timeout();
                    break;
                case KEY_ENTER:
                    boot_entry(&entries[selected]);
                    break;
                default:
                    /* Ignore all other scancodes — no timeout cancel */
                    break;
            }
        }

        /* Timeout countdown using RTC seconds */
        if (timeout > 0) {
            uint8_t cur_sec = rtc_seconds();
            if (cur_sec != last_sec) {
                last_sec = cur_sec;
                timeout--;
                draw_timeout();
            }
        } else if (timeout == 0) {
            boot_entry(&entries[0]);
        }

        /* Small busy delay to avoid hammering ports */
        for (volatile int i = 0; i < 1000; i++)
            __asm__ volatile ("pause");
    }
}