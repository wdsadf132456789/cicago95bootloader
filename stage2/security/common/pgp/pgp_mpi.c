#include <stdint.h>
#include <string.h>
#include "security/pgp.h"
#include "boot/security.h"

#define MPI_TEMP (2 * PGP_MPI_MAX_LIMBS)

void pgp_mpi_zero(pgp_mpi_t *a) {
    for (uint32_t i = 0; i < PGP_MPI_MAX_LIMBS; i++) a->limbs[i] = 0;
    a->count = 0;
}

int pgp_mpi_is_zero(const pgp_mpi_t *a) {
    for (uint32_t i = 0; i < a->count; i++) if (a->limbs[i]) return 0;
    return 1;
}

int pgp_mpi_is_one(const pgp_mpi_t *a) {
    if (a->count == 0) return 0;
    if (a->limbs[0] != 1) return 0;
    for (uint32_t i = 1; i < a->count; i++) if (a->limbs[i]) return 0;
    return 1;
}

void pgp_mpi_set_u32(pgp_mpi_t *a, uint32_t v) {
    pgp_mpi_zero(a);
    a->limbs[0] = v;
    a->count = (v ? 1 : 0);
}

int pgp_mpi_compare(const pgp_mpi_t *a, const pgp_mpi_t *b) {
    uint32_t i;
    if (a->count > b->count) {
        for (i = a->count - 1; i >= b->count; i--) if (a->limbs[i]) return 1;
    } else if (b->count > a->count) {
        for (i = b->count - 1; i >= a->count; i--) if (b->limbs[i]) return -1;
    }
    i = (a->count > b->count) ? a->count : b->count;
    while (i > 0) {
        i--;
        if (a->limbs[i] != b->limbs[i])
            return (a->limbs[i] > b->limbs[i]) ? 1 : -1;
    }
    return 0;
}

static void mpi_normalize(pgp_mpi_t *a) {
    while (a->count > 0 && a->limbs[a->count - 1] == 0) a->count--;
}

void pgp_mpi_add(pgp_mpi_t *r, const pgp_mpi_t *a, const pgp_mpi_t *b) {
    uint64_t carry = 0;
    uint32_t max = (a->count > b->count) ? a->count : b->count;
    if (max >= PGP_MPI_MAX_LIMBS) max = PGP_MPI_MAX_LIMBS - 1;
    for (uint32_t i = 0; i <= max; i++) {
        uint64_t sum = carry;
        if (i < a->count) sum += a->limbs[i];
        if (i < b->count) sum += b->limbs[i];
        r->limbs[i] = (uint32_t)(sum & 0xFFFFFFFF);
        carry = sum >> 32;
    }
    if (carry && max + 1 < PGP_MPI_MAX_LIMBS) {
        r->limbs[max + 1] = (uint32_t)carry;
        r->count = max + 2;
    } else {
        r->count = max + 1;
    }
    mpi_normalize(r);
}

void pgp_mpi_sub(pgp_mpi_t *r, const pgp_mpi_t *a, const pgp_mpi_t *b) {
    uint64_t borrow = 0;
    uint32_t max = (a->count > b->count) ? a->count : b->count;
    if (max >= PGP_MPI_MAX_LIMBS) max = PGP_MPI_MAX_LIMBS - 1;
    for (uint32_t i = 0; i < max; i++) {
        uint64_t diff = (uint64_t)(i < a->count ? a->limbs[i] : 0) - (i < b->count ? b->limbs[i] : 0) - borrow;
        if ((int64_t)diff < 0) { diff += 0x100000000ULL; borrow = 1; }
        else borrow = 0;
        r->limbs[i] = (uint32_t)(diff & 0xFFFFFFFF);
    }
    r->count = max;
    mpi_normalize(r);
}

void pgp_mpi_mul(pgp_mpi_t *r, const pgp_mpi_t *a, const pgp_mpi_t *b) {
    uint64_t tmp[MPI_TEMP];
    for (uint32_t i = 0; i < MPI_TEMP; i++) tmp[i] = 0;
    for (uint32_t i = 0; i < a->count; i++) {
        uint64_t carry = 0;
        for (uint32_t j = 0; j < b->count && (i + j) < MPI_TEMP; j++) {
            tmp[i + j] += (uint64_t)a->limbs[i] * b->limbs[j] + carry;
            carry = tmp[i + j] >> 32;
            tmp[i + j] &= 0xFFFFFFFF;
        }
        if (carry && (i + b->count) < MPI_TEMP)
            tmp[i + b->count] = carry;
    }
    uint32_t max = a->count + b->count;
    if (max > PGP_MPI_MAX_LIMBS) max = PGP_MPI_MAX_LIMBS;
    for (uint32_t i = 0; i < max; i++) r->limbs[i] = (uint32_t)(tmp[i] & 0xFFFFFFFF);
    r->count = max;
    mpi_normalize(r);
}

void pgp_mpi_div(pgp_mpi_t *q, pgp_mpi_t *rem, const pgp_mpi_t *a, const pgp_mpi_t *b) {
    pgp_mpi_t tmp_a, tmp_b, tmp_q, qbit, shifted;
    if (pgp_mpi_is_zero(b)) { if (q) pgp_mpi_zero(q); if (rem) pgp_mpi_zero(rem); return; }
    pgp_mpi_zero(&tmp_q);
    pgp_mpi_zero(&tmp_a);
    pgp_mpi_zero(&tmp_b);
    for (uint32_t i = 0; i < a->count && i < PGP_MPI_MAX_LIMBS; i++) tmp_a.limbs[i] = a->limbs[i];
    tmp_a.count = a->count;
    for (uint32_t i = 0; i < b->count && i < PGP_MPI_MAX_LIMBS; i++) tmp_b.limbs[i] = b->limbs[i];
    tmp_b.count = b->count;
    if (pgp_mpi_compare(&tmp_a, &tmp_b) < 0) {
        if (rem) for (uint32_t i = 0; i < tmp_a.count; i++) rem->limbs[i] = tmp_a.limbs[i];
        if (rem) { rem->count = tmp_a.count; mpi_normalize(rem); }
        if (q) pgp_mpi_zero(q);
        return;
    }
    int shift = 0;
    uint32_t b_top = tmp_b.limbs[tmp_b.count - 1];
    while (!(b_top & 0x80000000)) { b_top <<= 1; shift++; }
    pgp_mpi_lshift(&tmp_a, &tmp_a, shift);
    pgp_mpi_lshift(&tmp_b, &tmp_b, shift);
    mpi_normalize(&tmp_b);
    mpi_normalize(&tmp_a);
    uint32_t a_bits = (tmp_a.count > 0) ? (tmp_a.count * 32 - __builtin_clz(tmp_a.limbs[tmp_a.count - 1])) : 0;
    uint32_t b_bits = (tmp_b.count > 0) ? (tmp_b.count * 32 - __builtin_clz(tmp_b.limbs[tmp_b.count - 1])) : 0;
    while (b_bits <= a_bits) {
        pgp_mpi_zero(&shifted);
        pgp_mpi_set_u32(&qbit, 1);
        pgp_mpi_lshift(&shifted, &tmp_b, a_bits - b_bits);
        pgp_mpi_lshift(&qbit, &qbit, a_bits - b_bits);
        if (pgp_mpi_compare(&tmp_a, &shifted) >= 0) {
            pgp_mpi_sub(&tmp_a, &tmp_a, &shifted);
            pgp_mpi_add(&tmp_q, &tmp_q, &qbit);
        }
        a_bits = (tmp_a.count > 0) ? (tmp_a.count * 32 - __builtin_clz(tmp_a.limbs[tmp_a.count - 1])) : 0;
    }
    pgp_mpi_rshift(&tmp_a, &tmp_a, shift);
    if (rem) for (uint32_t i = 0; i < tmp_a.count; i++) rem->limbs[i] = tmp_a.limbs[i];
    if (rem) { rem->count = tmp_a.count; mpi_normalize(rem); }
    if (q) {
        mpi_normalize(&tmp_q);
        for (uint32_t i = 0; i < tmp_q.count && i < PGP_MPI_MAX_LIMBS; i++) q->limbs[i] = tmp_q.limbs[i];
        q->count = tmp_q.count;
        mpi_normalize(q);
    }
}

void pgp_mpi_mod(pgp_mpi_t *r, const pgp_mpi_t *a, const pgp_mpi_t *m) {
    pgp_mpi_t tmp;
    pgp_mpi_div(NULL, &tmp, a, m);
    for (uint32_t i = 0; i < tmp.count; i++) r->limbs[i] = tmp.limbs[i];
    r->count = tmp.count;
    mpi_normalize(r);
}

void pgp_mpi_pow_mod(pgp_mpi_t *r, const pgp_mpi_t *a, const pgp_mpi_t *e, const pgp_mpi_t *m) {
    pgp_mpi_t base, exp, result;
    pgp_mpi_zero(&result);
    pgp_mpi_set_u32(&result, 1);
    for (uint32_t i = 0; i < a->count; i++) base.limbs[i] = a->limbs[i];
    base.count = a->count;
    mpi_normalize(&base);
    for (uint32_t i = 0; i < e->count; i++) exp.limbs[i] = e->limbs[i];
    exp.count = e->count;
    mpi_normalize(&exp);
    pgp_mpi_mod(&base, &base, m);
    uint32_t bits = (exp.count > 0) ? (exp.count * 32 - __builtin_clz(exp.limbs[exp.count - 1])) : 0;
    for (int i = (int)bits - 1; i >= 0; i--) {
        pgp_mpi_mul(&result, &result, &result);
        pgp_mpi_mod(&result, &result, m);
        uint32_t limb_idx = i / 32;
        uint32_t bit_idx = i % 32;
        if (limb_idx < exp.count && (exp.limbs[limb_idx] & (1U << bit_idx))) {
            pgp_mpi_mul(&result, &result, &base);
            pgp_mpi_mod(&result, &result, m);
        }
    }
    for (uint32_t i = 0; i < result.count; i++) r->limbs[i] = result.limbs[i];
    r->count = result.count;
    mpi_normalize(r);
}

void pgp_mpi_gcd(pgp_mpi_t *r, const pgp_mpi_t *a, const pgp_mpi_t *b) {
    pgp_mpi_t ta, tb, tmp;
    for (uint32_t i = 0; i < a->count; i++) ta.limbs[i] = a->limbs[i];
    ta.count = a->count;
    for (uint32_t i = 0; i < b->count; i++) tb.limbs[i] = b->limbs[i];
    tb.count = b->count;
    mpi_normalize(&ta);
    mpi_normalize(&tb);
    while (!pgp_mpi_is_zero(&tb)) {
        pgp_mpi_mod(&tmp, &ta, &tb);
        for (uint32_t i = 0; i < tb.count; i++) ta.limbs[i] = tb.limbs[i];
        ta.count = tb.count;
        for (uint32_t i = 0; i < tmp.count; i++) tb.limbs[i] = tmp.limbs[i];
        tb.count = tmp.count;
        mpi_normalize(&ta);
        mpi_normalize(&tb);
    }
    for (uint32_t i = 0; i < ta.count; i++) r->limbs[i] = ta.limbs[i];
    r->count = ta.count;
    mpi_normalize(r);
}

int pgp_mpi_inv_mod(pgp_mpi_t *r, const pgp_mpi_t *a, const pgp_mpi_t *m) {
    pgp_mpi_t t, newt, r0, newr0, q, tmp, tmp2;
    pgp_mpi_set_u32(&t, 0);
    pgp_mpi_set_u32(&newt, 1);
    for (uint32_t i = 0; i < m->count; i++) r0.limbs[i] = m->limbs[i];
    r0.count = m->count;
    mpi_normalize(&r0);
    for (uint32_t i = 0; i < a->count; i++) newr0.limbs[i] = a->limbs[i];
    newr0.count = a->count;
    pgp_mpi_mod(&newr0, &newr0, m);
    mpi_normalize(&newr0);
    while (!pgp_mpi_is_zero(&newr0)) {
        pgp_mpi_div(&q, &tmp, &r0, &newr0);
        for (uint32_t i = 0; i < newr0.count; i++) r0.limbs[i] = newr0.limbs[i];
        r0.count = newr0.count;
        for (uint32_t i = 0; i < tmp.count; i++) newr0.limbs[i] = tmp.limbs[i];
        newr0.count = tmp.count;
        mpi_normalize(&r0);
        mpi_normalize(&newr0);
        for (uint32_t i = 0; i < t.count; i++) tmp.limbs[i] = t.limbs[i];
        tmp.count = t.count;
        pgp_mpi_mul(&tmp2, &q, &newt);
        if (pgp_mpi_compare(&tmp, &tmp2) >= 0) {
            pgp_mpi_sub(&t, &tmp, &tmp2);
        } else {
            pgp_mpi_sub(&t, &tmp2, &tmp);
            pgp_mpi_sub(&t, m, &t);
        }
        for (uint32_t i = 0; i < newt.count; i++) t.limbs[i] = newt.limbs[i];
        t.count = newt.count;
        for (uint32_t i = 0; i < tmp2.count; i++) newt.limbs[i] = tmp2.limbs[i];
        newt.count = tmp2.count;
        mpi_normalize(&t);
        mpi_normalize(&newt);
    }
    if (!pgp_mpi_is_one(&r0)) return PGP_ERR_KEY;
    for (uint32_t i = 0; i < t.count; i++) r->limbs[i] = t.limbs[i];
    r->count = t.count;
    mpi_normalize(r);
    return PGP_OK;
}

int pgp_mpi_is_prime(const pgp_mpi_t *a, int rounds) {
    if (pgp_mpi_is_zero(a) || pgp_mpi_is_one(a)) return 0;
    if (a->limbs[0] % 2 == 0) return (a->limbs[0] == 2);
    pgp_mpi_t d, s, one, two, a_minus_1, x, y, jmp;
    pgp_mpi_set_u32(&one, 1);
    pgp_mpi_set_u32(&two, 2);
    pgp_mpi_sub(&a_minus_1, a, &one);
    for (uint32_t i = 0; i < a_minus_1.count; i++) d.limbs[i] = a_minus_1.limbs[i];
    d.count = a_minus_1.count;
    pgp_mpi_set_u32(&s, 0);
    while (d.limbs[0] % 2 == 0) {
        pgp_mpi_rshift(&d, &d, 1);
        s.limbs[0]++;
    }
    for (int i = 0; i < rounds; i++) {
        pgp_mpi_rand(&x, 32);
        pgp_mpi_mod(&x, &x, &a_minus_1);
        if (pgp_mpi_is_zero(&x) || pgp_mpi_is_one(&x)) continue;
        pgp_mpi_pow_mod(&y, &x, &d, a);
        if (pgp_mpi_is_one(&y) || pgp_mpi_compare(&y, &a_minus_1) == 0) continue;
        int composite = 1;
        pgp_mpi_t j; pgp_mpi_set_u32(&j, 1);
        while (pgp_mpi_compare(&j, &s) < 0) {
            pgp_mpi_pow_mod(&y, &y, &two, a);
            if (pgp_mpi_compare(&y, &a_minus_1) == 0) { composite = 0; break; }
            jmp.limbs[0] = j.limbs[0] + 1; jmp.count = (jmp.limbs[0] ? 1 : 0);
            j.limbs[0] = jmp.limbs[0];
        }
        if (composite) return 0;
    }
    return 1;
}

void pgp_mpi_from_bytes(pgp_mpi_t *a, const uint8_t *buf, uint32_t len) {
    pgp_mpi_zero(a);
    if (!len) return;
    uint32_t bit_len = ((uint32_t)buf[0] << 8) | buf[1];
    uint32_t byte_len = (bit_len + 7) / 8;
    buf += 2; len = (len - 2 < byte_len) ? len - 2 : byte_len;
    for (uint32_t i = 0; i < len; i++) {
        uint32_t limb = (len - 1 - i) / 4;
        if (limb >= PGP_MPI_MAX_LIMBS) break;
        a->limbs[limb] |= ((uint32_t)buf[i]) << ((3 - (i % 4)) * 8);
    }
    a->count = PGP_MPI_MAX_LIMBS;
    mpi_normalize(a);
}

void pgp_mpi_to_bytes(const pgp_mpi_t *a, uint8_t *buf, uint32_t *len) {
    uint32_t bit_len = 0;
    if (a->count > 0) {
        uint32_t top = a->limbs[a->count - 1];
        bit_len = (a->count - 1) * 32;
        if (top) { uint32_t c = __builtin_clz(top); bit_len += 32 - c; }
    }
    buf[0] = (bit_len >> 8) & 0xFF;
    buf[1] = bit_len & 0xFF;
    uint32_t byte_len = (bit_len + 7) / 8;
    for (uint32_t i = 0; i < byte_len; i++) {
        uint32_t limb = (byte_len - 1 - i) / 4;
        uint32_t byte_in_limb = 3 - (i % 4);
        if (limb < PGP_MPI_MAX_LIMBS)
            buf[2 + i] = (a->limbs[limb] >> (byte_in_limb * 8)) & 0xFF;
        else
            buf[2 + i] = 0;
    }
    *len = byte_len + 2;
}

void pgp_mpi_rand(pgp_mpi_t *a, uint32_t bits) {
    uint32_t bytes = (bits + 7) / 8;
    pgp_mpi_zero(a);
    for (uint32_t i = 0; i < bytes; i++) {
        uint8_t r = 0;
        sec_random_bytes(&r, 1);
        uint32_t limb = (bytes - 1 - i) / 4;
        if (limb < PGP_MPI_MAX_LIMBS)
            a->limbs[limb] |= ((uint32_t)r) << ((3 - (i % 4)) * 8);
    }
    a->count = (bytes + 3) / 4;
    mpi_normalize(a);
    a->limbs[a->count - 1] |= (1U << ((bits - 1) % 32));
    a->limbs[0] |= 1;
}

void pgp_mpi_lshift(pgp_mpi_t *r, const pgp_mpi_t *a, uint32_t bits) {
    uint32_t limb_shift = bits / 32;
    uint32_t bit_shift = bits % 32;
    uint64_t carry = 0;
    for (uint32_t i = 0; i < a->count + limb_shift + 1 && i < PGP_MPI_MAX_LIMBS; i++) {
        uint64_t val = carry;
        if (i >= limb_shift && i - limb_shift < a->count)
            val += (uint64_t)a->limbs[i - limb_shift] << bit_shift;
        r->limbs[i] = (uint32_t)(val & 0xFFFFFFFF);
        carry = val >> 32;
    }
    r->count = a->count + limb_shift + 1;
    if (r->count > PGP_MPI_MAX_LIMBS) r->count = PGP_MPI_MAX_LIMBS;
    mpi_normalize(r);
}

void pgp_mpi_rshift(pgp_mpi_t *r, const pgp_mpi_t *a, uint32_t bits) {
    uint32_t limb_shift = bits / 32;
    uint32_t bit_shift = bits % 32;
    uint64_t carry = 0;
    for (int i = (int)a->count - 1; i >= 0; i--) {
        uint64_t val = ((uint64_t)a->limbs[i]) << 32;
        uint32_t shifted = (uint32_t)((val >> bit_shift) & 0xFFFFFFFF);
        if ((uint32_t)(i - limb_shift) < PGP_MPI_MAX_LIMBS) {
            if (i >= (int)limb_shift)
                r->limbs[i - limb_shift] = (shifted | (uint32_t)carry);
            else
                r->limbs[0] = 0;
        }
        carry = (val >> (bit_shift + 32)) & 0xFFFFFFFF;
    }
    if (a->count > limb_shift)
        r->count = a->count - limb_shift;
    else
        r->count = 0;
    mpi_normalize(r);
}

void pgp_mpi_and(pgp_mpi_t *r, const pgp_mpi_t *a, const pgp_mpi_t *b) {
    uint32_t max = (a->count < b->count) ? a->count : b->count;
    for (uint32_t i = 0; i < max; i++) r->limbs[i] = a->limbs[i] & b->limbs[i];
    r->count = max;
    mpi_normalize(r);
}

void pgp_mpi_set_bit(pgp_mpi_t *a, uint32_t bit) {
    uint32_t limb = bit / 32;
    uint32_t bitpos = bit % 32;
    if (limb >= PGP_MPI_MAX_LIMBS) return;
    a->limbs[limb] |= (1U << bitpos);
    if (limb >= a->count) a->count = limb + 1;
}
