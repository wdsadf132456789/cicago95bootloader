#include <stdint.h>
#include "console.h"
#include "cpu.h"

static inline void cpuid(uint32_t leaf, uint32_t sub,
                         uint32_t *a, uint32_t *b, uint32_t *c, uint32_t *d) {
    __asm__ volatile("cpuid"
        : "=a"(*a), "=b"(*b), "=c"(*c), "=d"(*d)
        : "a"(leaf), "c"(sub));
}

void cpu_detect(cpu_info_t *info) {
    uint32_t a, b, c, d;

    cpuid(0, 0, &a, &b, &c, &d);
    info->max_leaf = a;
    *(uint32_t *)(info->vendor + 0) = b;
    *(uint32_t *)(info->vendor + 4) = d;
    *(uint32_t *)(info->vendor + 8) = c;
    info->vendor[12] = 0;

    if (a >= 1) {
        cpuid(1, 0, &a, &b, &c, &d);
        info->stepping  = a & 0xF;
        info->model     = (a >> 4) & 0xF;
        info->family    = (a >> 8) & 0xF;
        info->type      = (a >> 12) & 0x3;
        info->ext_model = (a >> 16) & 0xF;
        info->ext_family = (a >> 20) & 0xFF;
        info->features[0] = d; /* ECX */
        info->features[1] = c; /* EDX */
        info->features[2] = b; /* EBX */
    }

    info->cores = 1;
    info->threads = 1;
    if (a >= 4) {
        cpuid(4, 0, &a, &b, &c, &d);
        info->cores = ((a >> 26) & 0x3F) + 1;
        info->threads = ((a >> 14) & 0x3F) + 1;
    }

    if (info->max_leaf >= 0x80000000) {
        cpuid(0x80000000, 0, &a, &b, &c, &d);
        info->max_ext_leaf = a;
        if (a >= 0x80000004) {
            cpuid(0x80000002, 0, &a, &b, &c, &d);
            *(uint32_t *)(info->brand + 0) = a;
            *(uint32_t *)(info->brand + 4) = b;
            *(uint32_t *)(info->brand + 8) = c;
            *(uint32_t *)(info->brand + 12) = d;
            cpuid(0x80000003, 0, &a, &b, &c, &d);
            *(uint32_t *)(info->brand + 16) = a;
            *(uint32_t *)(info->brand + 20) = b;
            *(uint32_t *)(info->brand + 24) = c;
            *(uint32_t *)(info->brand + 28) = d;
            cpuid(0x80000004, 0, &a, &b, &c, &d);
            *(uint32_t *)(info->brand + 32) = a;
            *(uint32_t *)(info->brand + 36) = b;
            *(uint32_t *)(info->brand + 40) = c;
            *(uint32_t *)(info->brand + 44) = d;
            info->brand[48] = 0;
        } else {
            info->brand[0] = 0;
        }
    } else {
        info->max_ext_leaf = 0;
        info->brand[0] = 0;
    }

    if (info->features[1] & (1 << 28)) info->features[0] |= FEAT_MMX;
    if (info->features[1] & (1 << 25)) info->features[0] |= FEAT_SSE;
    if (info->features[1] & (1 << 26)) info->features[0] |= FEAT_SSE2;
    if (info->features[1] & (1 << 0))  info->features[0] |= FEAT_SSE3;
    if (info->features[1] & (1 << 9))  info->features[0] |= FEAT_SSSE3;
    if (info->features[1] & (1 << 19)) info->features[0] |= FEAT_SSE41;
    if (info->features[1] & (1 << 20)) info->features[0] |= FEAT_SSE42;
    if (info->features[0] & (1 << 5))  info->features[0] |= FEAT_AVX;
    if (info->features[0] & (1 << 25)) info->features[0] |= FEAT_AES_NI;
    if (info->features[0] & (1 << 30)) info->features[0] |= FEAT_RDRAND;
    if (info->features[0] & (1 << 18)) info->features[0] |= FEAT_PCLMUL;
    if (info->features[0] & (1 << 0))  info->features[0] |= FEAT_FSGSBASE;
    if (info->features[2] & (1 << 5))  info->features[0] |= FEAT_AVX2;
    if (info->features[2] & (1 << 3))  info->features[0] |= FEAT_BMI1;
    if (info->features[2] & (1 << 8))  info->features[0] |= FEAT_BMI2;
    if (info->features[2] & (1 << 19)) info->features[0] |= FEAT_ADX;
    if (info->features[1] & (1 << 12)) info->features[0] |= FEAT_FMA;
    if (info->features[1] & (1 << 5))  info->features[0] |= FEAT_VMX;
    if (info->features[0] & (1 << 26)) info->features[0] |= FEAT_XSAVE;
    if (info->features[1] & (1 << 23)) info->features[0] |= FEAT_MMX;
    if (info->features[1] & (1 << 4))  info->features[0] |= FEAT_TSC;
    if (info->features[1] & (1 << 5))  info->features[0] |= FEAT_MSR;
    if (info->features[1] & (1 << 9))  info->features[0] |= FEAT_APIC;
    if (info->features[1] & (1 << 6))  info->features[0] |= FEAT_PAE;
    if (info->features[1] & (1 << 17)) info->features[0] |= FEAT_PSE36;
    if (info->features[1] & (1 << 13)) info->features[0] |= FEAT_PGE;
    if (info->features[1] & (1 << 3))  info->features[0] |= FEAT_PSE;
    if (info->features[0] & (1 << 29)) info->features[0] |= FEAT_SHA;
    if (info->features[0] & (1 << 7))  info->features[0] |= FEAT_SMEP;
    if (info->features[0] & (1 << 20)) info->features[0] |= FEAT_SMAP;
    if (info->features[0] & (1 << 2))  info->features[0] |= FEAT_UMIP;
}

const char *cpu_feature_name(uint32_t bit) {
    switch (bit) {
        case FEAT_SSE:     return "SSE";
        case FEAT_SSE2:    return "SSE2";
        case FEAT_SSE3:    return "SSE3";
        case FEAT_SSSE3:   return "SSSE3";
        case FEAT_SSE41:   return "SSE4.1";
        case FEAT_SSE42:   return "SSE4.2";
        case FEAT_AVX:     return "AVX";
        case FEAT_AVX2:    return "AVX2";
        case FEAT_AES_NI:  return "AES-NI";
        case FEAT_RDRAND:  return "RDRAND";
        case FEAT_RDSEED:  return "RDSEED";
        case FEAT_SHA:     return "SHA";
        case FEAT_BMI1:    return "BMI1";
        case FEAT_BMI2:    return "BMI2";
        case FEAT_ADX:     return "ADX";
        case FEAT_FMA:     return "FMA";
        case FEAT_VMX:     return "VMX";
        case FEAT_SVM:     return "SVM";
        case FEAT_PCLMUL:  return "PCLMUL";
        case FEAT_FSGSBASE:return "FSGSBASE";
        case FEAT_SMEP:    return "SMEP";
        case FEAT_SMAP:    return "SMAP";
        case FEAT_UMIP:    return "UMIP";
        case FEAT_XSAVE:   return "XSAVE";
        case FEAT_MMX:     return "MMX";
        case FEAT_TSC:     return "TSC";
        case FEAT_MSR:     return "MSR";
        case FEAT_APIC:    return "APIC";
        case FEAT_PAE:     return "PAE";
        case FEAT_PGE:     return "PGE";
        case FEAT_PSE:     return "PSE";
        case FEAT_PSE36:   return "PSE36";
        default:           return "?";
    }
}

void cpu_print(const cpu_info_t *info) {
    cons_color("  CPU: ", COL_LABEL);
    cons_color(info->vendor, COL_OK);
    cons_color(" / ", COL_DEFAULT);

    int has_brand = 0;
    for (int i = 0; info->brand[i]; i++)
        if (info->brand[i] > ' ') has_brand = 1;
    if (has_brand) {
        cons_color(info->brand, COL_OK);
    } else {
        cons_color("Family ", COL_OK);
        cons_dec32(info->family);
        cons_color(" Model ", COL_OK);
        cons_dec32(info->model);
    }
    cons_puts("\n");

    cons_color("  Cores: ", COL_LABEL);
    cons_dec32(info->cores);
    cons_color(" / ", COL_DEFAULT);
    cons_dec32(info->threads);
    cons_color(" threads\n", COL_DEFAULT);

    cons_color("  Features: ", COL_LABEL);
    static const uint32_t interesting[] = {
        FEAT_TSC, FEAT_SSE, FEAT_SSE2, FEAT_SSE3, FEAT_SSSE3,
        FEAT_SSE41, FEAT_SSE42, FEAT_AVX, FEAT_AVX2,
        FEAT_AES_NI, FEAT_SHA, FEAT_RDRAND, FEAT_RDSEED,
        FEAT_FMA, FEAT_BMI1, FEAT_BMI2, FEAT_ADX,
        FEAT_VMX, FEAT_PCLMUL, FEAT_XSAVE, FEAT_FSGSBASE,
        FEAT_SMEP, FEAT_SMAP
    };
    int first = 1;
    for (int i = 0; i < 23; i++) {
        if (info->features[0] & interesting[i]) {
            if (!first) { cons_color(", ", COL_DEFAULT); }
            first = 0;
            const char *name = cpu_feature_name(interesting[i]);
            cons_color(name, COL_OK);
        }
    }
    cons_puts("\n");
}
