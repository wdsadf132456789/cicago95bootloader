/**
 * Chicago-95 GNU Tools
 * Freestanding implementations of classic GNU/Unix command-line utilities
 * grep, sed, cut, sort, uniq, wc, head, tail, tr, diff, find, xargs,
 * tee, base64, yes, seq, sleep, date, env, man, cat (enhanced)
 */

#ifndef GNU_TOOLS_H
#define GNU_TOOLS_H

#include <stdint.h>

/* Result codes */
#define GNU_OK       0
#define GNU_ERR     -1
#define GNU_NOT_FOUND -2

/* grep flags */
#define GNU_GREP_IGNORE_CASE  (1 << 0)
#define GNU_GREP_INVERT       (1 << 1)
#define GNU_GREP_LINE_NUM     (1 << 2)
#define GNU_GREP_COUNT_ONLY   (1 << 3)
#define GNU_GREP_WHOLE_WORD   (1 << 4)
#define GNU_GREP_EXTENDED     (1 << 5)

/* sort flags */
#define GNU_SORT_REVERSE      (1 << 0)
#define GNU_SORT_NUMERIC      (1 << 1)
#define GNU_SORT_UNIQUE       (1 << 2)
#define GNU_SORT_CASE_FOLD    (1 << 3)

/* cut delimiters */
#define GNU_CUT_DELIM_TAB     '\t'
#define GNU_CUT_DELIM_SPACE   ' '

/* ======================================================================== */
/* grep - pattern search in text                                            */
/* ======================================================================== */

/* Search a single line for a pattern (returns 1 if match, 0 if not) */
int  gnu_grep_line(const char *line, uint32_t line_len,
                   const char *pattern, uint32_t pat_len, uint32_t flags);

/* Search text buffer, call callback for each matching line */
typedef void (*gnu_grep_callback)(const char *line, uint32_t line_len,
                                  uint32_t line_num, void *userdata);
int  gnu_grep(const char *text, uint32_t text_len,
              const char *pattern, uint32_t flags,
              gnu_grep_callback cb, void *userdata);

/* ======================================================================== */
/* sed - stream editor (basic)                                              */
/* ======================================================================== */

/* Apply sed-like substitutions to a line */
/* Supported: s/pattern/replacement/g, d, p, q */
typedef struct {
    char     cmd;         /* 's', 'd', 'p', 'q' */
    char     pattern[64];
    char     replacement[64];
    uint8_t  global;      /* 'g' flag */
} gnu_sed_cmd_t;

int  gnu_sed_apply(const char *line, uint32_t line_len,
                   const gnu_sed_cmd_t *cmds, uint32_t ncmds,
                   char *out, uint32_t out_max);

/* Parse a sed expression string into commands */
int  gnu_sed_parse(const char *expr, gnu_sed_cmd_t *cmds, uint32_t max_cmds);

/* ======================================================================== */
/* cut - extract columns/fields                                             */
/* ======================================================================== */

/* Extract fields from a delimited line */
int  gnu_cut_delimited(const char *line, uint32_t line_len,
                       char delimiter, const uint32_t *fields,
                       uint32_t nfields, char *out, uint32_t out_max);

/* Extract character ranges (e.g., 1-5,8,10-20) */
int  gnu_cut_chars(const char *line, uint32_t line_len,
                   const uint32_t *ranges, uint32_t nranges,
                   char *out, uint32_t out_max);

/* ======================================================================== */
/* sort - sort lines                                                        */
/* ======================================================================== */

typedef struct {
    char     lines[32][256];
    uint32_t lengths[32];
    uint32_t count;
} gnu_sort_buffer_t;

int  gnu_sort_add(gnu_sort_buffer_t *buf, const char *line, uint32_t len);
int  gnu_sort_sort(gnu_sort_buffer_t *buf, uint32_t flags);

/* ======================================================================== */
/* uniq - filter unique lines                                               */
/* ======================================================================== */

int  gnu_uniq_filter(const char *lines, uint32_t lines_len,
                     char *out, uint32_t out_max, uint8_t count_repeats);

/* ======================================================================== */
/* wc - word/line/byte count                                                */
/* ======================================================================== */

typedef struct {
    uint32_t lines;
    uint32_t words;
    uint32_t bytes;
    uint32_t chars;
    uint32_t max_line_len;
} gnu_wc_result_t;

void gnu_wc(const char *text, uint32_t len, gnu_wc_result_t *result);

/* ======================================================================== */
/* head/tail - first/last N lines                                           */
/* ======================================================================== */

int  gnu_head(const char *text, uint32_t len, uint32_t nlines,
              char *out, uint32_t out_max);
int  gnu_tail(const char *text, uint32_t len, uint32_t nlines,
              char *out, uint32_t out_max);

/* ======================================================================== */
/* tr - translate characters                                                */
/* ======================================================================== */

int  gnu_tr(const char *input, uint32_t input_len,
            const char *from, const char *to,
            char *output, uint32_t output_max);

/* Translate with deletion: tr -d 'chars' */
int  gnu_tr_delete(const char *input, uint32_t input_len,
                   const char *chars,
                   char *output, uint32_t output_max);

/* Squeeze repeats: tr -s 'char' */
int  gnu_tr_squeeze(const char *input, uint32_t input_len,
                    const char *chars,
                    char *output, uint32_t output_max);

/* ======================================================================== */
/* diff - compare two texts                                                 */
/* ======================================================================== */

#define GNU_DIFF_SAME     0
#define GNU_DIFF_DIFFERENT 1
#define GNU_DIFF_ERROR   -1

typedef struct {
    uint32_t line;
    char     type;  /* '<', '>', '---', '***' */
    char     text[256];
} gnu_diff_line_t;

int  gnu_diff(const char *text1, uint32_t len1,
              const char *text2, uint32_t len2,
              gnu_diff_line_t *results, uint32_t max_results);

/* ======================================================================== */
/* base64 - encode/decode                                                   */
/* ======================================================================== */

int  gnu_base64_encode(const uint8_t *data, uint32_t len,
                       char *out, uint32_t out_max);
int  gnu_base64_decode(const char *encoded, uint32_t len,
                       uint8_t *out, uint32_t out_max);

/* ======================================================================== */
/* find - search (stub for bare-metal)                                      */
/* ======================================================================== */

/* In bare-metal, find lists BrainFS directory entries matching criteria */
typedef struct {
    char     name[64];
    uint8_t  is_dir;
    uint32_t size;
} gnu_find_entry_t;

int  gnu_find(const char *path, const char *name_pattern,
              gnu_find_entry_t *results, uint32_t max_results);

/* ======================================================================== */
/* Utility functions                                                        */
/* ======================================================================== */

/* Parse a range string like "1-5,8,10-20" into pairs */
int  gnu_parse_ranges(const char *spec, uint32_t *out, uint32_t max_pairs);

/* String matching helpers */
int  gnu_strcasestr(const char *haystack, const char *needle);
int  gnu_strstr(const char *haystack, const char *needle);
int  gnu_match_simple(const char *text, const char *pattern);

#endif /* GNU_TOOLS_H */
