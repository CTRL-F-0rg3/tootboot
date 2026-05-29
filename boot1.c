/* boot1.c — TootBoot stage 1
 * VGA menu, RTC countdown, loads TanOS kernel from RAM
 */

/* ---------------------------------------------------------------------------
 * Types
 * ---------------------------------------------------------------------------*/
typedef unsigned char  uint8_t;
typedef unsigned short uint16_t;
typedef unsigned int   uint32_t;
typedef unsigned long  uint64_t;

/* ---------------------------------------------------------------------------
 * VGA
 * ---------------------------------------------------------------------------*/
#define VGA_BASE    ((volatile uint16_t *)0xB8000)
#define VGA_W       80
#define VGA_H       25

#define BG_PURPLE   0x5
#define FG_WHITE    0xF
#define FG_GREEN    0xA
#define FG_GRAY     0x7

#define ATTR(bg,fg) ((uint8_t)(((bg)<<4)|(fg)))
#define C_NORM      ATTR(BG_PURPLE, FG_WHITE)
#define C_SEL       ATTR(BG_PURPLE, FG_GREEN)
#define C_BORDER    ATTR(BG_PURPLE, FG_GREEN)
#define C_DIM       ATTR(BG_PURPLE, FG_GRAY)

static void vga_putc(int x, int y, char c, uint8_t a) {
    VGA_BASE[y * VGA_W + x] = (uint16_t)((a << 8) | (uint8_t)c);
}
static void vga_puts(int x, int y, const char *s, uint8_t a) {
    while (*s) vga_putc(x++, y, *s++, a);
}
static void vga_fill(uint8_t a) {
    for (int i = 0; i < VGA_W * VGA_H; i++)
        VGA_BASE[i] = (uint16_t)((a << 8) | ' ');
}
static void vga_hline(int x, int y, int n, char c, uint8_t a) {
    for (int i = 0; i < n; i++) vga_putc(x+i, y, c, a);
}
static int vga_len(const char *s) { int n=0; while(s[n]) n++; return n; }
static void vga_center(int y, const char *s, uint8_t a) {
    vga_puts((VGA_W - vga_len(s)) / 2, y, s, a);
}
static void vga_putint(int x, int y, int n, uint8_t a) {
    char b[12]; int i=10; b[11]=0;
    if (!n) { vga_putc(x,y,'0',a); return; }
    while (n>0&&i>=0) { b[i--]='0'+(n%10); n/=10; }
    vga_puts(x, y, b+i+1, a);
}

/* ---------------------------------------------------------------------------
 * Port I/O
 * ---------------------------------------------------------------------------*/
static inline uint8_t inb(uint16_t p) {
    uint8_t v; __asm__ volatile("inb %1,%0":"=a"(v):"Nd"(p)); return v;
}
static inline void outb(uint16_t p, uint8_t v) {
    __asm__ volatile("outb %0,%1"::"a"(v),"Nd"(p));
}

/* ---------------------------------------------------------------------------
 * CMOS RTC — seconds
 * ---------------------------------------------------------------------------*/
static uint8_t rtc_sec(void) {
    while (({ outb(0x70,0x0A); inb(0x71) & 0x80; }));
    outb(0x70, 0x00);
    uint8_t s = inb(0x71);
    return (s & 0x0F) + ((s >> 4) * 10);
}

/* ---------------------------------------------------------------------------
 * PS/2 keyboard
 * ---------------------------------------------------------------------------*/
#define KEY_UP    0x48
#define KEY_DOWN  0x50
#define KEY_ENTER 0x1C

static uint8_t kb_poll(void) {
    if (inb(0x64) & 1) return inb(0x60);
    return 0;
}

/* ---------------------------------------------------------------------------
 * PIC remap — avoid spurious IRQ exceptions in 64-bit
 * ---------------------------------------------------------------------------*/
static void pic_remap(void) {
    outb(0x20,0x11); outb(0xA0,0x11);
    outb(0x21,0x20); outb(0xA1,0x28);
    outb(0x21,0x04); outb(0xA1,0x02);
    outb(0x21,0x01); outb(0xA1,0x01);
    outb(0x21,0xFF); outb(0xA1,0xFF);
}

/* ---------------------------------------------------------------------------
 * Boot entries
 * ---------------------------------------------------------------------------*/
#define MAX_ENTRIES 8
#define TIMEOUT     5

typedef enum { OS_TANOS=1, OS_LINUX=2 } OsType;

typedef struct {
    char   label[48];
    OsType type;
} BootEntry;

static BootEntry entries[MAX_ENTRIES];
static int       nentries = 0;
static int       sel      = 0;
static int       timeout  = TIMEOUT;

static void add_entry(const char *s, OsType t) {
    if (nentries >= MAX_ENTRIES) return;
    int i=0;
    while (s[i] && i<47) { entries[nentries].label[i]=s[i]; i++; }
    entries[nentries].label[i]=0;
    entries[nentries].type = t;
    nentries++;
}

/* ---------------------------------------------------------------------------
 * Layout constants
 * ---------------------------------------------------------------------------*/
#define BX1 2
#define BX2 77
#define EX  5
#define EY  4

static void draw(void) {
    /* Border */
    vga_putc(BX1,0,'\xDA',C_BORDER);
    vga_hline(BX1+1,0,BX2-BX1-1,'\xC4',C_BORDER);
    vga_putc(BX2,0,'\xBF',C_BORDER);
    for (int r=1;r<=23;r++) {
        vga_putc(BX1,r,'\xB3',C_BORDER);
        vga_putc(BX2,r,'\xB3',C_BORDER);
    }
    vga_putc(BX1,3,'\xC3',C_BORDER);
    vga_hline(BX1+1,3,BX2-BX1-1,'\xC4',C_BORDER);
    vga_putc(BX2,3,'\xB4',C_BORDER);
    vga_putc(BX1,20,'\xC3',C_BORDER);
    vga_hline(BX1+1,20,BX2-BX1-1,'\xC4',C_BORDER);
    vga_putc(BX2,20,'\xB4',C_BORDER);
    vga_putc(BX1,24,'\xC0',C_BORDER);
    vga_hline(BX1+1,24,BX2-BX1-1,'\xC4',C_BORDER);
    vga_putc(BX2,24,'\xD9',C_BORDER);

    /* Title */
    vga_center(1, "TootBoot  v0.1.0", C_BORDER);
    vga_center(2, "Use arrow keys to select  |  ENTER to boot", C_DIM);

    /* Entries */
    for (int i=0; i<16; i++) {
        int row = EY+i;
        vga_hline(BX1+1, row, BX2-BX1-1, ' ', C_NORM);
        if (i >= nentries) continue;
        uint8_t a = (i==sel) ? C_SEL : C_NORM;
        vga_putc(EX-2, row, (i==sel)?'\x10':' ', C_SEL);
        const char *tag = entries[i].type==OS_TANOS ? "[ TanOS ]" : "[ Linux ]";
        vga_puts(EX, row, tag, a);
        vga_puts(EX+10, row, entries[i].label, a);
    }

    /* Hints */
    vga_hline(BX1+1,21,BX2-BX1-1,' ',C_DIM);
    vga_puts(EX,21,"[UP/DOWN] Navigate   [ENTER] Boot",C_DIM);

    /* Timeout row */
    vga_hline(BX1+1,22,BX2-BX1-1,' ',C_DIM);
    if (timeout > 0) {
        vga_puts(EX,22,"Auto-boot in ",C_DIM);
        vga_putint(EX+13,22,timeout,C_SEL);
        vga_puts(EX+14,22,"s",C_DIM);
    } else if (timeout == 0) {
        vga_puts(EX,22,"Booting...",C_SEL);
    } else {
        vga_puts(EX,22,"Timeout cancelled.",C_DIM);
    }
}

/* ---------------------------------------------------------------------------
 * Kernel loader
 * Kernel sits at 0x9800 in RAM (loaded by boot0, sector 14)
 * Copy to 0x200000 and jump to entry from TanOS header
 * ---------------------------------------------------------------------------*/
/* Kernel source: loaded by boot0 at 0x7E00 + (14-1)*512 = 0x9800
 * Page tables are at 0x1000-0x3FFF, stack at 0x90000
 * 0x9800 is safe — below stack, above page tables
 *
 * Kernel destination: 0x100000 (1MB) — safely within the 2MB identity map
 * NOT 0x200000 which is exactly the boundary of our single 2MB page
 */
#define KERNEL_SRC  0x9800UL
#define KERNEL_DST  0x100000UL
#define KERNEL_SIZE (16*512)
#define TANOS_MAGIC 0x544E4F53UL

static void __attribute__((noreturn)) do_boot(void) {
    volatile uint16_t *dbg = VGA_BASE;

    /* Copy kernel from RAM to load address */
    dbg[0] = (C_SEL<<8)|'L';
    uint8_t *src = (uint8_t *)KERNEL_SRC;
    uint8_t *dst = (uint8_t *)KERNEL_DST;
    for (uint32_t i=0; i<KERNEL_SIZE; i++) dst[i] = src[i];

    /* Verify magic */
    uint32_t magic = *(uint32_t *)KERNEL_DST;
    if (magic != TANOS_MAGIC) {
        dbg[0] = (0x4F<<8)|'M';  /* red M = magic fail */
        goto fail;
    }

    /* Jump to entry point */
    dbg[0] = (C_SEL<<8)|'J';
    uint64_t entry = *(uint64_t *)(KERNEL_DST + 16);
    ((void(*)(void*))entry)((void*)0);

fail:
    __asm__ volatile("cli;hlt");
    __builtin_unreachable();
}

/* ---------------------------------------------------------------------------
 * Entry point
 * ---------------------------------------------------------------------------*/
void __attribute__((section(".boot1_entry"))) boot1_entry_point(void) {
    pic_remap();
    vga_fill(C_NORM);

    add_entry("TanOS", OS_TANOS);
    draw();

    /* Drain PS/2 buffer after short stabilization delay */
    for (volatile uint32_t i=0; i<8000000; i++) __asm__("pause");
    while (inb(0x64)&1) inb(0x60);

    uint8_t last_sec = rtc_sec();

    while (1) {
        uint8_t key = kb_poll();
        if (key && !(key & 0x80)) {
            if (key == KEY_UP) {
                if (sel > 0) sel--;
                timeout = -1;
                draw();
            } else if (key == KEY_DOWN) {
                if (sel < nentries-1) sel++;
                timeout = -1;
                draw();
            } else if (key == KEY_ENTER) {
                timeout = 0;
                draw();
                do_boot();
            }
        }

        if (timeout > 0) {
            uint8_t s = rtc_sec();
            if (s != last_sec) {
                last_sec = s;
                timeout--;
                draw();
                if (timeout == 0) do_boot();
            }
        }

        for (volatile int i=0; i<2000; i++) __asm__("pause");
    }
}