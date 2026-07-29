#include "boot/disk_privacy.h"
#include "boot/security.h"

#define DP_MBR_GPT_MASK_KEY_LEN 16

static dp_mbr_gpt_mask_state_t state;

static uint32_t dp_mbr_gpt_mask_derive_mbr_key(uint32_t disk_idx)
{
	uint32_t val;
	uint8_t i;

	val = 0;
	for (i = 0; i < DP_MBR_GPT_MASK_KEY_LEN; i += 4) {
		val ^= (uint32_t)state.key[i] << 24;
		val ^= (uint32_t)state.key[i + 1] << 16;
		val ^= (uint32_t)state.key[i + 2] << 8;
		val ^= (uint32_t)state.key[i + 3];
	}
	val ^= (disk_idx * 0x9E3779B9U);
	return val;
}

static void dp_mbr_gpt_mask_derive_gpt_key(uint32_t disk_idx,
					    uint8_t out[16])
{
	uint8_t i;

	for (i = 0; i < 16; i++)
		out[i] = state.key[i] ^ (uint8_t)((disk_idx >> ((i % 4) * 8)) & 0xFF);
}

int dp_mbr_gpt_mask_init(void)
{
	uint8_t *p = (uint8_t *)&state;
	uint32_t sz = sizeof(state);
	while (sz--) p[sz] = 0;

	sec_random_bytes(state.key, DP_MBR_GPT_MASK_KEY_LEN);

	state.armed = 1;
	return 0;
}

int dp_mbr_gpt_mask_add(uint8_t disk_idx, uint32_t real_mbr_sig,
			 const uint8_t real_gpt_guid[DP_GPT_UUID_LEN])
{
	dp_mbr_gpt_map_t *disk;
	uint32_t mbr_key;
	uint8_t gpt_key[16];
	uint8_t i;

	if (!state.armed)
		return -1;

	if (disk_idx >= DP_MAX_DISKS)
		return -1;

	disk = &state.map[disk_idx];

	disk->real_mbr_sig = real_mbr_sig;
	for (i = 0; i < DP_GPT_UUID_LEN; i++)
		disk->real_gpt_guid[i] = real_gpt_guid[i];

	mbr_key = dp_mbr_gpt_mask_derive_mbr_key(disk_idx);
	disk->masked_mbr_sig = real_mbr_sig ^ mbr_key;

	dp_mbr_gpt_mask_derive_gpt_key(disk_idx, gpt_key);
	for (i = 0; i < DP_GPT_UUID_LEN; i++)
		disk->masked_gpt_guid[i] = real_gpt_guid[i] ^ gpt_key[i];

	disk->active = 1;

	if (disk_idx >= state.count)
		state.count = disk_idx + 1;

	return 0;
}

int dp_mbr_gpt_mask_get(uint8_t disk_idx, uint32_t *out_mbr_sig,
			 uint8_t out_gpt_guid[DP_GPT_UUID_LEN])
{
	dp_mbr_gpt_map_t *disk;
	uint8_t i;

	if (!state.armed)
		return -1;

	if (disk_idx >= DP_MAX_DISKS)
		return -1;

	disk = &state.map[disk_idx];

	if (!disk->active)
		return -1;

	if (out_mbr_sig)
		*out_mbr_sig = disk->masked_mbr_sig;

	if (out_gpt_guid)
		for (i = 0; i < DP_GPT_UUID_LEN; i++)
			out_gpt_guid[i] = disk->masked_gpt_guid[i];

	return 0;
}

int dp_mbr_gpt_mask_resolve(uint32_t masked_mbr_sig, uint8_t *out_disk,
			     uint32_t *out_real)
{
	dp_mbr_gpt_map_t *disk;
	uint32_t idx;

	if (!state.armed)
		return -1;

	for (idx = 0; idx < state.count; idx++) {
		disk = &state.map[idx];
		if (!disk->active)
			continue;

		if (disk->masked_mbr_sig == masked_mbr_sig) {
			if (out_disk)
				*out_disk = (uint8_t)idx;

			if (out_real)
				*out_real = disk->real_mbr_sig;

			return 0;
		}
	}

	return -1;
}

int dp_mbr_gpt_mask_count(void)
{
	return state.count;
}

void dp_mbr_gpt_mask_reset(void)
{
	uint8_t *p = (uint8_t *)&state;
	uint32_t sz = sizeof(state);
	while (sz--) p[sz] = 0;
}
