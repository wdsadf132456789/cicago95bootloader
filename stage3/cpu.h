#ifndef STAGE3_CPU_H
#define STAGE3_CPU_H

#include <stdint.h>

#define CPU_VENDOR_LEN 13
#define CPU_BRAND_LEN  49

typedef struct {
    char     vendor[CPU_VENDOR_LEN];
    char     brand[CPU_BRAND_LEN];
    uint32_t max_leaf;
    uint32_t max_ext_leaf;
    uint32_t stepping;
    uint32_t model;
    uint32_t family;
    uint32_t type;
    uint32_t ext_model;
    uint32_t ext_family;
    uint32_t features[4];
    uint32_t cache_l1d;
    uint32_t cache_l1i;
    uint32_t cache_l2;
    uint32_t cache_l3;
    uint32_t cores;
    uint32_t threads;
} cpu_info_t;

#define FEAT_SSE     (1<<0)
#define FEAT_SSE2    (1<<1)
#define FEAT_SSE3    (1<<2)
#define FEAT_SSSE3   (1<<3)
#define FEAT_SSE41   (1<<4)
#define FEAT_SSE42   (1<<5)
#define FEAT_AVX     (1<<6)
#define FEAT_AVX2    (1<<7)
#define FEAT_AES_NI  (1<<8)
#define FEAT_RDRAND  (1<<9)
#define FEAT_RDSEED  (1<<10)
#define FEAT_SHA     (1<<11)
#define FEAT_BMI1    (1<<12)
#define FEAT_BMI2    (1<<13)
#define FEAT_ADX     (1<<14)
#define FEAT_FMA     (1<<15)
#define FEAT_VMX     (1<<16)
#define FEAT_SVM     (1<<17)
#define FEAT_PCLMUL  (1<<18)
#define FEAT_FSGSBASE (1<<19)
#define FEAT_SMEP    (1<<20)
#define FEAT_SMAP    (1<<21)
#define FEAT_UMIP    (1<<22)
#define FEAT_XSAVE   (1<<23)
#define FEAT_MMX     (1<<24)
#define FEAT_TSC     (1<<25)
#define FEAT_MSR     (1<<26)
#define FEAT_APIC    (1<<27)
#define FEAT_PAE     (1<<28)
#define FEAT_PSE36   (1<<29)
#define FEAT_PGE     (1<<30)
#define FEAT_PSE     (1u<<31)

void cpu_detect(cpu_info_t *info);
void cpu_print(const cpu_info_t *info);
const char *cpu_feature_name(uint32_t bit);

#endif