#ifndef P1_PARSE_H
#define P1_PARSE_H

#include <stdio.h>

/*
 * parse.h — host-testable parsing helpers for the P1 probe payload.
 *
 * Extracted from the tokenizer used by /tmp/opencode/diag/tool.c so the
 * new command-line grammar (extra mount options, flags, base-0 integers)
 * can be tested on the host without the PS5 toolchain or a console.
 */

/* Tokenize a mutable command line, honoring "quoted strings".
 * Writes pointers into `line` (modifying it in place).
 * Returns the token count, 0 for an empty/whitespace-only line,
 * or -1 if more than maxtok tokens are present. */
int p1_tokenize(char *line, char **tok, int maxtok);

/* Strip trailing '\r', '\n', ' ' and '\t' in place. Returns s. */
char *p1_chomp(char *s);

/* Chomp + tokenize — the exact preprocessing run_command applies to EVERY
 * line (stdin mode AND script mode). Without the chomp, fgets' trailing
 * '\n' would be glued to the LAST token (the live-run defect: mkdir created
 * a directory literally named "p1mnt\n"; option_len parsed "0x1C\n").
 * Returns the token count like p1_tokenize. */
int p1_tokenize_line(char *line, char **tok, int maxtok);

/* Split "key=value" at the first '='.
 * Returns 1 when '=' is present (sets key and val; val may be empty),
 * 0 when there is no '=' (sets *key to the whole arg, *val to NULL),
 * or -1 for an empty key (arg starts with '=' or is empty).
 * Modifies `arg` in place. */
int p1_split_kv(char *arg, char **key, char **val);

/* Parse an unsigned 32-bit integer, accepting decimal or 0x-prefixed hex.
 * Returns 0 on success (writes *out) or -1 on empty/invalid/overflow. */
int p1_parse_u32(const char *s, unsigned int *out);

/* Parse a signed 32-bit integer (LVD device_id), accepting an optional '-'
 * followed by decimal or 0x-prefixed hex. Returns 0 on success (writes *out,
 * range [-2147483648, 2147483647]) or -1 on empty/invalid/overflow. */
int p1_parse_i32(const char *s, int *out);

/* Read the next script line from `fp` into `buf` (capacity `n`).
 * Skips blank (whitespace-only) lines and comment lines (first non-space
 * char is '#'). Trailing CR/LF are stripped. A line longer than n-1 chars
 * is truncated to n-1 chars and the remainder of the line is discarded
 * (never returned as a phantom next line).
 * Returns 1 when a line was stored, 0 on EOF/end (or invalid args, n<2). */
int p1_script_next(FILE *fp, char *buf, size_t n);

/* Parse an unsigned 16-bit integer (LVD img_type / option_len), accepting
 * decimal or 0x/0X-prefixed hex exactly like p1_parse_u32.
 * Returns 0 on success (writes *out, range [0, 65535]) or -1 on
 * empty/invalid/overflow. */
int p1_parse_u16(const char *s, unsigned int *out);

/* Parse the numeric arguments of `lvdattach` exactly as the tool does:
 *   sector    -> p1_parse_u32
 *   img_type  -> p1_parse_u16   <-- THE img_type parser (hex 0x.. accepted)
 *   option_len-> p1_parse_u16   (opt_len_s may be NULL: *optlen unchanged)
 * Returns 0 on success, or the 1-based index of the first invalid field
 * (1=sector, 2=img_type, 3=option_len). */
int p1_parse_lvd_nums(const char *sector_s, const char *imgtype_s,
                      const char *optlen_s,
                      unsigned int *sector, unsigned int *imgtype,
                      unsigned int *optlen);

/* Resolve a device token for mntfs (source) / lvddetach (id).
 * Literal "last" resolves against the remembered device id (the
 * device_id of the most recent SUCCESSFUL lvdattach in the process):
 *   returns  1 and *devid = last_dev  when tok is "last" and last_dev >= 0
 *   returns -1                         when tok is "last" and last_dev <  0
 *   returns  0                         when tok is anything else (use as-is)
 */
int p1_resolve_last(const char *tok, int last_dev, int *devid);

/* R8: after a SUCCESSFUL lvddetach of device `detached_id`, the remembered
 * `last` device must be cleared when it matches — a stale `last` could
 * otherwise detach a reassigned system device in a later block. Returns the
 * new remembered id (-1 after a matching clear). */
int p1_last_after_detach(int last_dev, int detached_id);

/* Glob matcher for the `rmdir` command's leaf pattern: '*' matches any
 * sequence (including empty), '?' matches exactly one character, all other
 * characters match themselves. Escaping is NOT supported.
 * Returns 1 on match, 0 otherwise (0 for NULL args). */
int p1_glob_match(const char *pat, const char *name);

/* Format "/dev/lvd<devid>" into buf (device path for an LVD unit).
 * Returns 0, or -1 for devid < 0, NULL buf, or insufficient space. */
int p1_lvd_path(int devid, char *buf, size_t n);

#endif /* P1_PARSE_H */
