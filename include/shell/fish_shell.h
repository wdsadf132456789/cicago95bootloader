/**
 * Chicago-95 Fish Full Shell
 * Fish-style interactive shell: syntax highlighting, autosuggestions,
 * tab completion, $status, piping, wildcards, abbreviations,
 * function definitions, control flow (if/for/while/switch),
 * command substitution, redirections, and extended builtins.
 */

#ifndef FISH_SHELL_H
#define FISH_SHELL_H

#include <stdint.h>

#define FISH_MAX_CMD      512
#define FISH_MAX_ARGS     64
#define FISH_HISTORY_SLOTS 128
#define FISH_MAX_COMPLETIONS 32
#define FISH_MAX_VARS     64
#define FISH_MAX_ABBREVS  32
#define FISH_MAX_PIPES    8
#define FISH_MAX_FUNCS    32
#define FISH_MAX_FUNC_LINES 64
#define FISH_FUNC_LINE_LEN 128
#define FISH_MAX_IF_NEST   16
#define FISH_MAX_LOOPS     16

/* Key codes for interactive shells */
#define KEY_NONE        (-1)
#define KEY_UP          (-2)
#define KEY_DOWN        (-3)
#define KEY_LEFT        (-4)
#define KEY_RIGHT       (-5)
#define KEY_HOME        (-6)
#define KEY_END         (-7)
#define KEY_DELETE      (-8)
#define KEY_PGUP        (-9)
#define KEY_PGDN        (-10)
#define KEY_F1          (-11)
#define KEY_F2          (-12)
#define KEY_F3          (-13)
#define KEY_F4          (-14)
#define KEY_F5          (-15)
#define KEY_F6          (-16)
#define KEY_F7          (-17)
#define KEY_F8          (-18)
#define KEY_F9          (-19)
#define KEY_F10         (-20)
#define KEY_F11         (-21)
#define KEY_F12         (-22)
#define KEY_CTRL_A      (-23)
#define KEY_CTRL_E      (-24)
#define KEY_CTRL_U      (-25)
#define KEY_CTRL_K      (-26)
#define KEY_CTRL_W      (-27)
#define KEY_CTRL_R      (-28)
#define KEY_CTRL_D      (-29)
#define KEY_CTRL_C      (-30)
#define KEY_CTRL_L      (-31)
#define KEY_CTRL_Z      (-32)
#define KEY_TAB         (-33)
#define KEY_SHIFT_TAB   (-34)
#define KEY_ENTER       (-35)
#define KEY_BACKSPACE   (-36)
#define KEY_CTRL_Y      (-37)
#define KEY_CTRL_LEFT   (-38)
#define KEY_CTRL_RIGHT  (-39)
#define KEY_CTRL_UP     (-40)
#define KEY_CTRL_DOWN   (-41)

/* Redirection types */
#define FISH_REDIR_NONE  0
#define FISH_REDIR_OUT   1  /* > */
#define FISH_REDIR_APPEND 2 /* >> */
#define FISH_REDIR_IN    3  /* < */
#define FISH_REDIR_ERR   4  /* 2> */
#define FISH_REDIR_ERR_APP 5 /* 2>> */
#define FISH_REDIR_ERR_OUT 6 /* 2>&1 */

/* Variable scopes */
#define FISH_SCOPE_GLOBAL  0
#define FISH_SCOPE_LOCAL   1
#define FISH_SCOPE_EXPORT  2
#define FISH_SCOPE_UNIV    3
#define FISH_SCOPE_FUNC    4

/* Fish-style prompt components */
typedef struct {
    char     username[32];
    char     hostname[32];
    char     cwd[128];
    char     rprompt[128];  /* right prompt */
    uint8_t  show_status;
    uint8_t  show_jobs;
    uint8_t  last_status;
    uint8_t  mode; /* 0=normal, 1=insert, 2=replace */
} fish_prompt_t;

/* Universal variable */
typedef struct {
    char     name[32];
    char     value[64];
    uint8_t  exported;
    uint8_t  scope; /* FISH_SCOPE_* */
} fish_var_t;

/* Abbreviation */
typedef struct {
    char     trigger[16];
    char     expansion[64];
    uint8_t  position; /* 0=command, 1=anywhere */
} fish_abbrev_t;

/* Tab completion candidate */
typedef struct {
    char     text[64];
    uint8_t  is_dir;
    uint8_t  is_command;
    uint8_t  is_function;
    uint8_t  is_alias;
} fish_completion_t;

/* Pipe segment with redirections */
typedef struct {
    char     cmd[FISH_MAX_CMD];
    uint32_t cmd_len;
    /* Redirections */
    uint8_t  redir_out_type;
    char     redir_out_file[128];
    uint8_t  redir_err_type;
    char     redir_err_file[128];
    uint8_t  redir_in_type;
    char     redir_in_file[128];
} fish_pipe_segment_t;

/* History entry */
typedef struct {
    char     cmd[FISH_MAX_CMD];
    uint32_t cmd_len;
    int      status;
    uint64_t timestamp; /* uptime ticks */
} fish_history_entry_t;

/* User-defined function */
typedef struct {
    char     name[64];
    char     lines[FISH_MAX_FUNC_LINES][FISH_FUNC_LINE_LEN];
    uint32_t line_count;
    char     description[128];
    uint8_t  autoloaded;
} fish_func_t;

/* Block types for control flow */
#define FISH_BLOCK_IF      0
#define FISH_BLOCK_FOR     1
#define FISH_BLOCK_WHILE   2
#define FISH_BLOCK_SWITCH  3
#define FISH_BLOCK_BEGIN   4
#define FISH_BLOCK_FUNCDEF 5

/* Loop state (for break/continue) */
typedef struct {
    uint8_t  block_type;
    int      for_var_value;
    int      for_end;
    int      for_step;
    uint32_t start_line;  /* line in func body or input to loop back to */
    uint8_t  broken;
    char     var_name[32]; /* loop variable */
} fish_loop_state_t;

/* Command substitution result */
typedef struct {
    char     text[256];
    uint32_t len;
} fish_cmdsub_result_t;

/* Main fish shell state */
typedef struct {
    uint8_t              initialized;
    uint8_t              running;
    fish_prompt_t        prompt;
    fish_var_t           vars[FISH_MAX_VARS];
    uint32_t             var_count;
    fish_abbrev_t        abbrevs[FISH_MAX_ABBREVS];
    uint32_t             abbrev_count;
    fish_history_entry_t history[FISH_HISTORY_SLOTS];
    uint32_t             history_count;
    uint32_t             history_idx;
    uint32_t             cmd_count;
    int                  last_status;
    uint8_t              syntax_enabled;
    uint8_t              autosuggest_enabled;
    uint8_t              bracket_paste;
    uint32_t             cursor_col;
    /* Function definitions */
    fish_func_t          funcs[FISH_MAX_FUNCS];
    uint32_t             func_count;
    /* Control flow state */
    fish_loop_state_t    loops[FISH_MAX_LOOPS];
    uint32_t             loop_depth;
    uint8_t              break_requested;
    uint8_t              continue_requested;
    /* Command line state (for commandline builtin) */
    char                 commandline_buf[FISH_MAX_CMD];
    uint32_t             commandline_len;
    /* Current executing function name */
    char                 current_func[64];
    /* Event handlers */
    char                 event_handlers[16][64]; /* event name */
    uint32_t             event_count;
    /* Color state */
    uint8_t              current_fg;
    uint8_t              current_bg;
} fish_state_t;

/* Initialize fish shell */
int fish_init(void);

/* Run the fish shell main loop */
int fish_run(void);

/* Exit the fish shell */
void fish_exit(void);

/* Variable access */
int          fish_var_set(const char *name, const char *value);
int          fish_var_set_scope(const char *name, const char *value, uint8_t scope);
const char  *fish_var_get(const char *name);
int          fish_var_exported(const char *name);
int          fish_var_erase(const char *name);
uint32_t     fish_var_count_by_prefix(const char *prefix);

/* Abbreviation management */
int  fish_abbrev_add(const char *trigger, const char *expansion);
int  fish_abbrev_expand(const char *trigger, char *out, uint32_t out_len);
int  fish_abbrev_erase(const char *trigger);
int  fish_abbrev_show(void);

/* Function management */
int  fish_func_add(const char *name, const char *body_lines[], uint32_t count);
int  fish_func_erase(const char *name);
fish_func_t *fish_func_find(const char *name);
int  fish_func_list(void);
int  fish_func_show(const char *name);
int  fish_func_run(const char *name, int argc, char args[FISH_MAX_ARGS][64]);

/* Tab completion */
int  fish_complete(const char *partial, fish_completion_t *comps, uint32_t max_comps);

/* History search */
int  fish_history_search(const char *prefix, fish_history_entry_t *results, uint32_t max_results);
int  fish_history_delete(uint32_t index);
int  fish_history_clear(void);

/* Syntax highlighting helper */
uint8_t fish_syntax_color(const char *token, uint32_t token_len, uint8_t is_first_token);

/* Read a key from PS/2 keyboard (shared by fish + nano) */
int fish_read_key(void);

/* Command substitution */
int  fish_cmdsub_eval(const char *input, char *output, uint32_t out_len);

/* Event system */
int  fish_emit_event(const char *event_name);

#endif /* FISH_SHELL_H */
