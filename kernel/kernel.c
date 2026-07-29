#include "kernel.h"
#include "gdt.h"
#include "idt.h"
#include "irq.h"
#include "timer.h"
#include "keyboard.h"
#include "console.h"
#include "kmalloc.h"
#include "process.h"
#include "serial.h"
#include "pci.h"
#include "ata.h"
#include "elf.h"
#include "init.h"
#include "syscall.h"
#include "vfs.h"
#include "device.h"
#include "kmsg.h"
#include "drivers/e1000.h"
#include "drivers/net.h"
#include "drivers/xhci.h"
#include "drivers/usb.h"

static void print_banner(void) {
    console_puts("==========================================\n", CONSOLE_LIGHT_CYAN | (CONSOLE_BLACK << 4));
    console_puts("     Chicago-95 Kernel v1.0\n", CONSOLE_WHITE | (CONSOLE_BLACK << 4));
    console_puts("     BrainFS Bootloader Kernel\n", CONSOLE_LIGHT_CYAN | (CONSOLE_BLACK << 4));
    console_puts("==========================================\n", CONSOLE_LIGHT_CYAN | (CONSOLE_BLACK << 4));
    console_putc('\n', CONSOLE_WHITE | (CONSOLE_BLACK << 4));
}

void kernel_main(void) {
    serial_init();
    serial_puts("[BOOT] Serial initialized\n");

    console_init();

    /* Initialize kernel ring buffer early */
    kmsg_init();

    print_banner();

    kmsg_info("Chicago-95 Kernel v1.0 booting");
    kmsg_info("Serial initialized");

    gdt_init();
    kmsg_info("GDT initialized");

    idt_init();
    kmsg_info("IDT initialized (256 entries)");

    irq_init();
    kmsg_info("IRQ initialized");

    timer_init();
    kmsg_info("Timer initialized (100Hz PIT)");

    keyboard_init();
    irq_enable(1);
    kmsg_info("Keyboard initialized (PS/2, extended scancodes)");

    pmm_init();
    uint64_t free = pmm_get_free_pages();
    uint64_t total = pmm_get_total_pages();
    kmsg_info("PMM: %u KB free / %u KB total", (free * 4096) / 1024, (total * 4096) / 1024);

    kmalloc_init();
    kmsg_info("Heap initialized (kmalloc)");

    vfs_init();
    devfs_init();
    procfs_init();
    tmpfs_init();
    kmsg_info("VFS initialized (devfs, procfs, tmpfs)");

    device_init();
    kmsg_info("Device framework initialized");

    pci_init();
    int pci_count = pci_get_device_count();
    kmsg_info("PCI: %d devices found", pci_count);

    ata_init();
    int ata_count = ata_get_detected_count();
    kmsg_info("ATA: %d drive(s) detected", ata_count);

    /* Scan for e1000 NIC */
    int e1000_idx = pci_find_class(0x02, 0x00);
    if (e1000_idx >= 0) {
        pci_device_t *nic = pci_get_device(e1000_idx);
        if (nic && e1000_init(nic->bus, nic->slot, nic->func) == 0) {
            uint8_t mac[6];
            e1000_get_mac(mac);
            kmsg_info("e1000 NIC: MAC %x:%x:%x:%x:%x:%x",
                mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
            net_init();
            uint32_t ip = net_get_ip();
            kmsg_info("Network: IP %d.%d.%d.%d",
                ip & 0xFF, (ip >> 8) & 0xFF, (ip >> 16) & 0xFF, (ip >> 24) & 0xFF);
        } else {
            kmsg_warn("e1000 NIC init failed");
        }
    } else {
        kmsg_info("No Ethernet NIC found");
    }

    /* Scan for xHCI */
    int xhci_idx = pci_find_class(0x0C, 0x03);
    if (xhci_idx < 0) xhci_idx = pci_find_class(0x0C, 0x00);
    if (xhci_idx >= 0) {
        pci_device_t *usb = pci_get_device(xhci_idx);
        if (usb && xhci_init(usb->bus, usb->slot, usb->func) == 0) {
            kmsg_info("xHCI controller at PCI %d:%d (%d ports)", usb->bus, usb->slot, xhci.max_ports);
            usb_init();
            kmsg_info("USB subsystem initialized (%d device(s))", usb_get_device_count());
        } else {
            kmsg_warn("xHCI init failed");
        }
    } else {
        kmsg_info("No xHCI controller found");
    }

    process_init();
    kmsg_info("Process system initialized");

    syscall_init();
    kmsg_info("Syscall interface ready (25 handlers)");

    fcache_init();
    kmsg_info("File cache initialized");

    irq_enable(0);

    console_puts("\n", CONSOLE_WHITE | (CONSOLE_BLACK << 4));
    kmsg_info("System ready, starting init process");

    int init_pid = process_create((void *)init_main, "init", RING0);
    if (init_pid >= 0) {
        kmsg_info("Init process created (PID %d)", init_pid);
    }

    /* Schedule the first process (this never returns) */
    sti();
    process_scheduler();

    /* Should never reach here */
    while (1) {
        hlt();
    }
}
