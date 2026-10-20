/*
 * parse.c — host-testable parsing helpers for the P1 probe payload.
 *
 * Behaviour mirrors the tokenizer already used by /tmp/opencode/diag/tool.c
 * (so existing command lines keep working) and adds key=value splitting and
 * base-0 integer parsing needed by the extended lvdattach/mntfs grammar.
 */
#include <stddef.h>
#include <string.h>

#include "parse.h"

int p1_tokenize(char *line, char **tok, int maxtok)
{
    int n = 0;
    char *s = line;

    while (*s) {
        while (*s == ' ' || *s == '\t')
            s++;
        if (!*s)
            break;
        if (n >= maxtok)
            return -1;

        if (*s == '"') {
            s++;
            tok[n++] = s;
            while (*s && *s != '"')
                s++;
            if (*s)
                *s++ = 0;
        } else {
            tok[n++] = s;
            while (*s && *s != ' ' && *s != '\t')
                s++;
            if (*s)
                *s++ = 0;
        }
    }

    return n;
}

int p1_split_kv(char *arg, char **key, char **val)
{
    char *eq;

    if (arg == NULL || *arg == 0)
        return -1;

    eq = strchr(arg, '=');
    if (eq == NULL) {
        *key = arg;
        *val = NULL;
        return 0;
    }

    if (eq == arg)
        return -1; /* empty key, e.g. "=v" */

    *eq = 0;
    *key = arg;
    *val = eq + 1;
    return 1;
}

int p1_parse_u32(const char *s, unsigned int *out)
{
    unsigned long v = 0;
    int base = 10;

    if (s == NULL || *s == 0)
        return -1;

    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        base = 16;
        s += 2;
        if (*s == 0)
            return -1;
    }

    for (; *s; s++) {
        unsigned int d;
        char c = *s;

        if (c >= '0' && c <= '9')
            d = (unsigned int)(c - '0');
        else if (base == 16 && c >= 'a' && c <= 'f')
            d = (unsigned int)(c - 'a' + 10);
        else if (base == 16 && c >= 'A' && c <= 'F')
            d = (unsigned int)(c - 'A' + 10);
        else
            return -1;

        if (d >= (unsigned int)base)
            return -1;
        if (v > (0xffffffffUL - d) / (unsigned long)base)
            return -1; /* would overflow 32 bits */
        v = v * (unsigned long)base + d;
    }

    *out = (unsigned int)v;
    return 0;
}

/* Real implementation (TDD GREEN): sign + magnitude, magnitude parsed by the
 * already-tested p1_parse_u32, then bounded to the int32 range. */
int p1_parse_i32(const char *s, int *out)
{
    unsigned int mag = 0;
    int neg = 0;

    if (s == NULL || *s == 0)
        return -1;
    if (*s == '-') {
        neg = 1;
        s++;
        if (*s == 0)
            return -1;
    }
    if (p1_parse_u32(s, &mag) != 0)
        return -1;

    if (neg) {
        if (mag > 2147483648u)
            return -1;
        *out = (mag == 2147483648u) ? (-2147483647 - 1) : -(int)mag;
    } else {
        if (mag > 2147483647u)
            return -1;
        *out = (int)mag;
    }
    return 0;
}

/*
 * Real implementation (TDD GREEN): read one significant line. fgets keeps
 * at most n-1 chars; when no newline was captured the rest of the physical
 * line is consumed and dropped, so a truncated line never yields a phantom
 * second line. Blank and '#' comment lines are skipped.
 */
int p1_script_next(FILE *fp, char *buf, size_t n)
{
    if (fp == NULL || buf == NULL || n < 2)
        return 0;

    while (fgets(buf, (int)n, fp) != NULL) {
        size_t len = strlen(buf);
        char *s;

        /* no newline captured: truncated line, or final line without one */
        if (len == 0 || buf[len - 1] != '\n') {
            int c = getc(fp);
            if (c != EOF) {
                while (c != EOF && c != '\n')
                    c = getc(fp); /* drop the remainder of the line */
            }
        }

        /* strip trailing CR/LF */
        while (len > 0 && (buf[len - 1] == '\n' || buf[len - 1] == '\r'))
            buf[--len] = '\0';

        /* skip blank and comment lines */
        for (s = buf; *s == ' ' || *s == '\t'; s++)
            ;
        if (*s == '\0' || *s == '#')
            continue;

        return 1;
    }
    return 0;
}

/* Real implementation (TDD GREEN): strip trailing CR/LF/space/tab. */
char *p1_chomp(char *s)
{
    size_t len;

    if (s == NULL)
        return s;
    len = strlen(s);
    while (len > 0 && (s[len - 1] == '\n' || s[len - 1] == '\r' ||
                       s[len - 1] == ' ' || s[len - 1] == '\t'))
        s[--len] = '\0';
    return s;
}

/* Real implementation (TDD GREEN): chomp + tokenize — run_command's path. */
int p1_tokenize_line(char *line, char **tok, int maxtok)
{
    if (line == NULL)
        return 0;
    return p1_tokenize(p1_chomp(line), tok, maxtok);
}

/* Real implementation (TDD GREEN): clear `last` when the successful detach
 * matches it (R8); otherwise keep. */
int p1_last_after_detach(int last_dev, int detached_id)
{
    if (detached_id >= 0 && detached_id == last_dev)
        return -1;
    return last_dev;
}

/* Real implementation (TDD GREEN): classic single-pass backtracking glob —
 * '*' (any sequence, incl. empty), '?' (exactly one char), literals. */
int p1_glob_match(const char *pat, const char *name)
{
    const char *star = NULL, *back = NULL;

    if (pat == NULL || name == NULL)
        return 0;

    while (*name) {
        if (*pat == '?' || *pat == *name) {
            pat++;
            name++;
        } else if (*pat == '*') {
            star = pat++;   /* remember the star and the position it covers */
            back = name;
        } else if (star != NULL) {
            pat = star + 1; /* backtrack: the star absorbs one more char */
            name = ++back;
        } else {
            return 0;
        }
    }
    while (*pat == '*')
        pat++;
    return *pat == '\0';
}

/* Real implementation (TDD GREEN): u16 = u32 parse (decimal or 0x/0X hex)
 * + range check. This is THE parser for LVD img_type/option_len. */
int p1_parse_u16(const char *s, unsigned int *out)
{
    unsigned int v = 0;

    if (p1_parse_u32(s, &v) != 0)
        return -1;
    if (v > 0xFFFFu)
        return -1;
    if (out != NULL)
        *out = v;
    return 0;
}

/* Real implementation (TDD GREEN): the exact lvdattach numeric wiring —
 * sector=u32, img_type=u16 (hex accepted), option_len=u16 (optional). */
int p1_parse_lvd_nums(const char *sector_s, const char *imgtype_s,
                      const char *optlen_s,
                      unsigned int *sector, unsigned int *imgtype,
                      unsigned int *optlen)
{
    if (sector_s == NULL || p1_parse_u32(sector_s, sector) != 0)
        return 1;
    if (imgtype_s == NULL || p1_parse_u16(imgtype_s, imgtype) != 0)
        return 2;
    if (optlen_s != NULL && p1_parse_u16(optlen_s, optlen) != 0)
        return 3;
    return 0;
}

/* Real implementation (TDD GREEN): literal "last" -> remembered device id. */
int p1_resolve_last(const char *tok, int last_dev, int *devid)
{
    if (tok == NULL || strcmp(tok, "last") != 0)
        return 0;
    if (last_dev < 0)
        return -1;
    if (devid != NULL)
        *devid = last_dev;
    return 1;
}

/* Real implementation (TDD GREEN): "/dev/lvd<devid>". */
int p1_lvd_path(int devid, char *buf, size_t n)
{
    int r;

    if (devid < 0 || buf == NULL || n == 0)
        return -1;
    r = snprintf(buf, n, "/dev/lvd%d", devid);
    if (r < 0 || (size_t)r >= n)
        return -1;
    return 0;
}
