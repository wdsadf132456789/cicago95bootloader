#include "boot/disk_privacy.h"
#include "boot/security.h"

static dp_lba_scramble_state_t state;

static uint64_t lba_feistel(uint64_t lba, const uint8_t *key)
{
	uint64_t left, right, temp;
	uint32_t r, i;

	left = lba >> 32;
	right = lba & 0xFFFFFFFF;

	for (r = 0; r < 6; r++) {
		uint64_t f = 0;
		for (i = 0; i < 8; i++)
			f ^= ((uint64_t)key[(r * 8 + i) % DP_KEY_LEN]) << (i * 8);
		f ^= right;
		f = (f >> 13) | (f << 51);
		f &= 0xFFFFFFFF;
		left ^= f;

		temp = left;
		left = right;
		right = temp;
	}

	return (left << 32) | (right & 0xFFFFFFFF);
}

static uint64_t lba_derive_key_u64(uint8_t disk_idx, uint32_t offset)
{
	uint64_t val;
	uint32_t i;

	val = 0;
	for (i = 0; i < 8; i++)
		val ^= ((uint64_t)state.key[(offset + i) % DP_KEY_LEN]) << (i * 8);
	val ^= (uint64_t)disk_idx * 0x9E3779B97F4A7C15ULL;
	return val;
}

static uint64_t lba_scramble_one(uint8_t disk_idx, uint64_t real_lba)
{
	uint64_t xored, permuted, result;
	uint8_t i;

	xored = real_lba ^ lba_derive_key_u64(disk_idx, 0);
	permuted = lba_feistel(xored, state.key);

	result = permuted ^ lba_derive_key_u64(disk_idx, 16);

	for (i = 0; i < 8; i++) {
		uint8_t b = (uint8_t)(result >> (i * 8));
		b = (uint8_t)((b << 3) | (b >> 5));
		result &= ~((uint64_t)0xFF << (i * 8));
		result |= (uint64_t)b << (i * 8);
	}

	return result;
}

static uint64_t lba_unscramble_one(uint8_t disk_idx, uint64_t scrambled_lba)
{
	uint64_t result, xored, permuted;
	uint8_t i;

	result = scrambled_lba;
	for (i = 0; i < 8; i++) {
		uint8_t b = (uint8_t)(result >> (i * 8));
		b = (uint8_t)((b >> 3) | (b << 5));
		result &= ~((uint64_t)0xFF << (i * 8));
		result |= (uint64_t)b << (i * 8);
	}

	xored = result ^ lba_derive_key_u64(disk_idx, 16);
	permuted = lba_feistel(xored, state.key);

	return permuted ^ lba_derive_key_u64(disk_idx, 0);
}

int dp_lba_scramble_init(void)
{
	uint8_t *d = (uint8_t *)&state;
	uint32_t i;

	for (i = 0; i < sizeof(dp_lba_scramble_state_t); i++)
		d[i] = 0;

	sec_random_bytes(state.key, DP_KEY_LEN);
	state.armed = 1;

	return DP_OK;
}

int dp_lba_scramble_add(uint8_t disk_idx, uint64_t real_lba)
{
	uint32_t idx;
	uint32_t i;

	if (state.armed == 0)
		return DP_ERR_BAD_PARAM;

	if (state.count >= DP_MAX_MAP)
		return DP_ERR_NO_SPACE;

	for (i = 0; i < state.count; i++) {
		if (state.map[i].active &&
		    state.map[i].disk_idx == disk_idx &&
		    state.map[i].real_lba == real_lba) {
			return DP_OK;
		}
	}

	idx = state.count;
	state.map[idx].disk_idx = disk_idx;
	state.map[idx].real_lba = real_lba;
	state.map[idx].scrambled_lba = lba_scramble_one(disk_idx, real_lba);
	state.map[idx].active = 1;
	state.count++;

	return DP_OK;
}

int dp_lba_scramble_get(uint8_t disk_idx, uint64_t real_lba,
			 uint64_t *out_scrambled)
{
	uint32_t i;

	if (out_scrambled == 0)
		return DP_ERR_BAD_PARAM;

	if (state.armed == 0)
		return DP_ERR_BAD_PARAM;

	for (i = 0; i < state.count; i++) {
		if (state.map[i].active &&
		    state.map[i].disk_idx == disk_idx &&
		    state.map[i].real_lba == real_lba) {
			*out_scrambled = state.map[i].scrambled_lba;
			return DP_OK;
		}
	}

	return DP_ERR_NOT_FOUND;
}

int dp_lba_scramble_resolve(uint8_t disk_idx, uint64_t scrambled_lba,
			    uint64_t *out_real)
{
	uint32_t i;

	if (out_real == 0)
		return DP_ERR_BAD_PARAM;

	if (state.armed == 0)
		return DP_ERR_BAD_PARAM;

	for (i = 0; i < state.count; i++) {
		if (state.map[i].active &&
		    state.map[i].disk_idx == disk_idx &&
		    state.map[i].scrambled_lba == scrambled_lba) {
			*out_real = state.map[i].real_lba;
			return DP_OK;
		}
	}

	return DP_ERR_NOT_FOUND;
}

int dp_lba_scramble_count(void)
{
	return (int)state.count;
}

void dp_lba_scramble_reset(void)
{
	uint8_t *d = (uint8_t *)&state;
	uint32_t i;

	for (i = 0; i < sizeof(dp_lba_scramble_state_t); i++)
		d[i] = 0;
}
