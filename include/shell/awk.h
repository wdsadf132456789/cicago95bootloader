/**
 * Chicago-95 AWK Implementation
 * Freestanding pattern scanning and processing language for bare-metal ring-0
 * Supports: regex, field splitting, pattern-action pairs, BEGIN/END,
 *           print/printf, variables, expressions, built-in functions
 */

#ifndef AWK_H
#define AWK_H

#include <stdint.h>

/* Limits */
#define AWK_MAX_RULES       32
#define AWK_MAX_STMTS       16
#define AWK_MAX_FIELDS      32
#define AWK_MAX_VARS        32
#define AWK_MAX_LINE        512
#define AWK_MAX_FIELD_LEN   256
#define AWK_MAX_REGEX       128
#define AWK_MAX_PATTERN     128
#define AWK_MAX_FORMAT      128

/* ======================================================================== */
/* Regex Engine                                                              */
/* ======================================================================== */

typedef enum {
    RGX_LITERAL,    /* Match literal character */
    RGX_DOT,        /* Match any character */
    RGX_STAR,       /* Zero or more of preceding */
    RGX_PLUS,       /* One or more of preceding */
    RGX_QUESTION,   /* Zero or one of preceding */
    RGX_CHAR_CLASS, /* Character class [abc] [^abc] */
    RGX_ANCHOR_START, /* ^ anchor */
    RGX_ANCHOR_END,   /* $ anchor */
    RGX_ALT,        /* Alternation | */
    RGX_GROUP_OPEN, /* ( opening */
    RGX_GROUP_CLOSE, /* ) closing */
    RGX_BRANCH,     /* Internal: separates alternation branches */
} rgx_type_t;

typedef struct {
    rgx_type_t type;
    char      literal;          /* For RGX_LITERAL */
    char      char_class[64];   /* For RGX_CHAR_CLASS: the chars in the class */
    uint8_t   negated_class;    /* ^ in class */
} rgx_node_t;

typedef struct {
    rgx_node_t nodes[64];
    int        count;
    uint8_t    valid;
} rgx_compiled_t;

/* Compile a regex pattern string into a compiled form */
int  rgx_compile(rgx_compiled_t *out, const char *pattern, uint32_t pat_len);

/* Test if text matches a compiled regex. Returns 1 if match, 0 if not */
int  rgx_match(const rgx_compiled_t *rx, const char *text, uint32_t text_len);

/* Find first match of compiled regex in text. Returns offset or -1 */
int  rgx_find(const rgx_compiled_t *rx, const char *text, uint32_t text_len);

/* ======================================================================== */
/* AWK Types                                                                 */
/* ======================================================================== */

/* Pattern types */
typedef enum {
    AWK_PAT_ALWAYS,      /* No pattern = always match (but not BEGIN/END) */
    AWK_PAT_REGEX,       /* /pattern/ */
    AWK_PAT_STRING_EQ,   /* str == "literal" */
    AWK_PAT_STRING_NE,   /* str != "literal" */
    AWK_PAT_NUMERIC_EQ,  /* expr == number */
    AWK_PAT_NUMERIC_NE,  /* expr != number */
    AWK_PAT_NUMERIC_GT,  /* expr > number */
    AWK_PAT_NUMERIC_GE,  /* expr >= number */
    AWK_PAT_NUMERIC_LT,  /* expr < number */
    AWK_PAT_NUMERIC_LE,  /* expr <= number */
    AWK_PAT_BEGIN,       /* BEGIN block */
    AWK_PAT_END,         /* END block */
    AWK_PAT_RANGE_START, /* range start pattern */
    AWK_PAT_RANGE_END,   /* range end pattern */
} awk_pat_type_t;

/* Expression types for patterns and actions */
typedef enum {
    AWK_EXPR_FIELD,       /* $N - field reference */
    AWK_EXPR_DOLLAR0,     /* $0 - entire line */
    AWK_EXPR_VARIABLE,    /* var name */
    AWK_EXPR_NUMBER,      /* numeric literal */
    AWK_EXPR_STRING,      /* string literal */
    AWK_EXPR_ADD,         /* + */
    AWK_EXPR_SUB,         /* - */
    AWK_EXPR_MUL,         /* * */
    AWK_EXPR_DIV,         /* / */
    AWK_EXPR_MOD,         /* % */
    AWK_EXPR_CONCAT,      /* string concatenation */
    AWK_EXPR_EQ,          /* == */
    AWK_EXPR_NE,          /* != */
    AWK_EXPR_GT,          /* > */
    AWK_EXPR_GE,          /* >= */
    AWK_EXPR_LT,          /* < */
    AWK_EXPR_LE,          /* <= */
    AWK_EXPR_AND,         /* && */
    AWK_EXPR_OR,          /* || */
    AWK_EXPR_NOT,         /* ! */
    AWK_EXPR_NEGATE,      /* unary - */
    AWK_EXPR_LENGTH,      /* length() */
    AWK_EXPR_TONUM,       /* int() / +val */
    AWK_EXPR_TOSTR,       /* "" + val */
} awk_expr_type_t;

typedef struct awk_expr {
    awk_expr_type_t type;
    /* For AWK_EXPR_NUMBER */
    int32_t num_value;
    /* For AWK_EXPR_STRING */
    char str_value[64];
    /* For AWK_EXPR_FIELD */
    int32_t field_index;
    /* For AWK_EXPR_VARIABLE */
    char var_name[32];
    /* For binary ops */
    struct awk_expr *left;
    struct awk_expr *right;
} awk_expr_t;

/* Statement types */
typedef enum {
    AWK_STMT_PRINT,       /* print expr, expr, ... */
    AWK_STMT_PRINTF,      /* printf format, expr, expr, ... */
    AWK_STMT_ASSIGN,      /* var = expr */
    AWK_STMT_FIELD_ASSIGN, /* $N = expr */
    AWK_STMT_EXPR,        /* expression statement */
    AWK_STMT_IF,          /* if (cond) { ... } else { ... } */
    AWK_STMT_WHILE,       /* while (cond) { ... } */
    AWK_STMT_FOR,         /* for (init; cond; incr) { ... } */
    AWK_STMT_BREAK,       /* break */
    AWK_STMT_CONTINUE,    /* continue */
    AWK_STMT_NEXT,        /* next (skip to next record) */
    AWK_STMT_BLOCK,       /* { stmt; stmt; ... } */
    AWK_STMT_DELETE,      /* delete var */
    AWK_STMT_RETURN,      /* return expr */
} awk_stmt_type_t;

typedef struct awk_stmt awk_stmt_t;

typedef struct {
    awk_stmt_t *stmts[AWK_MAX_STMTS];
    int         count;
} awk_stmt_list_t;

struct awk_stmt {
    awk_stmt_type_t type;
    /* For PRINT: list of expressions to print */
    awk_expr_t *print_args[AWK_MAX_FIELDS];
    int         print_argc;
    uint8_t     print_newline;  /* 0 for printf */
    /* For PRINTF: format string */
    char format[AWK_MAX_FORMAT];
    /* For ASSIGN */
    char assign_var[32];
    awk_expr_t *assign_expr;
    /* For EXPR */
    awk_expr_t *expr;
    /* For IF */
    awk_expr_t *if_cond;
    awk_stmt_list_t *if_body;
    awk_stmt_list_t *else_body;
    /* For WHILE / FOR */
    awk_expr_t *loop_cond;
    awk_stmt_list_t *loop_body;
    awk_stmt_t *for_init;
    awk_stmt_t *for_incr;
};

/* ======================================================================== */
/* AWK Rule (Pattern + Action pair)                                         */
/* ======================================================================== */

typedef struct {
    awk_pat_type_t pat_type;
    /* For AWK_PAT_REGEX */
    rgx_compiled_t regex;
    /* For string comparison patterns */
    char pat_string[64];
    /* For numeric comparison patterns */
    int32_t pat_number;
    /* For range patterns */
    rgx_compiled_t range_start_regex;
    rgx_compiled_t range_end_regex;
    char range_start_str[64];
    char range_end_str[64];
    /* Action body */
    awk_stmt_list_t action;
    uint8_t has_action; /* 0 = default action is {print $0} */
} awk_rule_t;

/* ======================================================================== */
/* AWK Program                                                               */
/* ======================================================================== */

typedef struct {
    awk_rule_t rules[AWK_MAX_RULES];
    int        rule_count;

    /* Field separator */
    char fs[32];
    char ofs[32];
    char rs[32];

    /* Built-in variables */
    int32_t nr;         /* Current record number */
    int32_t nf;         /* Number of fields in current record */
    char filename[64];  /* Current filename */

    /* User variables */
    struct {
        char name[32];
        char str_val[64];
        int32_t num_val;
        uint8_t is_num;
    } vars[AWK_MAX_VARS];
    int var_count;

    /* State */
    uint8_t should_next;  /* Set by 'next' statement */
    uint8_t should_break;
    uint8_t should_continue;
    int32_t return_val;
    uint8_t has_return;
} awk_program_t;

/* ======================================================================== */
/* AWK API                                                                   */
/* ======================================================================== */

/* Initialize an awk program */
void awk_init(awk_program_t *prog);

/* Parse an awk program from a string (e.g., '{print $1, $3}')
 * Returns 0 on success, -1 on error */
int  awk_parse(awk_program_t *prog, const char *program);

/* Run the awk program on an input text buffer.
 * For each line, splits into fields, evaluates rules, executes actions.
 * Returns 0 on success */
int  awk_run(awk_program_t *prog, const char *input, uint32_t input_len);

/* Run awk on a single record (line). Internal use. */
void awk_process_record(awk_program_t *prog, const char *line, uint32_t line_len);

/* High-level: parse and run awk in one call.
 * Returns 0 on success */
int  awk_exec(const char *program, const char *input, uint32_t input_len);

/* ======================================================================== */
/* AWK Internal Functions                                                    */
/* ======================================================================== */

/* Variable management */
void     awk_var_set(awk_program_t *prog, const char *name, const char *str_val, int32_t num_val, uint8_t is_num);
int32_t  awk_var_get_num(awk_program_t *prog, const char *name);
const char *awk_var_get_str(awk_program_t *prog, const char *name);
uint8_t  awk_var_is_num(awk_program_t *prog, const char *name);

/* Evaluate an expression */
void awk_eval_expr(awk_program_t *prog, awk_expr_t *expr, char *str_out, uint32_t str_max, int32_t *num_out, uint8_t *is_num_out);

/* Split a line into fields */
void awk_split_fields(awk_program_t *prog, const char *line, uint32_t line_len);

/* Get field value */
const char *awk_get_field(awk_program_t *prog, int32_t index);

/* Output helpers (print to VGA text) */
void awk_print_string(const char *s);
void awk_print_char(char c);
void awk_print_num(int32_t num);
void awk_newline(void);

#endif /* AWK_H */
