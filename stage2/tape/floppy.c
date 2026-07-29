/**
 * Chicago-95 Floppy Disk Driver
 * 1.44MB 3.5" floppy controller (FDC) support
 * Uses DMA channel 2 for data transfers
 */

#include "tape/tape.h"

/* I/O port accessors */
static inline void flp_outb(uint16_t port, uint8_t val) {
    asm volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}
static inline uint8_t flp_inb(uint16_t port) {
    uint8_t val;
    asm volatile("inb %1, %0" : "=a"(val) : "Nd"(port));
    return val;
}

#define FLP_DMA_ADDR    0x1000   /* Physical address for DMA buffer */
#define FLP_SECTOR_SIZE 512
#define FLP_TIMEOUT     1000000

static uint8_t floppy_motor_state[2] = {0, 0};
static int8_t  floppy_cylinder[2]    = {-1, -1};

/* ---- DMA setup ---- */

static void floppy_setup_dma(uint32_t phys_addr, uint32_t len, int write) {
    /* Mask DMA channel 2 */
    flp_outb(0x0A, 0x06);

    /* Clear flip-flop */
    flp_outb(0x0C, 0xFF);

    /* Set address */
    flp_outb(0x04, phys_addr & 0xFF);
    flp_outb(0x04, (phys_addr >> 8) & 0xFF);
    flp_outb(0x81, (phys_addr >> 16) & 0xFF);

    /* Clear flip-flop */
    flp_outb(0x0C, 0xFF);

    /* Set length */
    flp_outb(0x05, (len - 1) & 0xFF);
    flp_outb(0x05, ((len - 1) >> 8) & 0xFF);

    /* Set mode: single transfer, auto-increment, read/write */
    uint8_t mode = write ? 0x48 : 0x46;  /* Write to memory = 0x48, Read = 0x46 */
    flp_outb(0x0B, mode);

    /* Unmask DMA channel 2 */
    flp_outb(0x0A, 0x02);
}

/* ---- FDC I/O ---- */

static void fdc_write(uint8_t val) {
    for (int i = 0; i < FLP_TIMEOUT; i++) {
        if (!(flp_inb(FLOPPY_MSR) & 0x80)) {
            flp_outb(FLOPPY_FIFO, val);
            return;
        }
    }
}

static uint8_t fdc_read(void) {
    for (int i = 0; i < FLP_TIMEOUT; i++) {
        if (flp_inb(FLOPPY_MSR) & 0x80) {
            return flp_inb(FLOPPY_FIFO);
        }
    }
    return 0xFF;
}

static void fdc_wait_irq(void) {
    /* Simple polling wait for IRQ6 */
    for (int i = 0; i < FLP_TIMEOUT; i++) {
        if (flp_inb(FLOPPY_MSR) & 0x80) break;
        /* io_wait */
        flp_outb(0x80, 0);
    }
}

/* ---- Motor control ---- */

void tape_floppy_motor_on(uint8_t drive_num) {
    if (drive_num > 1) return;

    uint8_t motor_bit = (1 << (drive_num + 4));
    flp_outb(FLOPPY_DOR, 0x0C | motor_bit | drive_num);
    floppy_motor_state[drive_num] = 1;

    /* Wait ~500ms for motor spin-up */
    for (volatile int i = 0; i < 500000; i++)
        flp_outb(0x80, 0);
}

void tape_floppy_motor_off(uint8_t drive_num) {
    if (drive_num > 1) return;

    flp_outb(FLOPPY_DOR, 0x0C);
    floppy_motor_state[drive_num] = 0;
}

/* ---- Sense interrupt ---- */

static void fdc_sense_interrupt(void) {
    fdc_write(FLOPPY_CMD_SENSE_INT);
    uint8_t st0 = fdc_read();
    uint8_t cyl = fdc_read();

    /* Update cylinder tracking */
    if (st0 & 0x02) { /* Head 0 selected */
        int drive = st0 & 0x03;
        if (drive < 2) floppy_cylinder[drive] = cyl;
    }
}

/* ---- Seek ---- */

int tape_floppy_seek(uint8_t drive_num, uint8_t cylinder) {
    if (drive_num > 1) return -1;

    if (floppy_cylinder[drive_num] == cylinder) return 0;

    fdc_write(FLOPPY_CMD_SEEK);
    fdc_write(drive_num);
    fdc_write(cylinder);
    fdc_wait_irq();
    fdc_sense_interrupt();

    floppy_cylinder[drive_num] = cylinder;
    return 0;
}

/* ---- Recalibrate ---- */

int tape_floppy_recalibrate(uint8_t drive_num) {
    if (drive_num > 1) return -1;

    tape_floppy_motor_on(drive_num);

    fdc_write(FLOPPY_CMD_RECALIBRATE);
    fdc_write(drive_num);
    fdc_wait_irq();
    fdc_sense_interrupt();

    floppy_cylinder[drive_num] = 0;
    return 0;
}

/* ---- Sector address (CHS -> FIFO byte) ---- */

static uint8_t chs_to_fdc(uint8_t cyl, uint8_t head, uint8_t sector) {
    /* FDC uses sector numbering starting at 1, with special encoding:
     * Sector 1 = 0x01, ..., Sector 18 = 0x12 (for 1.44MB)
     * In the FIFO, the sector is sent as-is (1-based) */
    return sector;
}

/* ---- Read sector ---- */

int tape_floppy_read_sector(uint8_t drive_num, uint8_t cylinder, uint8_t head,
                            uint8_t sector, void *buf) {
    if (drive_num > 1 || sector < 1 || sector > 18) return -1;

    tape_floppy_motor_on(drive_num);

    /* Seek to cylinder */
    if (tape_floppy_seek(drive_num, cylinder) < 0) return -1;

    /* Setup DMA for read (FDC -> memory) */
    floppy_setup_dma(FLP_DMA_ADDR, FLP_SECTOR_SIZE, 0);

    /* Send read command (MT=0, MFM=1, SK=0) */
    fdc_write(0xE6);  /* READ with MT=0, MFM=1 */
    fdc_write((head << 2) | drive_num);
    fdc_write(cylinder);
    fdc_write(head);
    fdc_write(sector);
    fdc_write(2);     /* Sector size: 512 bytes = 2 */
    fdc_write(18);    /* EOT: sectors per track */
    fdc_write(0x1B);  /* Gap 3 length */
    fdc_write(0xFF);  /* DTL: unused */

    fdc_wait_irq();

    /* Read result phase */
    uint8_t st[7];
    st[0] = fdc_read();
    st[1] = fdc_read();
    st[2] = fdc_read();
    st[3] = fdc_read();
    st[4] = fdc_read();
    st[5] = fdc_read();
    st[6] = fdc_read();

    /* Copy from DMA buffer */
    uint8_t *dma_buf = (uint8_t *)FLP_DMA_ADDR;
    uint8_t *dst = (uint8_t *)buf;
    for (uint32_t i = 0; i < FLP_SECTOR_SIZE; i++)
        dst[i] = dma_buf[i];

    /* Check for errors (ST0 bit 6 = abnormal termination) */
    if (st[0] & 0x40) return -1;

    return 0;
}

/* ---- Write sector ---- */

int tape_floppy_write_sector(uint8_t drive_num, uint8_t cylinder, uint8_t head,
                             uint8_t sector, const void *buf) {
    if (drive_num > 1 || sector < 1 || sector > 18) return -1;

    tape_floppy_motor_on(drive_num);

    if (tape_floppy_seek(drive_num, cylinder) < 0) return -1;

    /* Copy data to DMA buffer */
    uint8_t *dma_buf = (uint8_t *)FLP_DMA_ADDR;
    const uint8_t *src = (const uint8_t *)buf;
    for (uint32_t i = 0; i < FLP_SECTOR_SIZE; i++)
        dma_buf[i] = src[i];

    /* Setup DMA for write (memory -> FDC) */
    floppy_setup_dma(FLP_DMA_ADDR, FLP_SECTOR_SIZE, 1);

    /* Send write command */
    fdc_write(0xC5);  /* WRITE with MT=0, MFM=1 */
    fdc_write((head << 2) | drive_num);
    fdc_write(cylinder);
    fdc_write(head);
    fdc_write(sector);
    fdc_write(2);     /* Sector size: 512 */
    fdc_write(18);    /* EOT */
    fdc_write(0x1B);  /* Gap 3 */
    fdc_write(0xFF);  /* DTL */

    fdc_wait_irq();

    uint8_t st[7];
    for (int i = 0; i < 7; i++) st[i] = fdc_read();

    if (st[0] & 0x40) return -1;
    return 0;
}

/* ---- Init ---- */

int tape_floppy_init(uint8_t drive_num) {
    if (drive_num > 1) return -1;

    floppy_cylinder[drive_num] = -1;

    /* Specify command: SRT=0ms, HUT=500ms, HLT=250ms, ND=0 */
    fdc_write(FLOPPY_CMD_SPECIFY);
    fdc_write(0xDF);  /* SRT=13, HUT=6 */
    fdc_write(0x02);  /* HLT=1, ND=0 (DMA mode) */

    /* Reset controller */
    tape_floppy_recalibrate(drive_num);

    return 0;
}
