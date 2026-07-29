/**
 * Chicago-95 64-bit Division/Modulus Helpers
 * GCC runtime helpers for 32-bit targets that lack native 64-bit division
 */

#include <stdint.h>

typedef struct { uint64_t quot; uint64_t rem; } udivmod64_result;

static udivmod64_result udivmod64(uint64_t dividend, uint64_t divisor) {
    udivmod64_result r;
    if (divisor == 0) {
        r.quot = 0;
        r.rem = 0;
        return r;
    }
    if (divisor == 1) {
        r.quot = dividend;
        r.rem = 0;
        return r;
    }
    if (dividend < divisor) {
        r.quot = 0;
        r.rem = dividend;
        return r;
    }
    if (dividend == divisor) {
        r.quot = 1;
        r.rem = 0;
        return r;
    }

    uint64_t quotient = 0;
    uint64_t bit = 1;

    while (!(divisor & (1ULL << 63))) {
        divisor <<= 1;
        bit <<= 1;
    }

    do {
        if (dividend >= divisor) {
            dividend -= divisor;
            quotient |= bit;
        }
        divisor >>= 1;
        bit >>= 1;
    } while (bit);

    r.quot = quotient;
    r.rem = dividend;
    return r;
}

uint64_t __udivdi3(uint64_t dividend, uint64_t divisor) {
    return udivmod64(dividend, divisor).quot;
}

uint64_t __umoddi3(uint64_t dividend, uint64_t divisor) {
    return udivmod64(dividend, divisor).rem;
}

int64_t __divdi3(int64_t dividend, int64_t divisor) {
    int neg = ((dividend < 0) ^ (divisor < 0));
    uint64_t a = dividend < 0 ? -(uint64_t)dividend : (uint64_t)dividend;
    uint64_t b = divisor < 0 ? -(uint64_t)divisor : (uint64_t)divisor;
    uint64_t q = udivmod64(a, b).quot;
    return neg ? -(int64_t)q : (int64_t)q;
}

int64_t __moddi3(int64_t dividend, int64_t divisor) {
    uint64_t a = dividend < 0 ? -(uint64_t)dividend : (uint64_t)dividend;
    uint64_t b = divisor < 0 ? -(uint64_t)divisor : (uint64_t)divisor;
    uint64_t r = udivmod64(a, b).rem;
    return dividend < 0 ? -(int64_t)r : (int64_t)r;
}

typedef struct { uint64_t quotient; uint64_t remainder; } udivmoddi_result;

udivmoddi_result __udivmoddi4(uint64_t dividend, uint64_t divisor, uint64_t *remainder) {
    udivmoddi_result r;
    udivmod64_result tmp = udivmod64(dividend, divisor);
    r.quotient = tmp.quot;
    r.remainder = tmp.rem;
    if (remainder) *remainder = r.remainder;
    return r;
}
