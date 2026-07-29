/**
 * Chicago-95 GNU Tools Implementation
 * Freestanding GNU/Unix command-line utilities for bare-metal ring-0
 */

#include <stdint.h>
#include <string.h>
#include "shell/gnu_tools.h"

/* ======================================================================== */
/* String Helpers                                                           */
/* ======================================================================== */

static uint32_t gnu_strlen(const char *s) {
    uint32_t len = 0;
    while (s[len]) len++;
    return len;
}

static int gnu_strcmp(const char *a, const char *b) {
    while (*a && *b) { if (*a != *b) return *a - *b; a++; b++; }
    return *a - *b;
}

static int gnu_strncasecmp(const char *a, const char *b, uint32_t n) {
    for (uint32_t i = 0; i < n; i++) {
        char ca = a[i], cb = b[i];
        if (ca >= 'A' && ca <= 'Z') ca += 32;
        if (cb >= 'A' && cb <= 'Z') cb += 32;
        if (ca != cb) return ca - cb;
        if (ca == 0) return 0;
    }
    return 0;
}

static void gnu_memcpy(void *dst, const void *src, uint32_t n) {
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;
    while (n--) *d++ = *s++;
}

static void gnu_memset(void *dst, int v, uint32_t n) {
    uint8_t *d = (uint8_t *)dst;
    while (n--) *d++ = (uint8_t)v;
}

/* ======================================================================== */
/* String Search                                                            */
/* ======================================================================== */

int gnu_strstr(const char *haystack, const char *needle) {
    uint32_t hlen = gnu_strlen(haystack);
    uint32_t nlen = gnu_strlen(needle);
    if (nlen == 0) return 1;
    if (nlen > hlen) return 0;
    for (uint32_t i = 0; i <= hlen - nlen; i++) {
        uint32_t j;
        for (j = 0; j < nlen; j++) {
            if (haystack[i + j] != needle[j]) break;
        }
        if (j == nlen) return 1;
    }
    return 0;
}

int gnu_strcasestr(const char *haystack, const char *needle) {
    uint32_t hlen = gnu_strlen(haystack);
    uint32_t nlen = gnu_strlen(needle);
    if (nlen == 0) return 1;
    if (nlen > hlen) return 0;
    for (uint32_t i = 0; i <= hlen - nlen; i++) {
        if (gnu_strncasecmp(&haystack[i], needle, nlen) == 0) return 1;
    }
    return 0;
}

int gnu_match_simple(const char *text, const char *pattern) {
    while (*pattern) {
        if (*pattern == '*') {
            pattern++;
            if (*pattern == 0) return 1;
            while (*text) {
                if (gnu_match_simple(text, pattern)) return 1;
                text++;
            }
            return 0;
        }
        if (*text == 0) return 0;
        if (*pattern != '?' && *pattern != *text) return 0;
        text++;
        pattern++;
    }
    return *text == 0;
}

/* ======================================================================== */
/* grep - pattern search                                                    */
/* ======================================================================== */

int gnu_grep_line(const char *line, uint32_t line_len,
                  const char *pattern, uint32_t pat_len, uint32_t flags) {
    (void)pat_len;
    int found;

    if (flags & GNU_GREP_IGNORE_CASE) {
        found = gnu_strcasestr(line, pattern) != 0;
    } else {
        found = gnu_strstr(line, pattern) != 0;
    }

    if (flags & GNU_GREP_INVERT) found = !found;
    return found;
}

int gnu_grep(const char *text, uint32_t text_len,
             const char *pattern, uint32_t flags,
             gnu_grep_callback cb, void *userdata) {
    uint32_t line_num = 0;
    uint32_t i = 0;
    uint32_t pat_len = gnu_strlen(pattern);
    int matches = 0;

    while (i < text_len) {
        /* Find end of line */
        uint32_t line_start = i;
        while (i < text_len && text[i] != '\n') i++;
        uint32_t line_len = i - line_start;
        if (i < text_len) i++; /* skip newline */

        line_num++;

        if (gnu_grep_line(&text[line_start], line_len, pattern, pat_len, flags)) {
            if (flags & GNU_GREP_COUNT_ONLY) {
                matches++;
            } else {
                cb(&text[line_start], line_len, line_num, userdata);
                matches++;
            }
        }
    }

    return matches;
}

/* ======================================================================== */
/* sed - stream editor                                                      */
/* ======================================================================== */

int gnu_sed_parse(const char *expr, gnu_sed_cmd_t *cmds, uint32_t max_cmds) {
    uint32_t ncmds = 0;
    uint32_t i = 0;
    uint32_t elen = gnu_strlen(expr);

    while (i < elen && ncmds < max_cmds) {
        /* Skip leading separators */
        while (i < elen && (expr[i] == ' ' || expr[i] == ';')) i++;
        if (i >= elen) break;

        gnu_sed_cmd_t *cmd = &cmds[ncmds];
        gnu_memset(cmd, 0, sizeof(gnu_sed_cmd_t));

        if (expr[i] == 's' && i + 1 < elen && expr[i + 1] == '/') {
            /* Substitution: s/pattern/replacement/g */
            cmd->cmd = 's';
            i += 2; /* skip s/ */

            /* Extract pattern */
            uint32_t pi = 0;
            while (i < elen && expr[i] != '/' && pi < 63) {
                cmd->pattern[pi++] = expr[i++];
            }
            cmd->pattern[pi] = 0;
            if (i < elen) i++; /* skip / */

            /* Extract replacement */
            uint32_t ri = 0;
            while (i < elen && expr[i] != '/' && pi < 63) {
                cmd->replacement[ri++] = expr[i++];
            }
            cmd->replacement[ri] = 0;
            if (i < elen) i++; /* skip / */

            /* Check for flags */
            while (i < elen && expr[i] != ';' && expr[i] != ' ') {
                if (expr[i] == 'g') cmd->global = 1;
                i++;
            }
            ncmds++;
        } else if (expr[i] == 'd') {
            cmd->cmd = 'd';
            i++;
            ncmds++;
        } else if (expr[i] == 'p') {
            cmd->cmd = 'p';
            i++;
            ncmds++;
        } else if (expr[i] == 'q') {
            cmd->cmd = 'q';
            i++;
            ncmds++;
        } else {
            i++; /* skip unknown */
        }
    }

    return (int)ncmds;
}

int gnu_sed_apply(const char *line, uint32_t line_len,
                  const gnu_sed_cmd_t *cmds, uint32_t ncmds,
                  char *out, uint32_t out_max) {
    char work[512];
    if (line_len >= 512) line_len = 511;
    gnu_memcpy(work, line, line_len);
    work[line_len] = 0;
    uint32_t wlen = line_len;

    for (uint32_t c = 0; c < ncmds; c++) {
        const gnu_sed_cmd_t *cmd = &cmds[c];

        if (cmd->cmd == 'd') {
            out[0] = 0;
            return 0;
        }

        if (cmd->cmd == 's') {
            /* Simple find-replace */
            uint32_t plen = gnu_strlen(cmd->pattern);
            uint32_t rlen = gnu_strlen(cmd->replacement);

            if (plen == 0) continue;

            char result[512];
            uint32_t ri = 0;
            uint32_t wi = 0;
            int replaced = 0;

            while (wi < wlen && ri < 511) {
                if (!replaced && wi + plen <= wlen &&
                    gnu_strncasecmp(&work[wi], cmd->pattern, plen) == 0) {
                    /* Replace */
                    for (uint32_t j = 0; j < rlen && ri < 511; j++) {
                        result[ri++] = cmd->replacement[j];
                    }
                    wi += plen;
                    replaced = 1;
                    if (!cmd->global) {
                        /* Copy rest */
                        while (wi < wlen && ri < 511) result[ri++] = work[wi++];
                        break;
                    }
                } else {
                    result[ri++] = work[wi++];
                }
            }
            result[ri] = 0;

            /* Copy back to work */
            wlen = ri;
            for (uint32_t j = 0; j <= wlen; j++) work[j] = result[j];
        }
    }

    /* Copy result */
    uint32_t copy = wlen;
    if (copy >= out_max) copy = out_max - 1;
    gnu_memcpy(out, work, copy);
    out[copy] = 0;
    return (int)copy;
}

/* ======================================================================== */
/* cut - extract fields/columns                                             */
/* ======================================================================== */

int gnu_cut_delimited(const char *line, uint32_t line_len,
                      char delimiter, const uint32_t *fields,
                      uint32_t nfields, char *out, uint32_t out_max) {
    uint32_t oi = 0;
    uint32_t field_num = 0;
    uint32_t field_start = 0;
    uint8_t first = 1;

    for (uint32_t i = 0; i <= line_len; i++) {
        if (i == line_len || line[i] == delimiter) {
            /* Check if this field is in the requested list */
            uint8_t want = 0;
            for (uint32_t f = 0; f < nfields; f++) {
                if (fields[f] == field_num + 1) { want = 1; break; }
            }

            if (want) {
                uint32_t flen = i - field_start;
                if (!first && oi < out_max - 1) {
                    out[oi++] = delimiter;
                }
                for (uint32_t j = 0; j < flen && oi < out_max - 1; j++) {
                    out[oi++] = line[field_start + j];
                }
                first = 0;
            }

            field_start = i + 1;
            field_num++;
            if (i == line_len) break;
        }
    }

    out[oi] = 0;
    return (int)oi;
}

int gnu_cut_chars(const char *line, uint32_t line_len,
                  const uint32_t *ranges, uint32_t nranges,
                  char *out, uint32_t out_max) {
    uint32_t oi = 0;

    for (uint32_t r = 0; r < nranges; r++) {
        uint32_t start = ranges[r * 2];
        uint32_t end = ranges[r * 2 + 1];
        if (start == 0) start = 1;
        if (end == 0 || end > line_len) end = line_len;

        for (uint32_t i = start; i <= end && i <= line_len; i++) {
            if (oi < out_max - 1) {
                out[oi++] = line[i - 1];
            }
        }
    }

    out[oi] = 0;
    return (int)oi;
}

/* ======================================================================== */
/* sort                                                                      */
/* ======================================================================== */

int gnu_sort_add(gnu_sort_buffer_t *buf, const char *line, uint32_t len) {
    if (buf->count >= 32) return -1;
    if (len >= 256) len = 255;
    gnu_memcpy(buf->lines[buf->count], line, len);
    buf->lines[buf->count][len] = 0;
    buf->lengths[buf->count] = len;
    buf->count++;
    return 0;
}

static int gnu_sort_compare(const char *a, const char *b, uint32_t flags) {
    if (flags & GNU_SORT_NUMERIC) {
        /* Parse as integers */
        int32_t na = 0, nb = 0;
        uint32_t i = 0;
        while (a[i] >= '0' && a[i] <= '9') { na = na * 10 + (a[i] - '0'); i++; }
        i = 0;
        while (b[i] >= '0' && b[i] <= '9') { nb = nb * 10 + (b[i] - '0'); i++; }
        if (flags & GNU_SORT_REVERSE) return (nb > na) ? -1 : (nb < na) ? 1 : 0;
        return (na > nb) ? 1 : (na < nb) ? -1 : 0;
    }

    /* String comparison */
    const char *p = a, *q = b;
    while (*p && *q) {
        char ca = *p, cb = *q;
        if (flags & GNU_SORT_CASE_FOLD) {
            if (ca >= 'A' && ca <= 'Z') ca += 32;
            if (cb >= 'A' && cb <= 'Z') cb += 32;
        }
        if (ca != cb) {
            if (flags & GNU_SORT_REVERSE) return (cb > ca) ? -1 : 1;
            return (ca > cb) ? 1 : -1;
        }
        p++; q++;
    }
    if (flags & GNU_SORT_REVERSE) return (*q > *p) ? -1 : (*q < *p) ? 1 : 0;
    return (*p > *q) ? 1 : (*p < *q) ? -1 : 0;
}

int gnu_sort_sort(gnu_sort_buffer_t *buf, uint32_t flags) {
    /* Simple insertion sort (good enough for 32 lines) */
    for (uint32_t i = 1; i < buf->count; i++) {
        char tmp_line[256];
        uint32_t tmp_len = buf->lengths[i];
        gnu_memcpy(tmp_line, buf->lines[i], tmp_len + 1);

        int j = (int)i - 1;
        while (j >= 0 && gnu_sort_compare(tmp_line, buf->lines[j], flags) < 0) {
            gnu_memcpy(buf->lines[j + 1], buf->lines[j], buf->lengths[j] + 1);
            buf->lengths[j + 1] = buf->lengths[j];
            j--;
        }
        gnu_memcpy(buf->lines[j + 1], tmp_line, tmp_len + 1);
        buf->lengths[j + 1] = tmp_len;
    }

    /* Remove duplicates if requested */
    if (flags & GNU_SORT_UNIQUE) {
        uint32_t write = 0;
        for (uint32_t i = 0; i < buf->count; i++) {
            if (i == 0 || gnu_strcmp(buf->lines[i], buf->lines[i - 1]) != 0) {
                if (write != i) {
                    gnu_memcpy(buf->lines[write], buf->lines[i], buf->lengths[i] + 1);
                    buf->lengths[write] = buf->lengths[i];
                }
                write++;
            }
        }
        buf->count = write;
    }

    return 0;
}

/* ======================================================================== */
/* uniq - filter unique/repeated lines                                      */
/* ======================================================================== */

int gnu_uniq_filter(const char *lines, uint32_t lines_len,
                    char *out, uint32_t out_max, uint8_t count_repeats) {
    uint32_t oi = 0;
    uint32_t i = 0;
    char prev[256];
    uint32_t prev_len = 0;
    uint32_t repeat_count = 0;
    uint8_t first_line = 1;

    while (i < lines_len) {
        /* Get current line */
        uint32_t start = i;
        while (i < lines_len && lines[i] != '\n') i++;
        uint32_t len = i - start;
        if (i < lines_len) i++;

        /* Compare with previous */
        int same = 0;
        if (!first_line && len == prev_len) {
            same = 1;
            for (uint32_t j = 0; j < len; j++) {
                if (lines[start + j] != prev[j]) { same = 0; break; }
            }
        }

        if (same) {
            repeat_count++;
        } else {
            /* Output previous line if it had repeats or count_repeats */
            if (!first_line && (repeat_count > 0 || count_repeats)) {
                if (count_repeats) {
                    /* Print count */
                    char num[16]; int ni = 0;
                    uint32_t cnt = repeat_count + 1;
                    if (cnt == 0) { num[ni++] = '0'; }
                    else {
                        char rev[16]; int ri = 0;
                        while (cnt) { rev[ri++] = '0' + (cnt % 10); cnt /= 10; }
                        while (ri > 0) num[ni++] = rev[--ri];
                    }
                    /* Pad to 4 chars */
                    while (ni < 4) {
                        for (int k = ni; k > 0; k--) num[k] = num[k-1];
                        num[0] = ' ';
                        ni++;
                    }
                    num[ni] = 0;
                    for (uint32_t k = 0; num[k] && oi < out_max - 1; k++)
                        out[oi++] = num[k];
                    out[oi++] = ' ';
                }
                for (uint32_t k = 0; k < prev_len && oi < out_max - 1; k++)
                    out[oi++] = prev[k];
                if (oi < out_max - 1) out[oi++] = '\n';
            } else if (!first_line) {
                /* Normal unique: output if different */
                for (uint32_t k = 0; k < prev_len && oi < out_max - 1; k++)
                    out[oi++] = prev[k];
                if (oi < out_max - 1) out[oi++] = '\n';
            }

            /* Save current as previous */
            prev_len = len;
            if (len >= 256) len = 255;
            gnu_memcpy(prev, &lines[start], len);
            prev[len] = 0;
            repeat_count = 0;
            first_line = 0;
        }
    }

    /* Output last line */
    if (!first_line) {
        for (uint32_t k = 0; k < prev_len && oi < out_max - 1; k++)
            out[oi++] = prev[k];
        if (oi < out_max - 1) out[oi++] = '\n';
    }

    out[oi] = 0;
    return (int)oi;
}

/* ======================================================================== */
/* wc - word/line/byte count                                                */
/* ======================================================================== */

void gnu_wc(const char *text, uint32_t len, gnu_wc_result_t *result) {
    gnu_memset(result, 0, sizeof(gnu_wc_result_t));
    uint32_t line_len = 0;

    for (uint32_t i = 0; i < len; i++) {
        result->bytes++;
        result->chars++;

        if (text[i] == '\n') {
            result->lines++;
            if (line_len > result->max_line_len) result->max_line_len = line_len;
            line_len = 0;
        } else {
            line_len++;
        }

        if (text[i] == ' ' || text[i] == '\t' || text[i] == '\n') {
            /* End of word */
        } else if (i == 0 || text[i - 1] == ' ' || text[i - 1] == '\t' || text[i - 1] == '\n') {
            result->words++;
        }
    }

    /* Handle last line without newline */
    if (len > 0 && text[len - 1] != '\n') {
        result->lines++;
        if (line_len > result->max_line_len) result->max_line_len = line_len;
    }
}

/* ======================================================================== */
/* head/tail                                                                */
/* ======================================================================== */

int gnu_head(const char *text, uint32_t len, uint32_t nlines,
             char *out, uint32_t out_max) {
    uint32_t oi = 0;
    uint32_t lines = 0;

    for (uint32_t i = 0; i < len && lines < nlines && oi < out_max - 1; i++) {
        out[oi++] = text[i];
        if (text[i] == '\n') lines++;
    }

    out[oi] = 0;
    return (int)oi;
}

int gnu_tail(const char *text, uint32_t len, uint32_t nlines,
             char *out, uint32_t out_max) {
    /* Count total lines first */
    uint32_t total = 0;
    for (uint32_t i = 0; i < len; i++) {
        if (text[i] == '\n') total++;
    }
    if (len > 0 && text[len - 1] != '\n') total++;

    uint32_t skip = (total > nlines) ? total - nlines : 0;
    uint32_t lines_seen = 0;
    uint32_t start = 0;

    for (uint32_t i = 0; i < len; i++) {
        if (text[i] == '\n') {
            lines_seen++;
            if (lines_seen > skip) {
                start = i + 1;
            }
        }
    }

    /* Copy from start to end */
    uint32_t remaining = len - start;
    if (remaining >= out_max) remaining = out_max - 1;
    gnu_memcpy(out, &text[start], remaining);
    out[remaining] = 0;
    return (int)remaining;
}

/* ======================================================================== */
/* tr - translate characters                                                */
/* ======================================================================== */

int gnu_tr(const char *input, uint32_t input_len,
           const char *from, const char *to,
           char *output, uint32_t output_max) {
    uint32_t flen = gnu_strlen(from);
    uint32_t oi = 0;

    for (uint32_t i = 0; i < input_len && oi < output_max - 1; i++) {
        char c = input[i];
        uint8_t found = 0;
        for (uint32_t j = 0; j < flen; j++) {
            if (c == from[j]) {
                output[oi++] = to[j];
                found = 1;
                break;
            }
        }
        if (!found) output[oi++] = c;
    }
    output[oi] = 0;
    return (int)oi;
}

int gnu_tr_delete(const char *input, uint32_t input_len,
                  const char *chars,
                  char *output, uint32_t output_max) {
    uint32_t clen = gnu_strlen(chars);
    uint32_t oi = 0;

    for (uint32_t i = 0; i < input_len && oi < output_max - 1; i++) {
        uint8_t del = 0;
        for (uint32_t j = 0; j < clen; j++) {
            if (input[i] == chars[j]) { del = 1; break; }
        }
        if (!del) output[oi++] = input[i];
    }
    output[oi] = 0;
    return (int)oi;
}

int gnu_tr_squeeze(const char *input, uint32_t input_len,
                   const char *chars,
                   char *output, uint32_t output_max) {
    uint32_t clen = gnu_strlen(chars);
    uint32_t oi = 0;
    char prev = 0;

    for (uint32_t i = 0; i < input_len && oi < output_max - 1; i++) {
        uint8_t is_char = 0;
        for (uint32_t j = 0; j < clen; j++) {
            if (input[i] == chars[j]) { is_char = 1; break; }
        }

        if (is_char && input[i] == prev) {
            continue; /* Squeeze repeat */
        }
        output[oi++] = input[i];
        prev = input[i];
    }
    output[oi] = 0;
    return (int)oi;
}

/* ======================================================================== */
/* diff - compare two texts                                                 */
/* ======================================================================== */

int gnu_diff(const char *text1, uint32_t len1,
             const char *text2, uint32_t len2,
             gnu_diff_line_t *results, uint32_t max_results) {
    uint32_t ri = 0;
    uint32_t l1 = 0, l2 = 0;
    uint32_t line = 0;

    /* Simple line-by-line comparison */
    while (l1 < len1 || l2 < len2) {
        line++;

        /* Get line from text1 */
        char line_a[256]; uint32_t la = 0;
        while (l1 < len1 && text1[l1] != '\n' && la < 255) {
            line_a[la++] = text1[l1++];
        }
        line_a[la] = 0;
        if (l1 < len1) l1++;

        /* Get line from text2 */
        char line_b[256]; uint32_t lb = 0;
        while (l2 < len2 && text2[l2] != '\n' && lb < 255) {
            line_b[lb++] = text2[l2++];
        }
        line_b[lb] = 0;
        if (l2 < len2) l2++;

        if (gnu_strcmp(line_a, line_b) != 0) {
            if (ri < max_results) {
                results[ri].line = line;
                results[ri].type = '<';
                gnu_memcpy(results[ri].text, line_a, la + 1);
                ri++;
            }
            if (ri < max_results) {
                results[ri].line = line;
                results[ri].type = '>';
                gnu_memcpy(results[ri].text, line_b, lb + 1);
                ri++;
            }
        }
    }

    return (int)ri;
}

/* ======================================================================== */
/* base64 - encode/decode                                                   */
/* ======================================================================== */

static const char b64_table[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

int gnu_base64_encode(const uint8_t *data, uint32_t len,
                      char *out, uint32_t out_max) {
    uint32_t oi = 0;
    uint32_t i;

    for (i = 0; i + 2 < len; i += 3) {
        if (oi + 4 >= out_max) break;
        uint32_t n = ((uint32_t)data[i] << 16) |
                     ((uint32_t)data[i + 1] << 8) |
                     (uint32_t)data[i + 2];
        out[oi++] = b64_table[(n >> 18) & 0x3F];
        out[oi++] = b64_table[(n >> 12) & 0x3F];
        out[oi++] = b64_table[(n >> 6) & 0x3F];
        out[oi++] = b64_table[n & 0x3F];
    }

    if (i < len) {
        uint32_t remaining = len - i;
        uint32_t n = (uint32_t)data[i] << 16;
        if (remaining > 1) n |= (uint32_t)data[i + 1] << 8;

        if (oi + 4 < out_max) {
            out[oi++] = b64_table[(n >> 18) & 0x3F];
            out[oi++] = b64_table[(n >> 12) & 0x3F];
            out[oi++] = (remaining > 1) ? b64_table[(n >> 6) & 0x3F] : '=';
            out[oi++] = '=';
        }
    }

    out[oi] = 0;
    return (int)oi;
}

int gnu_base64_decode(const char *encoded, uint32_t len,
                      uint8_t *out, uint32_t out_max) {
    uint32_t oi = 0;
    uint32_t buf = 0;
    int bits = 0;

    for (uint32_t i = 0; i < len; i++) {
        char c = encoded[i];
        if (c == '=' || c == '\n' || c == '\r' || c == ' ') continue;

        int val = -1;
        if (c >= 'A' && c <= 'Z') val = c - 'A';
        else if (c >= 'a' && c <= 'z') val = c - 'a' + 26;
        else if (c >= '0' && c <= '9') val = c - '0' + 52;
        else if (c == '+') val = 62;
        else if (c == '/') val = 63;

        if (val < 0) continue;

        buf = (buf << 6) | val;
        bits += 6;

        if (bits >= 8) {
            bits -= 8;
            if (oi < out_max) {
                out[oi++] = (uint8_t)((buf >> bits) & 0xFF);
            }
        }
    }

    return (int)oi;
}

/* ======================================================================== */
/* find - search (bare-metal BrainFS)                                       */
/* ======================================================================== */

int gnu_find(const char *path, const char *name_pattern,
             gnu_find_entry_t *results, uint32_t max_results) {
    /* Stub: In bare-metal, we can't do recursive directory traversal
       without knowing the VFS API fully. Return empty for now. */
    (void)path; (void)name_pattern;
    return 0;
}

/* ======================================================================== */
/* Range parsing                                                            */
/* ======================================================================== */

int gnu_parse_ranges(const char *spec, uint32_t *out, uint32_t max_pairs) {
    uint32_t pi = 0;
    uint32_t i = 0;
    uint32_t slen = gnu_strlen(spec);

    while (i < slen && pi + 1 < max_pairs) {
        /* Skip commas */
        while (i < slen && spec[i] == ',') i++;
        if (i >= slen) break;

        /* Parse start */
        uint32_t start = 0;
        while (i < slen && spec[i] >= '0' && spec[i] <= '9') {
            start = start * 10 + (spec[i] - '0');
            i++;
        }

        uint32_t end = start;

        if (i < slen && spec[i] == '-') {
            i++; /* skip dash */
            end = 0;
            while (i < slen && spec[i] >= '0' && spec[i] <= '9') {
                end = end * 10 + (spec[i] - '0');
                i++;
            }
        }

        out[pi++] = start;
        out[pi++] = end;
    }

    return (int)(pi / 2);
}
