/**
 * John the Reaper - Bare-Metal Password Cracking Engine
 * Boot-time password strength auditor and hash cracker
 *
 * Supports: MD5, SHA-1, SHA-256, SHA-512, NTLM, DES-crypt, bcrypt
 * Attack modes: Wordlist, Rule-based mutation, Brute force, Rainbow tables
 * Runs entirely at boot before OS loads - no libc, no OS, no disk.
 */

#ifndef JOHN_REAPER_H
#define JOHN_REAPER_H

#include <stdint.h>

/* ========================================================================
 * John the Reaper constants
 * ======================================================================== */
#define JOHN_OK                  0
#define JOHN_ERR_NOMEM          -1
#define JOHN_ERR_BAD_PARAM      -2
#define JOHN_ERR_NOT_INIT       -3
#define JOHN_ERR_NO_MATCH       -4
#define JOHN_ERR_OVERFLOW       -5
#define JOHN_ERR_HARDWARE       -6

#define JOHN_MAX_PASSWORD       128
#define JOHN_MAX_HASH           256
#define JOHN_MAX_SALT           64
#define JOHN_MAX_WORDLIST       2048
#define JOHN_MAX_RULES          256
#define JOHN_MAX_CANDIDATES     1024
#define JOHN_MAX_RAINBOW_CHAINS 4096
#define JOHN_MAX_RAINBOW_ENTRY  32

/* Hash type identifiers */
#define JOHN_HASH_MD5           0
#define JOHN_HASH_SHA1          1
#define JOHN_HASH_SHA256        2
#define JOHN_HASH_SHA512        3
#define JOHN_HASH_NTLM          4
#define JOHN_HASH_DES_CRYPT     5
#define JOHN_HASH_BCRYPT        6
#define JOHN_HASH_MD4           7
#define JOHN_HASH_HMAC_SHA256   8
#define JOHN_HASH_LM            9
#define JOHN_HASH_COUNT         10

/* Attack mode identifiers */
#define JOHN_ATTACK_WORDLIST    0
#define JOHN_ATTACK_RULES       1
#define JOHN_ATTACK_BRUTE       2
#define JOHN_ATTACK_RAINBOW     3
#define JOHN_ATTACK_HYBRID      4
#define JOHN_ATTACK_MASK        5

/* Charset definitions for brute force */
#define JOHN_CHARSET_LOWER      0x01
#define JOHN_CHARSET_UPPER      0x02
#define JOHN_CHARSET_DIGITS     0x04
#define JOHN_CHARSET_SYMBOLS    0x08
#define JOHN_CHARSET_ALL        0x0F

/* ========================================================================
 * Hash representation
 * ======================================================================== */
typedef struct {
    uint8_t  type;               /* JOHN_HASH_* */
    uint8_t  hash[64];           /* Raw hash bytes (up to 512-bit) */
    uint8_t  hash_len;           /* Hash length in bytes */
    uint8_t  salt[JOHN_MAX_SALT];
    uint8_t  salt_len;
    uint16_t iterations;         /* For bcrypt, PBKDF2, etc. */
    uint8_t  format_id;          /* Format index for matching */
} john_hash_t;

/* ========================================================================
 * Password candidate
 * ======================================================================== */
typedef struct {
    char     password[JOHN_MAX_PASSWORD];
    uint8_t  len;
    uint64_t candidates_tried;
} john_candidate_t;

/* ========================================================================
 * Attack configuration
 * ======================================================================== */
typedef struct {
    uint8_t  mode;               /* JOHN_ATTACK_* */
    uint8_t  hash_type;          /* JOHN_HASH_* to crack */
    uint8_t  charset;            /* JOHN_CHARSET_* bitmask */
    uint8_t  min_len;
    uint8_t  max_len;
    uint8_t  max_threads;        /* 1 = single core */
    uint32_t max_candidates;     /* 0 = unlimited */
    uint64_t max_time_ms;        /* 0 = unlimited */
    char     mask[JOHN_MAX_PASSWORD];   /* For mask attack ?a?d?l etc */
    uint8_t  mask_len;
    uint32_t rule_count;
    uint8_t  rules[JOHN_MAX_RULES * 32];
} john_attack_config_t;

/* ========================================================================
 * Cracking result
 * ======================================================================== */
typedef struct {
    uint8_t  found;
    char     password[JOHN_MAX_PASSWORD];
    uint8_t  password_len;
    uint64_t candidates_tried;
    uint64_t time_ms;
    uint32_t rate;               /* candidates per second */
} john_result_t;

/* ========================================================================
 * Statistics
 * ======================================================================== */
typedef struct {
    uint64_t passwords_cracked;
    uint64_t passwords_tested;
    uint64_t candidates_generated;
    uint64_t hashes_computed;
    uint64_t total_time_ms;
    uint32_t current_rate;
    uint32_t peak_rate;
    uint8_t  current_mode;
    uint8_t  current_hash_type;
    uint8_t  initialized;
} john_stats_t;

/* ========================================================================
 * Rainbow table entry
 * ======================================================================== */
typedef struct {
    uint8_t  hash[64];
    uint8_t  start_password[JOHN_MAX_PASSWORD];
    uint8_t  start_len;
} john_rainbow_entry_t;

typedef struct {
    uint8_t  chain_len;
    uint32_t chain_count;
    uint8_t  hash_type;
    john_rainbow_entry_t entries[JOHN_MAX_RAINBOW_CHAINS];
} john_rainbow_table_t;

/* ========================================================================
 * Built-in wordlist (bare-metal, compiled in)
 * ======================================================================== */
typedef struct {
    const char **words;
    uint32_t count;
} john_wordlist_t;

/* ========================================================================
 * Rule mutation
 * ======================================================================== */
typedef struct {
    uint8_t  type;               /* 0=prefix 1=suffix 2=replace 3=duplicate 4=toggle 5=reject */
    uint8_t  position;
    char     data[32];
    uint32_t param;
} john_rule_t;

/* ========================================================================
 * Core API
 * ======================================================================== */
int  john_init(void);
int  john_set_target(const john_hash_t *hash);
int  john_crack(const john_attack_config_t *config, john_result_t *result);
int  john_analyze_password(const char *password, uint32_t len, uint8_t hash_type, john_result_t *result);
void john_stop(void);
void john_reset(void);
void john_get_stats(john_stats_t *stats);

/* ========================================================================
 * Hash computation (john_hash.c)
 * ======================================================================== */
void john_hash_compute(uint8_t type, const char *password, uint32_t pass_len,
                       const uint8_t *salt, uint32_t salt_len,
                       uint16_t iterations, uint8_t *out, uint8_t *out_len);
int  john_hash_compare(const john_hash_t *target, const char *password, uint32_t pass_len);
void john_hash_to_hex(const uint8_t *hash, uint8_t hash_len, char *hex, uint32_t hex_max);
int  john_hash_from_hex(const char *hex, uint32_t hex_len, uint8_t *out, uint8_t *out_max);

/* ========================================================================
 * Brute force engine (john_brute.c)
 * ======================================================================== */
int  john_brute_init(void);
int  john_brute_crack(const john_hash_t *target, const john_attack_config_t *config, john_result_t *result);
int  john_brute_next(john_candidate_t *candidate, const john_attack_config_t *config);
void john_brute_reset(void);

/* ========================================================================
 * Wordlist engine (john_wordlist.c)
 * ======================================================================== */
int  john_wordlist_init(void);
int  john_wordlist_crack(const john_hash_t *target, const john_attack_config_t *config, john_result_t *result);
int  john_wordlist_load(const char **words, uint32_t count);
int  john_wordlist_next(john_candidate_t *candidate);
int  john_wordlist_apply_rules(const char *word, uint32_t word_len,
                               const john_rule_t *rules, uint32_t rule_count,
                               char *out, uint32_t out_max);
void john_wordlist_reset(void);

/* ========================================================================
 * Password strength analyzer (john_analyzer.c)
 * ======================================================================== */
int  john_analyzer_init(void);
int  john_analyzer_score(const char *password, uint32_t len, uint8_t hash_type);
uint32_t john_analyzer_entropy(const char *password, uint32_t len);
uint32_t john_analyzer_crack_time_seconds(const char *password, uint32_t len, uint8_t hash_type);
uint32_t john_analyzer_charset_size(const char *password, uint32_t len);
int  john_analyzer_suggest(const char *password, uint32_t len, char *suggestion, uint32_t sug_max);
void john_analyzer_get_stats(john_stats_t *stats);

/* ========================================================================
 * Rainbow table engine (john_table.c)
 * ======================================================================== */
int  john_table_init(void);
int  john_table_generate(uint8_t hash_type, uint32_t chain_len, uint32_t chain_count);
int  john_table_lookup(const john_hash_t *target, john_result_t *result);
int  john_table_store(const john_rainbow_table_t *table);
void john_table_reset(void);
uint32_t john_table_get_count(void);

/* ========================================================================
 * Built-in wordlist data (john_wordlist.c)
 * ======================================================================== */
extern const john_wordlist_t john_wordlist_common;

#endif /* JOHN_REAPER_H */
