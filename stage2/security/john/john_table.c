#include "john_core.h"
#include "boot/security.h"

static const char base62[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
static john_rainbow_table_t stored_table;
static uint32_t table_valid = 0;

static uint32_t xor_fold(const uint8_t *hash, uint32_t hash_len) {
    uint32_t result = 0;
    uint32_t i;
    for (i = 0; i < hash_len; i++) {
        result ^= (uint32_t)hash[i] << ((i & 3) * 8);
    }
    return result;
}

static void reduce_step(const uint8_t *hash, uint32_t hash_len, uint32_t step,
                        uint8_t *out_password, uint32_t out_len) {
    uint32_t val = xor_fold(hash, hash_len) + step;
    uint32_t i;
    for (i = 0; i < out_len; i++) {
        out_password[i] = (uint8_t)base62[val % 62];
        val /= 62;
    }
}

static void random_password(uint8_t *buf, uint32_t len) {
    uint8_t randbyte;
    uint32_t i;
    for (i = 0; i < len; i++) {
        sec_random_bytes(&randbyte, 1);
        buf[i] = (uint8_t)base62[randbyte % 62];
    }
}

static uint8_t hash_len_for_type(uint8_t type) {
    switch (type) {
        case JOHN_HASH_MD5:      return 16;
        case JOHN_HASH_SHA1:     return 20;
        case JOHN_HASH_SHA256:   return 32;
        case JOHN_HASH_SHA512:   return 64;
        case JOHN_HASH_NTLM:     return 16;
        case JOHN_HASH_DES_CRYPT: return 11;
        case JOHN_HASH_BCRYPT:   return 24;
        case JOHN_HASH_MD4:      return 16;
        case JOHN_HASH_HMAC_SHA256: return 32;
        case JOHN_HASH_LM:       return 16;
        default:                 return 16;
    }
}

int john_table_init(void) {
    uint32_t i, j;
    stored_table.chain_len = 0;
    stored_table.chain_count = 0;
    stored_table.hash_type = 0;
    for (i = 0; i < JOHN_MAX_RAINBOW_CHAINS; i++) {
        for (j = 0; j < 64; j++) stored_table.entries[i].hash[j] = 0;
        for (j = 0; j < JOHN_MAX_PASSWORD; j++) stored_table.entries[i].start_password[j] = 0;
        stored_table.entries[i].start_len = 0;
    }
    table_valid = 0;
    return JOHN_OK;
}

int john_table_generate(uint8_t hash_type, uint32_t chain_len, uint32_t chain_count) {
    uint32_t i, step;
    uint8_t password[JOHN_MAX_PASSWORD];
    uint8_t hash[64];
    uint8_t hlen;
    uint32_t entry_count;

    if (chain_count > JOHN_MAX_RAINBOW_CHAINS) {
        chain_count = JOHN_MAX_RAINBOW_CHAINS;
    }
    if (chain_len == 0 || chain_count == 0) {
        return JOHN_ERR_BAD_PARAM;
    }

    hlen = hash_len_for_type(hash_type);
    entry_count = 0;

    for (i = 0; i < chain_count; i++) {
        random_password(password, 8);

        john_hash_compute(hash_type, (const char *)password, 8, (const uint8_t *)0, 0, 0, hash, (uint8_t *)0);

        for (step = 0; step < chain_len; step++) {
            reduce_step(hash, hlen, step, password, 8);
            john_hash_compute(hash_type, (const char *)password, 8, (const uint8_t *)0, 0, 0, hash, (uint8_t *)0);
        }

        if (entry_count < JOHN_MAX_RAINBOW_CHAINS) {
            for (uint32_t j = 0; j < hlen; j++) {
                stored_table.entries[entry_count].hash[j] = hash[j];
            }
            for (uint32_t j = 0; j < 8; j++) {
                stored_table.entries[entry_count].start_password[j] = password[j];
            }
            stored_table.entries[entry_count].start_len = 8;
            entry_count++;
        }
    }

    stored_table.hash_type = hash_type;
    stored_table.chain_len = (uint8_t)chain_len;
    stored_table.chain_count = entry_count;
    table_valid = 1;
    return JOHN_OK;
}

int john_table_lookup(const john_hash_t *target, john_result_t *result) {
    uint32_t i, step, k;
    uint8_t hlen;
    uint8_t current[64];
    uint8_t password[JOHN_MAX_PASSWORD];
    uint8_t forward[64];

    if (!table_valid || stored_table.chain_count == 0) {
        return JOHN_ERR_NO_MATCH;
    }

    hlen = hash_len_for_type(stored_table.hash_type);

    for (i = 0; i < stored_table.chain_count; i++) {
        for (k = 0; k < hlen; k++) {
            current[k] = stored_table.entries[i].hash[k];
        }

        int32_t match_step = -1;

        for (step = stored_table.chain_len; step > 0; step--) {
            reduce_step(current, hlen, step - 1, password, 8);
            john_hash_compute(stored_table.hash_type, (const char *)password, 8,
                              target->salt, target->salt_len, target->iterations,
                              current, (uint8_t *)0);

            int match = 1;
            for (k = 0; k < hlen; k++) {
                if (current[k] != target->hash[k]) { match = 0; break; }
            }
            if (match) {
                match_step = (int32_t)(step - 1);
                break;
            }
        }

        if (match_step < 0) continue;

        for (k = 0; k < 8; k++) {
            password[k] = stored_table.entries[i].start_password[k];
        }

        john_hash_compute(stored_table.hash_type, (const char *)password, 8,
                          target->salt, target->salt_len, target->iterations,
                          forward, (uint8_t *)0);

        int match = 1;
        for (k = 0; k < hlen; k++) {
            if (forward[k] != target->hash[k]) { match = 0; break; }
        }
        if (match) {
            for (k = 0; k < 8; k++) result->password[k] = (char)password[k];
            result->password[8] = '\0';
            result->password_len = 8;
            result->found = 1;
            return JOHN_OK;
        }

        for (step = 0; step < (uint32_t)(match_step + 1); step++) {
            reduce_step(forward, hlen, step, password, 8);
            john_hash_compute(stored_table.hash_type, (const char *)password, 8,
                              target->salt, target->salt_len, target->iterations,
                              forward, (uint8_t *)0);

            match = 1;
            for (k = 0; k < hlen; k++) {
                if (forward[k] != target->hash[k]) { match = 0; break; }
            }
            if (match) {
                for (k = 0; k < 8; k++) result->password[k] = (char)password[k];
                result->password[8] = '\0';
                result->password_len = 8;
                result->found = 1;
                return JOHN_OK;
            }
        }
    }

    return JOHN_ERR_NO_MATCH;
}

int john_table_store(const john_rainbow_table_t *table) {
    uint32_t i, j;
    if (!table || table->chain_count > JOHN_MAX_RAINBOW_CHAINS) {
        return JOHN_ERR_BAD_PARAM;
    }

    stored_table.hash_type = table->hash_type;
    stored_table.chain_len = table->chain_len;
    stored_table.chain_count = table->chain_count;

    for (i = 0; i < table->chain_count; i++) {
        for (j = 0; j < 64; j++) {
            stored_table.entries[i].hash[j] = table->entries[i].hash[j];
        }
        for (j = 0; j < JOHN_MAX_PASSWORD; j++) {
            stored_table.entries[i].start_password[j] = table->entries[i].start_password[j];
        }
        stored_table.entries[i].start_len = table->entries[i].start_len;
    }

    table_valid = 1;
    return JOHN_OK;
}

void john_table_reset(void) {
    john_table_init();
}

uint32_t john_table_get_count(void) {
    if (!table_valid) return 0;
    return stored_table.chain_count;
}
