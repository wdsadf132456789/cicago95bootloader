#include "boot/disk_privacy.h"
#include "boot/security.h"

static dp_size_obfs_state_t state;

static uint64_t size_derive_key_u64(uint8_t disk_idx, uint32_t offset)
{
	uint64_t val;
	uint32_t i;

	val = 0;
	for (i = 0; i < 8; i++)
		val ^= ((uint64_t)state.key[(offset + i) % DP_KEY_LEN]) << (i * 8);
	val ^= (uint64_t)disk_idx * 0x9E3779B97F4A7C15ULL;
	return val;
}

static uint64_t size_compute_reported(uint8_t disk_idx, uint64_t real_sectors)
{
	uint64_t xored, band, min_s, max_s, band_width;

	xored = real_sectors ^ size_derive_key_u64(disk_idx, 0);

	if (real_sectors <= 10)
		return xored;

	band = real_sectors / 10;
	min_s = real_sectors - band;
	max_s = real_sectors + band;
	band_width = max_s - min_s;

	if (band_width == 0)
		return real_sectors;

	xored = min_s + (xored % band_width);

	return xored;
}

int dp_size_obfs_init(void)
{
	uint8_t *d = (uint8_t *)&state;
	uint32_t i;

	for (i = 0; i < sizeof(dp_size_obfs_state_t); i++)
		d[i] = 0;

	sec_random_bytes(state.key, DP_KEY_LEN);
	state.armed = 1;

	return DP_OK;
}

int dp_size_obfs_add(uint8_t disk_idx, uint64_t real_sectors,
		     uint32_t real_sector_size)
{
	uint32_t i;

	if (state.armed == 0)
		return DP_ERR_BAD_PARAM;

	if (state.count >= DP_MAX_DISKS)
		return DP_ERR_NO_SPACE;

	for (i = 0; i < state.count; i++) {
		if (state.map[i].active &&
		    state.map[i].disk_idx == disk_idx) {
			return DP_OK;
		}
	}

	state.map[state.count].disk_idx = disk_idx;
	state.map[state.count].real_sectors = real_sectors;
	state.map[state.count].real_sector_size = real_sector_size;
	state.map[state.count].reported_sectors =
		size_compute_reported(disk_idx, real_sectors);
	state.map[state.count].reported_sector_size = real_sector_size;
	state.map[state.count].active = 1;
	state.count++;

	return DP_OK;
}

int dp_size_obfs_get(uint8_t disk_idx, uint64_t *out_sectors,
		     uint32_t *out_sector_size)
{
	uint32_t i;

	if (out_sectors == 0 || out_sector_size == 0)
		return DP_ERR_BAD_PARAM;

	if (state.armed == 0)
		return DP_ERR_BAD_PARAM;

	for (i = 0; i < state.count; i++) {
		if (state.map[i].active &&
		    state.map[i].disk_idx == disk_idx) {
			*out_sectors = state.map[i].reported_sectors;
			*out_sector_size = state.map[i].reported_sector_size;
			return DP_OK;
		}
	}

	return DP_ERR_NOT_FOUND;
}

int dp_size_obfs_resolve(uint8_t disk_idx, uint64_t reported_sectors,
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
		    state.map[i].reported_sectors == reported_sectors) {
			*out_real = state.map[i].real_sectors;
			return DP_OK;
		}
	}

	return DP_ERR_NOT_FOUND;
}

int dp_size_obfs_count(void)
{
	return (int)state.count;
}

void dp_size_obfs_reset(void)
{
	uint8_t *d = (uint8_t *)&state;
	uint32_t i;

	for (i = 0; i < sizeof(dp_size_obfs_state_t); i++)
		d[i] = 0;
}
