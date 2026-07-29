#ifndef KERNEL_H
#define KERNEL_H

#include <stdint.h>
#include <stddef.h>

#define ALWAYS_INLINE __attribute__((always_inline)) static inline

ALWAYS_INLINE void outb(uint16_t port, uint8_t val) {
    __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}

ALWAYS_INLINE uint8_t inb(uint16_t port) {
    uint8_t ret;
    __asm__ volatile ("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

ALWAYS_INLINE void outw(uint16_t port, uint16_t val) {
    __asm__ volatile ("outw %0, %1" : : "a"(val), "Nd"(port));
}

ALWAYS_INLINE uint16_t inw(uint16_t port) {
    uint16_t ret;
    __asm__ volatile ("inw %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

ALWAYS_INLINE void outl(uint16_t port, uint32_t val) {
    __asm__ volatile ("outl %0, %1" : : "a"(val), "Nd"(port));
}

ALWAYS_INLINE uint32_t inl(uint16_t port) {
    uint32_t ret;
    __asm__ volatile ("inl %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

ALWAYS_INLINE void io_wait(void) { outb(0x80, 0); }
ALWAYS_INLINE void cli(void) { __asm__ volatile ("cli"); }
ALWAYS_INLINE void sti(void) { __asm__ volatile ("sti"); }
ALWAYS_INLINE void hlt(void) { __asm__ volatile ("hlt"); }
ALWAYS_INLINE void cli_hlt(void) { __asm__ volatile ("cli\nhlt"); }

ALWAYS_INLINE void invlpg(void *addr) {
    __asm__ volatile ("invlpg (%0)" : : "r"(addr) : "memory");
}

void *memset(void *dst, int val, size_t n);
void *memcpy(void *dst, const void *src, size_t n);
int memcmp(const void *a, const void *b, size_t n);
size_t strlen(const char *s);
int strcmp(const char *a, const char *b);
int strncmp(const char *a, const char *b, size_t n);
void strcpy(char *dst, const char *src);

#endif
