/**
 * Chicago-95 AWK Implementation
 * Freestanding pattern scanning and processing language for bare-metal ring-0
 */

#include <stdint.h>
#include "shell/awk.h"
#include "vga/vga.h"

/* ======================================================================== */
/* String Helpers                                                            */
/* ======================================================================== */

static uint32_t awks_strlen(const char *s) {
    uint32_t len = 0;
    while (s[len]) len++;
    return len;
}

static int awks_strcmp(const char *a, const char *b) {
    while (*a && *b) { if (*a != *b) return *a - *b; a++; b++; }
    return *a - *b;
}

static int awks_strncmp(const char *a, const char *b, uint32_t n) {
    for (uint32_t i = 0; i < n; i++) {
        if (a[i] != b[i]) return a[i] - b[i];
        if (a[i] == 0) return 0;
    }
    return 0;
}

static void awks_strcpy(char *dst, const char *src, uint32_t max) {
    uint32_t i = 0;
    while (src[i] && i < max - 1) { dst[i] = src[i]; i++; }
    dst[i] = 0;
}

static void awks_memcpy(void *dst, const void *src, uint32_t n) {
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;
    while (n--) *d++ = *s++;
}

static void awks_memset(void *dst, int v, uint32_t n) {
    uint8_t *d = (uint8_t *)dst;
    while (n--) *d++ = (uint8_t)v;
}

/* Check if character is whitespace */
static int awks_isspace(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

/* Check if character is digit */
static int awks_isdigit(char c) {
    return c >= '0' && c <= '9';
}

/* Parse integer from string */
static int32_t awks_atoi(const char *s) {
    int32_t val = 0;
    int neg = 0;
    if (*s == '-') { neg = 1; s++; }
    while (awks_isdigit(*s)) { val = val * 10 + (*s - '0'); s++; }
    return neg ? -val : val;
}

/* Convert int to string */
static void awks_itoa(int32_t val, char *out, uint32_t max) {
    (void)max;
    int ni = 0;
    if (val < 0) { out[ni++] = '-'; val = -val; }
    if (val == 0) { out[ni++] = '0'; }
    else {
        char rev[16]; int ri = 0;
        while (val) { rev[ri++] = '0' + (val % 10); val /= 10; }
        while (ri > 0) out[ni++] = rev[--ri];
    }
    out[ni] = 0;
}

/* ======================================================================== */
/* Regex Engine                                                              */
/* ======================================================================== */

/* Find closing bracket for character class */
static int rgx_find_class_end(const char *p, uint32_t len) {
    for (uint32_t i = 1; i < len; i++) {
        if (p[i] == ']' && i > 1) return (int)i;
    }
    return -1;
}

int rgx_compile(rgx_compiled_t *out, const char *pattern, uint32_t pat_len) {
    awks_memset(out, 0, sizeof(rgx_compiled_t));
    uint32_t pos = 0;

    while (pos < pat_len && out->count < 64) {
        rgx_node_t *node = &out->nodes[out->count];

        if (pattern[pos] == '\\') {
            /* Escaped character */
            pos++;
            if (pos < pat_len) {
                node->type = RGX_LITERAL;
                node->literal = pattern[pos];
                out->count++;
                pos++;
            }
        } else if (pattern[pos] == '^') {
            node->type = RGX_ANCHOR_START;
            out->count++;
            pos++;
        } else if (pattern[pos] == '$') {
            node->type = RGX_ANCHOR_END;
            out->count++;
            pos++;
        } else if (pattern[pos] == '.') {
            node->type = RGX_DOT;
            out->count++;
            pos++;
        } else if (pattern[pos] == '*') {
            if (out->count > 0) {
                out->nodes[out->count - 1].type |= 0x80; /* Mark as quantified */
            }
            node->type = RGX_STAR;
            out->count++;
            pos++;
        } else if (pattern[pos] == '+') {
            if (out->count > 0) {
                out->nodes[out->count - 1].type |= 0x80;
            }
            node->type = RGX_PLUS;
            out->count++;
            pos++;
        } else if (pattern[pos] == '?') {
            if (out->count > 0) {
                out->nodes[out->count - 1].type |= 0x80;
            }
            node->type = RGX_QUESTION;
            out->count++;
            pos++;
        } else if (pattern[pos] == '[') {
            /* Character class */
            int end = rgx_find_class_end(&pattern[pos], pat_len - pos);
            if (end > 0) {
                node->type = RGX_CHAR_CLASS;
                uint32_t ci = 0;
                uint32_t start = 1;
                if (pattern[pos + 1] == '^') {
                    node->negated_class = 1;
                    start = 2;
                }
                for (uint32_t k = start; k < (uint32_t)end && ci < 63; k++) {
                    node->char_class[ci++] = pattern[pos + k];
                }
                node->char_class[ci] = 0;
                out->count++;
                pos += end + 1;
            } else {
                /* Malformed class - treat as literal */
                node->type = RGX_LITERAL;
                node->literal = pattern[pos];
                out->count++;
                pos++;
            }
        } else if (pattern[pos] == '(') {
            node->type = RGX_GROUP_OPEN;
            out->count++;
            pos++;
        } else if (pattern[pos] == ')') {
            node->type = RGX_GROUP_CLOSE;
            out->count++;
            pos++;
        } else if (pattern[pos] == '|') {
            node->type = RGX_ALT;
            out->count++;
            pos++;
        } else {
            node->type = RGX_LITERAL;
            node->literal = pattern[pos];
            out->count++;
            pos++;
        }
    }

    out->valid = 1;
    return 0;
}

/* Internal: recursive match check */
static int rgx_match_at(const rgx_compiled_t *rx, int node_idx,
                         const char *text, uint32_t text_pos, uint32_t text_len) {
    if (node_idx >= rx->count) return 1; /* All nodes consumed = match */

    const rgx_node_t *node = &rx->nodes[node_idx];

    /* Handle quantifiers (star/plus/question) */
    if (node->type == RGX_STAR) {
        /* Get the preceding node type */
        if (node_idx == 0) return 0;
        const rgx_node_t *prev = &rx->nodes[node_idx - 1];
        /* Try matching 0 or more of prev */
        for (uint32_t skip = 0; ; skip++) {
            if (text_pos + skip > text_len) {
                /* Try matching rest from this position */
                if (rgx_match_at(rx, node_idx + 1, text, text_pos + skip - 1, text_len))
                    return 1;
                return 0;
            }
            int match = 0;
            char c = text[text_pos + skip];
            if (prev->type == RGX_LITERAL) match = (c == prev->literal);
            else if (prev->type == RGX_DOT) match = (c != '\n');
            else if (prev->type == RGX_CHAR_CLASS) {
                match = 0;
                for (uint32_t k = 0; prev->char_class[k]; k++) {
                    if (c == prev->char_class[k]) { match = 1; break; }
                }
                if (prev->negated_class) match = !match;
            }
            if (!match) {
                return rgx_match_at(rx, node_idx + 1, text, text_pos + skip, text_len);
            }
        }
    }

    if (node->type == RGX_PLUS) {
        if (node_idx == 0) return 0;
        const rgx_node_t *prev = &rx->nodes[node_idx - 1];
        int found = 0;
        for (uint32_t skip = 0; text_pos + skip < text_len; skip++) {
            char c = text[text_pos + skip];
            int match = 0;
            if (prev->type == RGX_LITERAL) match = (c == prev->literal);
            else if (prev->type == RGX_DOT) match = (c != '\n');
            else if (prev->type == RGX_CHAR_CLASS) {
                for (uint32_t k = 0; prev->char_class[k]; k++) {
                    if (c == prev->char_class[k]) { match = 1; break; }
                }
                if (prev->negated_class) match = !match;
            }
            if (!match) {
                if (found) return rgx_match_at(rx, node_idx + 1, text, text_pos + skip, text_len);
                return 0;
            }
            found = 1;
        }
        if (found) return rgx_match_at(rx, node_idx + 1, text, text_pos + (text_len - text_pos), text_len);
        return 0;
    }

    if (node->type == RGX_QUESTION) {
        if (node_idx == 0) return 0;
        const rgx_node_t *prev = &rx->nodes[node_idx - 1];
        if (text_pos < text_len) {
            char c = text[text_pos];
            int match = 0;
            if (prev->type == RGX_LITERAL) match = (c == prev->literal);
            else if (prev->type == RGX_DOT) match = (c != '\n');
            else if (prev->type == RGX_CHAR_CLASS) {
                for (uint32_t k = 0; prev->char_class[k]; k++) {
                    if (c == prev->char_class[k]) { match = 1; break; }
                }
                if (prev->negated_class) match = !match;
            }
            if (match) {
                if (rgx_match_at(rx, node_idx + 1, text, text_pos + 1, text_len)) return 1;
            }
        }
        return rgx_match_at(rx, node_idx + 1, text, text_pos, text_len);
    }

    /* Handle remaining node types */
    if (text_pos >= text_len) return 0;

    char c = text[text_pos];

    switch (node->type & 0x7F) { /* Mask quantifier bit */
    case RGX_LITERAL:
        if (c == node->literal)
            return rgx_match_at(rx, node_idx + 1, text, text_pos + 1, text_len);
        return 0;

    case RGX_DOT:
        if (c != '\n')
            return rgx_match_at(rx, node_idx + 1, text, text_pos + 1, text_len);
        return 0;

    case RGX_CHAR_CLASS: {
        int found = 0;
        for (uint32_t k = 0; node->char_class[k]; k++) {
            if (c == node->char_class[k]) { found = 1; break; }
        }
        if (node->negated_class) found = !found;
        if (found)
            return rgx_match_at(rx, node_idx + 1, text, text_pos + 1, text_len);
        return 0;
    }

    case RGX_ANCHOR_START:
        if (text_pos == 0)
            return rgx_match_at(rx, node_idx + 1, text, text_pos, text_len);
        return 0;

    case RGX_ANCHOR_END:
        if (text_pos >= text_len || text[text_pos] == '\n' || text[text_pos] == 0)
            return rgx_match_at(rx, node_idx + 1, text, text_pos, text_len);
        return 0;

    default:
        return 0;
    }
}

int rgx_match(const rgx_compiled_t *rx, const char *text, uint32_t text_len) {
    if (!rx->valid || rx->count == 0) return 0;

    /* Check for ^ anchor - match at start only */
    if (rx->nodes[0].type == RGX_ANCHOR_START) {
        return rgx_match_at(rx, 1, text, 0, text_len);
    }

    /* Try matching at each position */
    for (uint32_t i = 0; i <= text_len; i++) {
        if (rgx_match_at(rx, 0, text, i, text_len))
            return 1;
    }
    return 0;
}

int rgx_find(const rgx_compiled_t *rx, const char *text, uint32_t text_len) {
    if (!rx->valid || rx->count == 0) return -1;

    if (rx->nodes[0].type == RGX_ANCHOR_START) {
        if (rgx_match_at(rx, 1, text, 0, text_len)) return 0;
        return -1;
    }

    for (uint32_t i = 0; i < text_len; i++) {
        if (rgx_match_at(rx, 0, text, i, text_len))
            return (int)i;
    }
    return -1;
}

/* ======================================================================== */
/* VGA Output Helpers                                                        */
/* ======================================================================== */

void awk_print_string(const char *s) {
    while (*s) {
        vga_text_put_char(*s, VGA_COLOR(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
        s++;
    }
}

void awk_print_char(char c) {
    vga_text_put_char(c, VGA_COLOR(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
}

void awk_print_num(int32_t num) {
    char buf[16];
    awks_itoa(num, buf, 16);
    awk_print_string(buf);
}

void awk_newline(void) {
    vga_text_put_char('\r', VGA_COLOR(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
    vga_text_put_char('\n', VGA_COLOR(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
}

/* ======================================================================== */
/* AWK Variable Management                                                   */
/* ======================================================================== */

void awk_var_set(awk_program_t *prog, const char *name, const char *str_val,
                 int32_t num_val, uint8_t is_num) {
    /* Find existing */
    for (int i = 0; i < prog->var_count; i++) {
        if (awks_strcmp(prog->vars[i].name, name) == 0) {
            if (is_num) {
                prog->vars[i].num_val = num_val;
                awks_itoa(num_val, prog->vars[i].str_val, 64);
            } else {
                awks_strcpy(prog->vars[i].str_val, str_val ? str_val : "", 64);
                prog->vars[i].num_val = awks_atoi(prog->vars[i].str_val);
            }
            prog->vars[i].is_num = is_num;
            return;
        }
    }
    /* Add new */
    if (prog->var_count >= AWK_MAX_VARS) return;
    awks_strcpy(prog->vars[prog->var_count].name, name, 32);
    if (is_num) {
        prog->vars[prog->var_count].num_val = num_val;
        awks_itoa(num_val, prog->vars[prog->var_count].str_val, 64);
    } else {
        awks_strcpy(prog->vars[prog->var_count].str_val, str_val ? str_val : "", 64);
        prog->vars[prog->var_count].num_val = awks_atoi(str_val ? str_val : "0");
    }
    prog->vars[prog->var_count].is_num = is_num;
    prog->var_count++;
}

int32_t awk_var_get_num(awk_program_t *prog, const char *name) {
    if (awks_strcmp(name, "NR") == 0) return prog->nr;
    if (awks_strcmp(name, "NF") == 0) return prog->nf;
    if (awks_strcmp(name, "FNR") == 0) return prog->nr;
    for (int i = 0; i < prog->var_count; i++) {
        if (awks_strcmp(prog->vars[i].name, name) == 0)
            return prog->vars[i].num_val;
    }
    return 0;
}

const char *awk_var_get_str(awk_program_t *prog, const char *name) {
    if (awks_strcmp(name, "NR") == 0) {
        static char nr_buf[16];
        awks_itoa(prog->nr, nr_buf, 16);
        return nr_buf;
    }
    if (awks_strcmp(name, "NF") == 0) {
        static char nf_buf[16];
        awks_itoa(prog->nf, nf_buf, 16);
        return nf_buf;
    }
    if (awks_strcmp(name, "FS") == 0) return prog->fs;
    if (awks_strcmp(name, "OFS") == 0) return prog->ofs;
    if (awks_strcmp(name, "RS") == 0) return prog->rs;
    if (awks_strcmp(name, "FILENAME") == 0) return prog->filename;
    for (int i = 0; i < prog->var_count; i++) {
        if (awks_strcmp(prog->vars[i].name, name) == 0)
            return prog->vars[i].str_val;
    }
    return "";
}

uint8_t awk_var_is_num(awk_program_t *prog, const char *name) {
    if (awks_strcmp(name, "NR") == 0) return 1;
    if (awks_strcmp(name, "NF") == 0) return 1;
    for (int i = 0; i < prog->var_count; i++) {
        if (awks_strcmp(prog->vars[i].name, name) == 0)
            return prog->vars[i].is_num;
    }
    return 0;
}

/* ======================================================================== */
/* Field Splitting                                                           */
/* ======================================================================== */

static char awk_fields[AWK_MAX_FIELDS][AWK_MAX_FIELD_LEN];
static int  awk_field_lens[AWK_MAX_FIELDS];

void awk_split_fields(awk_program_t *prog, const char *line, uint32_t line_len) {
    prog->nf = 0;

    /* $0 = entire line */
    uint32_t copy_len = line_len;
    if (copy_len >= AWK_MAX_FIELD_LEN) copy_len = AWK_MAX_FIELD_LEN - 1;
    awks_memcpy(awk_fields[0], line, copy_len);
    awk_fields[0][copy_len] = 0;
    awk_field_lens[0] = (int)copy_len;

    if (awks_strlen(prog->fs) == 0) {
        /* Default FS: whitespace (like awk default) */
        uint32_t start = 0;
        while (start < line_len) {
            /* Skip whitespace */
            while (start < line_len && awks_isspace(line[start])) start++;
            if (start >= line_len) break;

            /* Find end of field */
            uint32_t end = start;
            while (end < line_len && !awks_isspace(line[end])) end++;

            if (prog->nf >= AWK_MAX_FIELDS - 1) break;
            uint32_t flen = end - start;
            if (flen >= AWK_MAX_FIELD_LEN) flen = AWK_MAX_FIELD_LEN - 1;
            awks_memcpy(awk_fields[prog->nf + 1], &line[start], flen);
            awk_fields[prog->nf + 1][flen] = 0;
            awk_field_lens[prog->nf + 1] = (int)flen;
            prog->nf++;
            start = end;
        }
    } else if (awks_strlen(prog->fs) == 1) {
        /* Single character FS */
        char delim = prog->fs[0];
        uint32_t start = 0;
        for (uint32_t i = 0; i <= line_len; i++) {
            if (i == line_len || line[i] == delim) {
                if (prog->nf >= AWK_MAX_FIELDS - 1) break;
                uint32_t flen = i - start;
                if (flen >= AWK_MAX_FIELD_LEN) flen = AWK_MAX_FIELD_LEN - 1;
                awks_memcpy(awk_fields[prog->nf + 1], &line[start], flen);
                awk_fields[prog->nf + 1][flen] = 0;
                awk_field_lens[prog->nf + 1] = (int)flen;
                prog->nf++;
                start = i + 1;
            }
        }
    } else {
        /* Multi-character FS - use as regex */
        rgx_compiled_t fs_rx;
        rgx_compile(&fs_rx, prog->fs, awks_strlen(prog->fs));
        uint32_t start = 0;
        while (start < line_len) {
            int match_pos = rgx_find(&fs_rx, &line[start], line_len - start);
            if (match_pos < 0) {
                /* Rest of line is last field */
                if (prog->nf >= AWK_MAX_FIELDS - 1) break;
                uint32_t flen = line_len - start;
                if (flen >= AWK_MAX_FIELD_LEN) flen = AWK_MAX_FIELD_LEN - 1;
                awks_memcpy(awk_fields[prog->nf + 1], &line[start], flen);
                awk_fields[prog->nf + 1][flen] = 0;
                awk_field_lens[prog->nf + 1] = (int)flen;
                prog->nf++;
                break;
            }
            /* Field before the separator */
            if (prog->nf >= AWK_MAX_FIELDS - 1) break;
            uint32_t flen = (uint32_t)match_pos;
            if (flen >= AWK_MAX_FIELD_LEN) flen = AWK_MAX_FIELD_LEN - 1;
            awks_memcpy(awk_fields[prog->nf + 1], &line[start], flen);
            awk_fields[prog->nf + 1][flen] = 0;
            awk_field_lens[prog->nf + 1] = (int)flen;
            prog->nf++;
            start += match_pos + 1;
        }
    }
}

const char *awk_get_field(awk_program_t *prog, int32_t index) {
    if (index < 0 || index > prog->nf) return "";
    return awk_fields[index];
}

/* ======================================================================== */
/* AWK Expression Parser                                                     */
/* ======================================================================== */

/* Parser state */
typedef struct {
    const char *input;
    uint32_t pos;
    uint32_t len;
    awk_program_t *prog;
} awk_parse_state_t;

/* Skip whitespace */
static void awk_skip_ws(awk_parse_state_t *st) {
    while (st->pos < st->len && awks_isspace(st->input[st->pos]))
        st->pos++;
}

/* Peek current char */
static char awk_peek(awk_parse_state_t *st) {
    if (st->pos >= st->len) return 0;
    return st->input[st->pos];
}

/* Consume a char */
static char awk_consume(awk_parse_state_t *st) {
    if (st->pos >= st->len) return 0;
    return st->input[st->pos++];
}

/* Check if string matches at current position */
static int awk_match_str(awk_parse_state_t *st, const char *s) {
    uint32_t slen = awks_strlen(s);
    if (st->pos + slen > st->len) return 0;
    return awks_strncmp(&st->input[st->pos], s, slen) == 0;
}

/* Parse a number */
static int32_t awk_parse_number(awk_parse_state_t *st) {
    int32_t val = 0;
    int neg = 0;
    if (awk_peek(st) == '-') { neg = 1; st->pos++; }
    while (st->pos < st->len && awks_isdigit(st->input[st->pos])) {
        val = val * 10 + (st->input[st->pos] - '0');
        st->pos++;
    }
    return neg ? -val : val;
}

/* Parse a string literal (quoted) */
static void awk_parse_string_literal(awk_parse_state_t *st, char *out, uint32_t max) {
    char quote = awk_consume(st);
    uint32_t oi = 0;
    while (st->pos < st->len && st->input[st->pos] != quote && oi < max - 1) {
        if (st->input[st->pos] == '\\') {
            st->pos++;
            if (st->pos < st->len) {
                char esc = st->input[st->pos];
                if (esc == 'n') out[oi++] = '\n';
                else if (esc == 't') out[oi++] = '\t';
                else if (esc == '\\') out[oi++] = '\\';
                else if (esc == '"') out[oi++] = '"';
                else if (esc == '\'') out[oi++] = '\'';
                else out[oi++] = esc;
                st->pos++;
            }
        } else {
            out[oi++] = st->input[st->pos++];
        }
    }
    if (st->pos < st->len) st->pos++; /* skip closing quote */
    out[oi] = 0;
}

/* Parse a variable or keyword name */
static void awk_parse_name(awk_parse_state_t *st, char *out, uint32_t max) {
    uint32_t oi = 0;
    while (st->pos < st->len && oi < max - 1) {
        char c = st->input[st->pos];
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == '_') {
            out[oi++] = c;
            st->pos++;
        } else break;
    }
    out[oi] = 0;
}

/* Forward declarations */
static awk_expr_t *awk_parse_expr(awk_parse_state_t *st);
static awk_stmt_t *awk_parse_stmt(awk_parse_state_t *st);

/* Parse primary expression */
static awk_expr_t *awk_parse_primary(awk_parse_state_t *st) {
    awk_skip_ws(st);
    awk_expr_t *expr = (awk_expr_t *)0;  /* Use NULL equivalent */

    /* Number literal */
    if (awks_isdigit(awk_peek(st)) || (awk_peek(st) == '-' && st->pos + 1 < st->len && awks_isdigit(st->input[st->pos + 1]))) {
        /* Allocate from a static pool */
        static awk_expr_t expr_pool[64];
        static int expr_pool_idx = 0;
        expr = &expr_pool[expr_pool_idx++ & 63];
        awks_memset(expr, 0, sizeof(awk_expr_t));
        expr->type = AWK_EXPR_NUMBER;
        expr->num_value = awk_parse_number(st);
        return expr;
    }

    /* String literal */
    if (awk_peek(st) == '"' || awk_peek(st) == '\'') {
        static awk_expr_t expr_pool2[64];
        static int expr_pool2_idx = 0;
        expr = &expr_pool2[expr_pool2_idx++ & 63];
        awks_memset(expr, 0, sizeof(awk_expr_t));
        expr->type = AWK_EXPR_STRING;
        awk_parse_string_literal(st, expr->str_value, 64);
        return expr;
    }

    /* $N - field reference */
    if (awk_peek(st) == '$') {
        st->pos++;
        awk_skip_ws(st);
        static awk_expr_t expr_pool3[64];
        static int expr_pool3_idx = 0;
        expr = &expr_pool3[expr_pool3_idx++ & 63];
        awks_memset(expr, 0, sizeof(awk_expr_t));

        if (awk_peek(st) == '0') {
            st->pos++;
            expr->type = AWK_EXPR_DOLLAR0;
        } else if (awks_isdigit(awk_peek(st))) {
            expr->type = AWK_EXPR_FIELD;
            expr->field_index = awk_parse_number(st);
        } else {
            /* $(expression) - treat as $1 */
            expr->type = AWK_EXPR_FIELD;
            expr->field_index = 1;
        }
        return expr;
    }

    /* Parenthesized expression */
    if (awk_peek(st) == '(') {
        st->pos++;
        awk_skip_ws(st);
        awk_expr_t *inner = awk_parse_expr(st);
        awk_skip_ws(st);
        if (awk_peek(st) == ')') st->pos++;
        return inner;
    }

    /* length() */
    if (awk_match_str(st, "length")) {
        st->pos += 6;
        awk_skip_ws(st);
        static awk_expr_t expr_len[16];
        static int expr_len_idx = 0;
        expr = &expr_len[expr_len_idx++ & 15];
        awks_memset(expr, 0, sizeof(awk_expr_t));
        expr->type = AWK_EXPR_LENGTH;
        if (awk_peek(st) == '(') {
            st->pos++;
            expr->left = awk_parse_expr(st);
            awk_skip_ws(st);
            if (awk_peek(st) == ')') st->pos++;
        }
        return expr;
    }

    /* int() */
    if (awk_match_str(st, "int")) {
        st->pos += 3;
        awk_skip_ws(st);
        static awk_expr_t expr_int[16];
        static int expr_int_idx = 0;
        expr = &expr_int[expr_int_idx++ & 15];
        awks_memset(expr, 0, sizeof(awk_expr_t));
        expr->type = AWK_EXPR_TONUM;
        awk_skip_ws(st);
        if (awk_peek(st) == '(') {
            st->pos++;
            expr->left = awk_parse_expr(st);
            awk_skip_ws(st);
            if (awk_peek(st) == ')') st->pos++;
        }
        return expr;
    }

    /* Variable name */
    if ((awk_peek(st) >= 'a' && awk_peek(st) <= 'z') ||
        (awk_peek(st) >= 'A' && awk_peek(st) <= 'Z') ||
        awk_peek(st) == '_') {
        char name[32];
        awk_parse_name(st, name, 32);

        /* Check for NR, NF, etc. */
        if (awks_strcmp(name, "NR") == 0 || awks_strcmp(name, "NF") == 0 ||
            awks_strcmp(name, "FS") == 0 || awks_strcmp(name, "OFS") == 0 ||
            awks_strcmp(name, "RS") == 0 || awks_strcmp(name, "FILENAME") == 0) {
            static awk_expr_t expr_builtin[32];
            static int expr_builtin_idx = 0;
            expr = &expr_builtin[expr_builtin_idx++ & 31];
            awks_memset(expr, 0, sizeof(awk_expr_t));
            expr->type = AWK_EXPR_VARIABLE;
            awks_strcpy(expr->var_name, name, 32);
            return expr;
        }

        static awk_expr_t expr_var[64];
        static int expr_var_idx = 0;
        expr = &expr_var[expr_var_idx++ & 63];
        awks_memset(expr, 0, sizeof(awk_expr_t));
        expr->type = AWK_EXPR_VARIABLE;
        awks_strcpy(expr->var_name, name, 32);
        return expr;
    }

    /* Fallback: create zero expression */
    static awk_expr_t expr_zero[16];
    static int expr_zero_idx = 0;
    expr = &expr_zero[expr_zero_idx++ & 15];
    awks_memset(expr, 0, sizeof(awk_expr_t));
    expr->type = AWK_EXPR_NUMBER;
    expr->num_value = 0;
    return expr;
}

/* Parse multiplication/division */
static awk_expr_t *awk_parse_mul(awk_parse_state_t *st) {
    awk_expr_t *left = awk_parse_primary(st);
    awk_skip_ws(st);

    while (awk_peek(st) == '*' || awk_peek(st) == '/' || awk_peek(st) == '%') {
        char op = awk_consume(st);
        awk_skip_ws(st);
        awk_expr_t *right = awk_parse_primary(st);

        static awk_expr_t bin_pool[64];
        static int bin_pool_idx = 0;
        awk_expr_t *bin = &bin_pool[bin_pool_idx++ & 63];
        awks_memset(bin, 0, sizeof(awk_expr_t));
        if (op == '*') bin->type = AWK_EXPR_MUL;
        else if (op == '/') bin->type = AWK_EXPR_DIV;
        else bin->type = AWK_EXPR_MOD;
        bin->left = left;
        bin->right = right;
        left = bin;
        awk_skip_ws(st);
    }
    return left;
}

/* Parse addition/subtraction */
static awk_expr_t *awk_parse_add(awk_parse_state_t *st) {
    awk_expr_t *left = awk_parse_mul(st);
    awk_skip_ws(st);

    while (awk_peek(st) == '+' || awk_peek(st) == '-') {
        char op = awk_consume(st);
        awk_skip_ws(st);
        awk_expr_t *right = awk_parse_mul(st);

        static awk_expr_t bin_pool[64];
        static int bin_pool_idx = 0;
        awk_expr_t *bin = &bin_pool[bin_pool_idx++ & 63];
        awks_memset(bin, 0, sizeof(awk_expr_t));
        if (op == '+') bin->type = AWK_EXPR_ADD;
        else bin->type = AWK_EXPR_SUB;
        bin->left = left;
        bin->right = right;
        left = bin;
        awk_skip_ws(st);
    }
    return left;
}

/* Parse comparison */
static awk_expr_t *awk_parse_cmp(awk_parse_state_t *st) {
    awk_expr_t *left = awk_parse_add(st);
    awk_skip_ws(st);

    /* Check for comparison operators */
    if (awk_match_str(st, "==")) {
        st->pos += 2; awk_skip_ws(st);
        awk_expr_t *right = awk_parse_add(st);
        static awk_expr_t cmp[32]; static int cmp_idx = 0;
        awk_expr_t *c = &cmp[cmp_idx++ & 31];
        awks_memset(c, 0, sizeof(awk_expr_t));
        c->type = AWK_EXPR_EQ; c->left = left; c->right = right;
        return c;
    }
    if (awk_match_str(st, "!=")) {
        st->pos += 2; awk_skip_ws(st);
        awk_expr_t *right = awk_parse_add(st);
        static awk_expr_t cmp[32]; static int cmp_idx = 0;
        awk_expr_t *c = &cmp[cmp_idx++ & 31];
        awks_memset(c, 0, sizeof(awk_expr_t));
        c->type = AWK_EXPR_NE; c->left = left; c->right = right;
        return c;
    }
    if (awk_match_str(st, ">=")) {
        st->pos += 2; awk_skip_ws(st);
        awk_expr_t *right = awk_parse_add(st);
        static awk_expr_t cmp[32]; static int cmp_idx = 0;
        awk_expr_t *c = &cmp[cmp_idx++ & 31];
        awks_memset(c, 0, sizeof(awk_expr_t));
        c->type = AWK_EXPR_GE; c->left = left; c->right = right;
        return c;
    }
    if (awk_match_str(st, "<=")) {
        st->pos += 2; awk_skip_ws(st);
        awk_expr_t *right = awk_parse_add(st);
        static awk_expr_t cmp[32]; static int cmp_idx = 0;
        awk_expr_t *c = &cmp[cmp_idx++ & 31];
        awks_memset(c, 0, sizeof(awk_expr_t));
        c->type = AWK_EXPR_LE; c->left = left; c->right = right;
        return c;
    }
    if (awk_peek(st) == '>') {
        st->pos++; awk_skip_ws(st);
        awk_expr_t *right = awk_parse_add(st);
        static awk_expr_t cmp[32]; static int cmp_idx = 0;
        awk_expr_t *c = &cmp[cmp_idx++ & 31];
        awks_memset(c, 0, sizeof(awk_expr_t));
        c->type = AWK_EXPR_GT; c->left = left; c->right = right;
        return c;
    }
    if (awk_peek(st) == '<') {
        st->pos++; awk_skip_ws(st);
        awk_expr_t *right = awk_parse_add(st);
        static awk_expr_t cmp[32]; static int cmp_idx = 0;
        awk_expr_t *c = &cmp[cmp_idx++ & 31];
        awks_memset(c, 0, sizeof(awk_expr_t));
        c->type = AWK_EXPR_LT; c->left = left; c->right = right;
        return c;
    }

    return left;
}

/* Parse expression (top level) */
static awk_expr_t *awk_parse_expr(awk_parse_state_t *st) {
    return awk_parse_cmp(st);
}

/* ======================================================================== */
/* AWK Statement Parser                                                      */
/* ======================================================================== */

/* Skip to matching brace */
static void awk_skip_block(awk_parse_state_t *st) {
    if (awk_peek(st) == '{') st->pos++;
    int depth = 1;
    while (st->pos < st->len && depth > 0) {
        if (st->input[st->pos] == '{') depth++;
        else if (st->input[st->pos] == '}') depth--;
        st->pos++;
    }
}

/* Parse a statement list (until '}' or end) */
static void awk_parse_stmt_list(awk_parse_state_t *st, awk_stmt_list_t *list) {
    list->count = 0;
    while (st->pos < st->len && list->count < AWK_MAX_STMTS) {
        awk_skip_ws(st);
        if (awk_peek(st) == '}' || awk_peek(st) == 0) break;
        awk_stmt_t *stmt = awk_parse_stmt(st);
        if (stmt) {
            list->stmts[list->count++] = stmt;
        }
        awk_skip_ws(st);
        if (awk_peek(st) == ';') st->pos++;
    }
}

static awk_stmt_t *awk_parse_stmt(awk_parse_state_t *st) {
    static awk_stmt_t stmt_pool[64];
    static int stmt_pool_idx = 0;
    awk_stmt_t *stmt = &stmt_pool[stmt_pool_idx++ & 63];
    awks_memset(stmt, 0, sizeof(awk_stmt_t));

    awk_skip_ws(st);

    /* print statement */
    if (awk_match_str(st, "print")) {
        st->pos += 5;
        stmt->type = AWK_STMT_PRINT;
        stmt->print_newline = 1;
        stmt->print_argc = 0;

        awk_skip_ws(st);
        /* Parse print arguments until newline/semicolon/} */
        while (awk_peek(st) && awk_peek(st) != '\n' &&
               awk_peek(st) != ';' && awk_peek(st) != '}' &&
               stmt->print_argc < AWK_MAX_FIELDS) {
            /* Check for >> redirect */
            if (awk_peek(st) == '>' || awk_peek(st) == '|') break;
            stmt->print_args[stmt->print_argc++] = awk_parse_expr(st);
            awk_skip_ws(st);
            if (awk_peek(st) == ',') {
                st->pos++;
                awk_skip_ws(st);
                /* Print OFS between fields */
            }
        }
        return stmt;
    }

    /* printf statement */
    if (awk_match_str(st, "printf")) {
        st->pos += 6;
        stmt->type = AWK_STMT_PRINTF;
        stmt->print_newline = 0;
        stmt->print_argc = 0;

        awk_skip_ws(st);
        /* First arg is format string */
        if (awk_peek(st) == '"') {
            awk_parse_string_literal(st, stmt->format, AWK_MAX_FORMAT);
        }
        awk_skip_ws(st);

        /* Parse remaining args */
        while (awk_peek(st) && awk_peek(st) != ';' &&
               awk_peek(st) != '}' && stmt->print_argc < AWK_MAX_FIELDS) {
            if (awk_peek(st) == ',') {
                st->pos++;
                awk_skip_ws(st);
            }
            if (awk_peek(st) == ';' || awk_peek(st) == '}' || awk_peek(st) == 0) break;
            stmt->print_args[stmt->print_argc++] = awk_parse_expr(st);
            awk_skip_ws(st);
        }
        return stmt;
    }

    /* if statement */
    if (awk_match_str(st, "if")) {
        st->pos += 2;
        stmt->type = AWK_STMT_IF;
        awk_skip_ws(st);
        if (awk_peek(st) == '(') st->pos++;
        stmt->if_cond = awk_parse_expr(st);
        awk_skip_ws(st);
        if (awk_peek(st) == ')') st->pos++;
        awk_skip_ws(st);

        /* Parse if body */
        static awk_stmt_list_t if_lists[16];
        static int if_list_idx = 0;
        stmt->if_body = &if_lists[if_list_idx++ & 15];
        if (awk_peek(st) == '{') {
            st->pos++;
            awk_parse_stmt_list(st, stmt->if_body);
            awk_skip_ws(st);
            if (awk_peek(st) == '}') st->pos++;
        } else {
            stmt->if_body->count = 0;
            awk_stmt_t *body_stmt = awk_parse_stmt(st);
            if (body_stmt) stmt->if_body->stmts[stmt->if_body->count++] = body_stmt;
        }

        /* Optional else */
        awk_skip_ws(st);
        if (awk_match_str(st, "else")) {
            st->pos += 4;
            awk_skip_ws(st);
            static awk_stmt_list_t else_lists[16];
            static int else_list_idx = 0;
            stmt->else_body = &else_lists[else_list_idx++ & 15];
            if (awk_peek(st) == '{') {
                st->pos++;
                awk_parse_stmt_list(st, stmt->else_body);
                awk_skip_ws(st);
                if (awk_peek(st) == '}') st->pos++;
            } else {
                stmt->else_body->count = 0;
                awk_stmt_t *body_stmt = awk_parse_stmt(st);
                if (body_stmt) stmt->else_body->stmts[stmt->else_body->count++] = body_stmt;
            }
        }
        return stmt;
    }

    /* while statement */
    if (awk_match_str(st, "while")) {
        st->pos += 5;
        stmt->type = AWK_STMT_WHILE;
        awk_skip_ws(st);
        if (awk_peek(st) == '(') st->pos++;
        stmt->loop_cond = awk_parse_expr(st);
        awk_skip_ws(st);
        if (awk_peek(st) == ')') st->pos++;
        awk_skip_ws(st);

        static awk_stmt_list_t while_lists[16];
        static int while_list_idx = 0;
        stmt->loop_body = &while_lists[while_list_idx++ & 15];
        if (awk_peek(st) == '{') {
            st->pos++;
            awk_parse_stmt_list(st, stmt->loop_body);
            awk_skip_ws(st);
            if (awk_peek(st) == '}') st->pos++;
        }
        return stmt;
    }

    /* for statement */
    if (awk_match_str(st, "for")) {
        st->pos += 3;
        stmt->type = AWK_STMT_FOR;
        awk_skip_ws(st);
        if (awk_peek(st) == '(') st->pos++;
        /* init */
        awk_skip_ws(st);
        if (awk_peek(st) != ';') stmt->for_init = awk_parse_stmt(st);
        awk_skip_ws(st);
        if (awk_peek(st) == ';') st->pos++;
        /* condition */
        awk_skip_ws(st);
        stmt->loop_cond = awk_parse_expr(st);
        awk_skip_ws(st);
        if (awk_peek(st) == ';') st->pos++;
        /* increment */
        awk_skip_ws(st);
        if (awk_peek(st) != ')') stmt->for_incr = awk_parse_stmt(st);
        awk_skip_ws(st);
        if (awk_peek(st) == ')') st->pos++;
        awk_skip_ws(st);

        static awk_stmt_list_t for_lists[16];
        static int for_list_idx = 0;
        stmt->loop_body = &for_lists[for_list_idx++ & 15];
        if (awk_peek(st) == '{') {
            st->pos++;
            awk_parse_stmt_list(st, stmt->loop_body);
            awk_skip_ws(st);
            if (awk_peek(st) == '}') st->pos++;
        }
        return stmt;
    }

    /* break / continue / next */
    if (awk_match_str(st, "break")) { st->pos += 5; stmt->type = AWK_STMT_BREAK; return stmt; }
    if (awk_match_str(st, "continue")) { st->pos += 8; stmt->type = AWK_STMT_CONTINUE; return stmt; }
    if (awk_match_str(st, "next")) { st->pos += 4; stmt->type = AWK_STMT_NEXT; return stmt; }

    /* Block { ... } */
    if (awk_peek(st) == '{') {
        stmt->type = AWK_STMT_BLOCK;
        st->pos++;
        static awk_stmt_list_t blk_lists[16];
        static int blk_list_idx = 0;
        stmt->loop_body = &blk_lists[blk_list_idx++ & 15];
        awk_parse_stmt_list(st, stmt->loop_body);
        awk_skip_ws(st);
        if (awk_peek(st) == '}') st->pos++;
        return stmt;
    }

    /* Assignment: var = expr or $N = expr */
    {
        uint32_t saved_pos = st->pos;
        char name[32];
        awk_parse_name(st, name, 32);
        if (awks_strlen(name) > 0) {
            awk_skip_ws(st);
            if (awk_peek(st) == '=') {
                st->pos++;
                awk_skip_ws(st);
                stmt->type = AWK_STMT_ASSIGN;
                awks_strcpy(stmt->assign_var, name, 32);
                stmt->assign_expr = awk_parse_expr(st);
                return stmt;
            }
        }
        st->pos = saved_pos;
    }

    /* Expression statement */
    stmt->type = AWK_STMT_EXPR;
    stmt->expr = awk_parse_expr(st);
    return stmt;
}

/* ======================================================================== */
/* AWK Program Parser                                                        */
/* ======================================================================== */

void awk_init(awk_program_t *prog) {
    awks_memset(prog, 0, sizeof(awk_program_t));
    awks_strcpy(prog->fs, " ", 32);    /* Default: whitespace */
    awks_strcpy(prog->ofs, " ", 32);   /* Default: space */
    awks_strcpy(prog->rs, "\n", 32);   /* Default: newline */
}

int awk_parse(awk_program_t *prog, const char *program) {
    uint32_t len = awks_strlen(program);
    uint32_t pos = 0;

    while (pos < len && prog->rule_count < AWK_MAX_RULES) {
        awk_parse_state_t st;
        st.input = program;
        st.pos = pos;
        st.len = len;
        st.prog = prog;

        awk_skip_ws(&st);
        if (st.pos >= len) break;

        awk_rule_t *rule = &prog->rules[prog->rule_count];
        awks_memset(rule, 0, sizeof(awk_rule_t));

        /* Check for BEGIN or END */
        if (awk_match_str(&st, "BEGIN")) {
            st.pos += 5;
            rule->pat_type = AWK_PAT_BEGIN;
            awk_skip_ws(&st);
            if (awk_peek(&st) == '{') {
                st.pos++;
                rule->has_action = 1;
                awk_parse_stmt_list(&st, &rule->action);
                awk_skip_ws(&st);
                if (awk_peek(&st) == '}') st.pos++;
            }
            prog->rule_count++;
            pos = st.pos;
            continue;
        }

        if (awk_match_str(&st, "END")) {
            st.pos += 3;
            rule->pat_type = AWK_PAT_END;
            awk_skip_ws(&st);
            if (awk_peek(&st) == '{') {
                st.pos++;
                rule->has_action = 1;
                awk_parse_stmt_list(&st, &rule->action);
                awk_skip_ws(&st);
                if (awk_peek(&st) == '}') st.pos++;
            }
            prog->rule_count++;
            pos = st.pos;
            continue;
        }

        /* Check for -F flag handling (done in fish shell before awk_parse) */

        /* Pattern: /regex/ { action } or { action } or expr { action } */
        if (awk_peek(&st) == '/') {
            /* Regex pattern */
            st.pos++; /* skip opening / */
            uint32_t pat_start = st.pos;
            while (st.pos < st.len && awk_peek(&st) != '/') st.pos++;
            if (st.pos < st.len) {
                rule->pat_type = AWK_PAT_REGEX;
                rgx_compile(&rule->regex, &st.input[pat_start], st.pos - pat_start);
                st.pos++; /* skip closing / */
            }
        } else if (awk_peek(&st) == '{') {
            /* No pattern = match every line */
            rule->pat_type = AWK_PAT_ALWAYS;
        } else {
            /* Expression pattern - for simplicity, treat as always-match
             * with the expression as a filter */
            rule->pat_type = AWK_PAT_ALWAYS;
            /* Skip past the expression until we find { */
            int depth = 0;
            while (st.pos < st.len) {
                if (awk_peek(&st) == '{') break;
                if (awk_peek(&st) == '(') depth++;
                else if (awk_peek(&st) == ')') depth--;
                st.pos++;
            }
        }

        /* Parse action block */
        awk_skip_ws(&st);
        if (awk_peek(&st) == '{') {
            st.pos++;
            rule->has_action = 1;
            awk_parse_stmt_list(&st, &rule->action);
            awk_skip_ws(&st);
            if (awk_peek(&st) == '}') st.pos++;
        }

        prog->rule_count++;
        pos = st.pos;
    }

    return 0;
}

/* ======================================================================== */
/* Expression Evaluation                                                     */
/* ======================================================================== */

void awk_eval_expr(awk_program_t *prog, awk_expr_t *expr,
                   char *str_out, uint32_t str_max,
                   int32_t *num_out, uint8_t *is_num_out) {
    if (!expr) {
        str_out[0] = 0;
        *num_out = 0;
        *is_num_out = 1;
        return;
    }

    char left_str[128] = {0};
    char right_str[128] = {0};
    int32_t left_num = 0, right_num = 0;
    uint8_t left_is_num = 0, right_is_num = 0;

    switch (expr->type) {
    case AWK_EXPR_NUMBER:
        *num_out = expr->num_value;
        *is_num_out = 1;
        awks_itoa(expr->num_value, str_out, str_max);
        return;

    case AWK_EXPR_STRING:
        awks_strcpy(str_out, expr->str_value, str_max);
        *num_out = awks_atoi(expr->str_value);
        *is_num_out = 0;
        return;

    case AWK_EXPR_FIELD:
        awks_strcpy(str_out, awk_get_field(prog, expr->field_index), str_max);
        *num_out = awks_atoi(str_out);
        *is_num_out = 0;
        return;

    case AWK_EXPR_DOLLAR0:
        awks_strcpy(str_out, awk_get_field(prog, 0), str_max);
        *num_out = awks_atoi(str_out);
        *is_num_out = 0;
        return;

    case AWK_EXPR_VARIABLE:
        if (awks_strcmp(expr->var_name, "NR") == 0) {
            *num_out = prog->nr; *is_num_out = 1;
            awks_itoa(prog->nr, str_out, str_max);
        } else if (awks_strcmp(expr->var_name, "NF") == 0) {
            *num_out = prog->nf; *is_num_out = 1;
            awks_itoa(prog->nf, str_out, str_max);
        } else if (awks_strcmp(expr->var_name, "FS") == 0) {
            awks_strcpy(str_out, prog->fs, str_max);
            *num_out = 0; *is_num_out = 0;
        } else if (awks_strcmp(expr->var_name, "OFS") == 0) {
            awks_strcpy(str_out, prog->ofs, str_max);
            *num_out = 0; *is_num_out = 0;
        } else if (awks_strcmp(expr->var_name, "FILENAME") == 0) {
            awks_strcpy(str_out, prog->filename, str_max);
            *num_out = 0; *is_num_out = 0;
        } else {
            /* User variable */
            awks_strcpy(str_out, awk_var_get_str(prog, expr->var_name), str_max);
            *num_out = awk_var_get_num(prog, expr->var_name);
            *is_num_out = awk_var_is_num(prog, expr->var_name);
        }
        return;

    case AWK_EXPR_LENGTH: {
        if (expr->left) {
            awk_eval_expr(prog, expr->left, str_out, str_max, &left_num, &left_is_num);
            *num_out = (int32_t)awks_strlen(str_out);
        } else {
            /* length of $0 */
            *num_out = (int32_t)awks_strlen(awk_get_field(prog, 0));
            awks_itoa(*num_out, str_out, str_max);
        }
        *is_num_out = 1;
        return;
    }

    case AWK_EXPR_TONUM: {
        awk_eval_expr(prog, expr->left, str_out, str_max, &left_num, &left_is_num);
        *num_out = left_is_num ? left_num : awks_atoi(str_out);
        *is_num_out = 1;
        awks_itoa(*num_out, str_out, str_max);
        return;
    }

    /* Binary operations */
    case AWK_EXPR_ADD:
    case AWK_EXPR_SUB:
    case AWK_EXPR_MUL:
    case AWK_EXPR_DIV:
    case AWK_EXPR_MOD:
        awk_eval_expr(prog, expr->left, left_str, 128, &left_num, &left_is_num);
        awk_eval_expr(prog, expr->right, right_str, 128, &right_num, &right_is_num);
        if (!left_is_num) left_num = awks_atoi(left_str);
        if (!right_is_num) right_num = awks_atoi(right_str);
        if (expr->type == AWK_EXPR_ADD) *num_out = left_num + right_num;
        else if (expr->type == AWK_EXPR_SUB) *num_out = left_num - right_num;
        else if (expr->type == AWK_EXPR_MUL) *num_out = left_num * right_num;
        else if (expr->type == AWK_EXPR_DIV) *num_out = right_num ? left_num / right_num : 0;
        else *num_out = right_num ? left_num % right_num : 0;
        *is_num_out = 1;
        awks_itoa(*num_out, str_out, str_max);
        return;

    /* String concatenation */
    case AWK_EXPR_CONCAT:
        awk_eval_expr(prog, expr->left, left_str, 128, &left_num, &left_is_num);
        awk_eval_expr(prog, expr->right, right_str, 128, &right_num, &right_is_num);
        awks_strcpy(str_out, left_str, str_max);
        {
            uint32_t slen = awks_strlen(str_out);
            uint32_t rlen = awks_strlen(right_str);
            if (slen + rlen >= str_max) rlen = str_max - slen - 1;
            awks_memcpy(&str_out[slen], right_str, rlen);
            str_out[slen + rlen] = 0;
        }
        *num_out = 0;
        *is_num_out = 0;
        return;

    /* Comparisons */
    case AWK_EXPR_EQ:
    case AWK_EXPR_NE:
    case AWK_EXPR_GT:
    case AWK_EXPR_GE:
    case AWK_EXPR_LT:
    case AWK_EXPR_LE:
        awk_eval_expr(prog, expr->left, left_str, 128, &left_num, &left_is_num);
        awk_eval_expr(prog, expr->right, right_str, 128, &right_num, &right_is_num);
        /* Compare as strings if either is string, otherwise numeric */
        if (!left_is_num && !right_is_num) {
            int cmp = awks_strcmp(left_str, right_str);
            if (expr->type == AWK_EXPR_EQ) *num_out = (cmp == 0);
            else if (expr->type == AWK_EXPR_NE) *num_out = (cmp != 0);
            else if (expr->type == AWK_EXPR_GT) *num_out = (cmp > 0);
            else if (expr->type == AWK_EXPR_GE) *num_out = (cmp >= 0);
            else if (expr->type == AWK_EXPR_LT) *num_out = (cmp < 0);
            else *num_out = (cmp <= 0);
        } else {
            if (!left_is_num) left_num = awks_atoi(left_str);
            if (!right_is_num) right_num = awks_atoi(right_str);
            if (expr->type == AWK_EXPR_EQ) *num_out = (left_num == right_num);
            else if (expr->type == AWK_EXPR_NE) *num_out = (left_num != right_num);
            else if (expr->type == AWK_EXPR_GT) *num_out = (left_num > right_num);
            else if (expr->type == AWK_EXPR_GE) *num_out = (left_num >= right_num);
            else if (expr->type == AWK_EXPR_LT) *num_out = (left_num < right_num);
            else *num_out = (left_num <= right_num);
        }
        *is_num_out = 1;
        str_out[0] = *num_out ? '1' : '0';
        str_out[1] = 0;
        return;

    /* Logical */
    case AWK_EXPR_AND:
    case AWK_EXPR_OR:
        awk_eval_expr(prog, expr->left, left_str, 128, &left_num, &left_is_num);
        awk_eval_expr(prog, expr->right, right_str, 128, &right_num, &right_is_num);
        if (expr->type == AWK_EXPR_AND) *num_out = (left_num && right_num);
        else *num_out = (left_num || right_num);
        *is_num_out = 1;
        str_out[0] = *num_out ? '1' : '0';
        str_out[1] = 0;
        return;

    case AWK_EXPR_NOT:
        awk_eval_expr(prog, expr->left, left_str, 128, &left_num, &left_is_num);
        *num_out = !left_num;
        *is_num_out = 1;
        str_out[0] = *num_out ? '1' : '0';
        str_out[1] = 0;
        return;

    case AWK_EXPR_NEGATE:
        awk_eval_expr(prog, expr->left, left_str, 128, &left_num, &left_is_num);
        *num_out = -left_num;
        *is_num_out = 1;
        awks_itoa(*num_out, str_out, str_max);
        return;

    default:
        str_out[0] = 0;
        *num_out = 0;
        *is_num_out = 1;
        return;
    }
}

/* ======================================================================== */
/* AWK Statement Execution                                                   */
/* ======================================================================== */

static void awk_exec_stmt(awk_program_t *prog, awk_stmt_t *stmt);

static void awk_exec_stmt_list(awk_program_t *prog, awk_stmt_list_t *list) {
    for (int i = 0; i < list->count; i++) {
        awk_exec_stmt(prog, list->stmts[i]);
        if (prog->should_next || prog->should_break || prog->should_continue || prog->has_return)
            break;
    }
}

static void awk_exec_stmt(awk_program_t *prog, awk_stmt_t *stmt) {
    if (!stmt) return;

    char val_str[256];
    int32_t val_num = 0;
    uint8_t val_is_num = 0;

    switch (stmt->type) {
    case AWK_STMT_PRINT:
        for (int i = 0; i < stmt->print_argc; i++) {
            if (i > 0) awk_print_string(prog->ofs);
            awk_eval_expr(prog, stmt->print_args[i], val_str, 256, &val_num, &val_is_num);
            if (val_is_num) awk_print_num(val_num);
            else awk_print_string(val_str);
        }
        if (stmt->print_newline) awk_newline();
        break;

    case AWK_STMT_PRINTF: {
        const char *fmt = stmt->format;
        int argi = 0;
        while (*fmt) {
            if (*fmt == '%') {
                fmt++;
                if (argi < stmt->print_argc) {
                    awk_eval_expr(prog, stmt->print_args[argi], val_str, 256, &val_num, &val_is_num);
                    argi++;
                } else {
                    val_str[0] = 0; val_num = 0; val_is_num = 1;
                }
                if (*fmt == 's') {
                    awk_print_string(val_str);
                } else if (*fmt == 'd' || *fmt == 'i' || *fmt == 'f') {
                    if (val_is_num) awk_print_num(val_num);
                    else awk_print_string(val_str);
                } else if (*fmt == 'x' || *fmt == 'X') {
                    if (val_is_num) {
                        const char *hex = (*fmt == 'x') ? "0123456789abcdef" : "0123456789ABCDEF";
                        uint32_t v = (uint32_t)val_num;
                        char hx[12]; int hi = 0;
                        if (v == 0) { hx[hi++] = '0'; }
                        else {
                            char rev[12]; int ri = 0;
                            while (v) { rev[ri++] = hex[v & 0xF]; v >>= 4; }
                            while (ri > 0) hx[hi++] = rev[--ri];
                        }
                        hx[hi] = 0;
                        awk_print_string(hx);
                    }
                } else if (*fmt == 'c') {
                    if (val_is_num) awk_print_char((char)val_num);
                    else if (val_str[0]) awk_print_char(val_str[0]);
                } else if (*fmt == '%') {
                    awk_print_char('%');
                } else {
                    awk_print_char('%');
                    awk_print_char(*fmt);
                }
            } else if (*fmt == '\\') {
                fmt++;
                if (*fmt == 'n') awk_newline();
                else if (*fmt == 't') awk_print_char('\t');
                else if (*fmt == '\\') awk_print_char('\\');
                else if (*fmt == '0') { /* null */ }
                else { awk_print_char('\\'); awk_print_char(*fmt); }
            } else {
                awk_print_char(*fmt);
            }
            fmt++;
        }
        break;
    }

    case AWK_STMT_ASSIGN:
        awk_eval_expr(prog, stmt->assign_expr, val_str, 256, &val_num, &val_is_num);
        awk_var_set(prog, stmt->assign_var, val_str, val_num, val_is_num);
        break;

    case AWK_STMT_EXPR:
        if (stmt->expr) {
            awk_eval_expr(prog, stmt->expr, val_str, 256, &val_num, &val_is_num);
        }
        break;

    case AWK_STMT_IF: {
        int32_t cond = 0;
        uint8_t cond_is_num;
        awk_eval_expr(prog, stmt->if_cond, val_str, 256, &cond, &cond_is_num);
        if (cond) {
            if (stmt->if_body) awk_exec_stmt_list(prog, stmt->if_body);
        } else {
            if (stmt->else_body) awk_exec_stmt_list(prog, stmt->else_body);
        }
        break;
    }

    case AWK_STMT_WHILE:
        while (1) {
            awk_eval_expr(prog, stmt->loop_cond, val_str, 256, &val_num, &val_is_num);
            if (!val_num) break;
            if (stmt->loop_body) awk_exec_stmt_list(prog, stmt->loop_body);
            if (prog->should_break) { prog->should_break = 0; break; }
            if (prog->should_continue) { prog->should_continue = 0; continue; }
        }
        break;

    case AWK_STMT_FOR:
        if (stmt->for_init) awk_exec_stmt(prog, stmt->for_init);
        while (1) {
            awk_eval_expr(prog, stmt->loop_cond, val_str, 256, &val_num, &val_is_num);
            if (!val_num) break;
            if (stmt->loop_body) awk_exec_stmt_list(prog, stmt->loop_body);
            if (prog->should_break) { prog->should_break = 0; break; }
            if (prog->should_continue) { prog->should_continue = 0; }
            if (stmt->for_incr) awk_exec_stmt(prog, stmt->for_incr);
        }
        break;

    case AWK_STMT_BREAK:
        prog->should_break = 1;
        break;
    case AWK_STMT_CONTINUE:
        prog->should_continue = 1;
        break;
    case AWK_STMT_NEXT:
        prog->should_next = 1;
        break;

    case AWK_STMT_BLOCK:
        if (stmt->loop_body) awk_exec_stmt_list(prog, stmt->loop_body);
        break;

    default:
        break;
    }
}

/* ======================================================================== */
/* AWK Pattern Matching                                                      */
/* ======================================================================== */

static int awk_pattern_matches(awk_program_t *prog, awk_rule_t *rule) {
    switch (rule->pat_type) {
    case AWK_PAT_ALWAYS:
        return 1;

    case AWK_PAT_BEGIN:
    case AWK_PAT_END:
        return 0; /* Not matched during normal processing */

    case AWK_PAT_REGEX:
        return rgx_match(&rule->regex, awk_fields[0], awks_strlen(awk_fields[0]));

    case AWK_PAT_STRING_EQ:
        return awks_strcmp(awk_fields[0], rule->pat_string) == 0;

    case AWK_PAT_STRING_NE:
        return awks_strcmp(awk_fields[0], rule->pat_string) != 0;

    default:
        return 1;
    }
}

/* ======================================================================== */
/* AWK Main Execution                                                        */
/* ======================================================================== */

void awk_process_record(awk_program_t *prog, const char *line, uint32_t line_len) {
    prog->nr++;
    prog->should_next = 0;

    /* Split fields */
    awk_split_fields(prog, line, line_len);

    /* Process rules (skip BEGIN/END) */
    for (int i = 0; i < prog->rule_count; i++) {
        awk_rule_t *rule = &prog->rules[i];
        if (rule->pat_type == AWK_PAT_BEGIN || rule->pat_type == AWK_PAT_END)
            continue;

        if (awk_pattern_matches(prog, rule)) {
            if (rule->has_action) {
                awk_exec_stmt_list(prog, &rule->action);
            } else {
                /* Default action: print $0 */
                awk_print_string(awk_fields[0]);
                awk_newline();
            }
        }

        if (prog->should_next) break;
    }
}

int awk_run(awk_program_t *prog, const char *input, uint32_t input_len) {
    /* Execute BEGIN rules */
    for (int i = 0; i < prog->rule_count; i++) {
        if (prog->rules[i].pat_type == AWK_PAT_BEGIN && prog->rules[i].has_action) {
            awk_exec_stmt_list(prog, &prog->rules[i].action);
        }
    }

    /* Process each line */
    uint32_t line_start = 0;
    for (uint32_t i = 0; i <= input_len; i++) {
        if (i == input_len || input[i] == '\n') {
            uint32_t line_len = i - line_start;
            awk_process_record(prog, &input[line_start], line_len);
            line_start = i + 1;
        }
    }

    /* Execute END rules */
    for (int i = 0; i < prog->rule_count; i++) {
        if (prog->rules[i].pat_type == AWK_PAT_END && prog->rules[i].has_action) {
            awk_exec_stmt_list(prog, &prog->rules[i].action);
        }
    }

    return 0;
}

int awk_exec(const char *program, const char *input, uint32_t input_len) {
    awk_program_t prog;
    awk_init(&prog);
    awk_parse(&prog, program);
    return awk_run(&prog, input, input_len);
}
