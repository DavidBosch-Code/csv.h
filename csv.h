/* csv.h — single-header CSV parser
 *
 * USAGE
 *   #define CSV_IMPLEMENTATION in exactly ONE translation unit before including.
 *
 * FEATURES
 *   - Configurable delimiter (or auto-detect from , ; \t |)
 *   - mmap for files >= CSV_MMAP_THRESHOLD (default 1 MB), read() fallback
 *   - Optional header row parsed into named schema
 *   - Per-column type hints (STRING / INT / FLOAT / BOOL) or auto-inference
 *   - Row iterator, column iterator, filtered row iterator
 *   - In-memory buffer mode (csv_open_mem)
 *
 * QUICK EXAMPLE
 *   #define CSV_IMPLEMENTATION
 *   #include "csv.h"
 *
 *   csv_t *c = csv_open("data.csv", 1);   // auto-detect delimiter
 *
 *   csv_row_iter_t it = csv_rows(c);
 *   while (csv_row_next(&it)) {
 *       csv_row_t *row = &it.row;
 *       printf("%s\n", csv_field_str(&row->fields[0]));
 *   }
 *   csv_row_iter_free(&it);
 *   csv_close(c);
 */

#ifndef CSV_H
#define CSV_H

#include <stddef.h>
#include <stdint.h>

/* b32: a 32-bit boolean. Communicates intent more clearly than a plain int,
   and gives predictable struct packing without introducing a _Bool/_Stdbool
   dependency. Use 0/1 or any truthy value when passing to API functions. */
typedef uint32_t b32;

/* ---------- tunables --------------------------------------------------- */

#ifndef CSV_MMAP_THRESHOLD
#define CSV_MMAP_THRESHOLD (1u * 1024u * 1024u)   /* 1 MB */
#endif

/* ---------- types ------------------------------------------------------- */

typedef enum {
    CSV_TYPE_STRING = 0,
    CSV_TYPE_INT,
    CSV_TYPE_FLOAT,
    CSV_TYPE_BOOL,
    CSV_TYPE_NULL,
} csv_type_t;

/* How the file data buffer is owned. Named constants are clearer than the
   magic integers (0/1/-1) that were used before. */
typedef enum {
    CSV_STORAGE_HEAP     = 0,  /* read() into a malloc'd buffer        */
    CSV_STORAGE_MMAP     = 1,  /* mmap'd region                        */
    CSV_STORAGE_BORROWED = 2,  /* caller-owned buffer (csv_open_mem)   */
} csv_storage_t;

/* Error codes are positive so that CSV_OK == 0 works as a falsy test
   (`if (csv_error(c)) { ... }`). Negative codes would also work, but
   positive is simpler when stored in a typed enum field. */
typedef enum {
    CSV_OK        = 0,
    CSV_ERR_IO    = 1,
    CSV_ERR_NOMEM = 2,
    CSV_ERR_PARSE = 3,
} csv_err_t;

/* A single parsed field. `str` is always populated (malloc'd).
   Numeric/bool fields also fill the corresponding union member. */
typedef struct {
    csv_type_t  type;
    char       *str;
    b32         is_null;
    union {
        int64_t  i;
        double   f;
        int      b;
    };
} csv_field_t;

typedef struct {
    csv_field_t *fields;
    size_t       count;   /* size_t: field count cannot be negative */
    size_t       index;   /* 0-based data row number (header not counted) */
} csv_row_t;

typedef struct {
    char        **names;   /* column names from header row, or NULL */
    csv_type_t   *types;   /* per-column type hints, or NULL (auto-infer) */
    size_t        count;   /* size_t: column count cannot be negative */
} csv_schema_t;

typedef struct {
    char           *data;
    size_t          size;
    int             fd;
    csv_storage_t   storage;

    char            delimiter;
    b32             has_header;
    csv_schema_t    schema;

    const char     *pos;
    const char     *end;
    size_t          row_index;
    csv_err_t       error;
} csv_t;

/* ---------- open / close ------------------------------------------------ */

/* Open a CSV file. The delimiter is auto-detected from the first 4 KB
   (candidates: , ; \t |). Use csv_open_with_delim for an explicit delimiter. */
csv_t *csv_open(const char *path, b32 has_header);

/* Open a CSV file with an explicit delimiter character. */
csv_t *csv_open_with_delim(const char *path, char delimiter, b32 has_header);

/* Open from a caller-owned in-memory buffer. The buffer must outlive the
   csv_t; it is never freed by csv_close. */
csv_t *csv_open_mem(const char *buf, size_t len, char delimiter, b32 has_header);

/* Override column types after open. count must match the actual number of
   columns in the file. */
void   csv_set_schema(csv_t *csv, const csv_type_t *types, size_t count);

/* Release all resources. For csv_open_mem, the caller's buffer is not freed.
   Safe to call with NULL. */
void   csv_close(csv_t *csv);

/* Reset the row cursor to the first data row (skipping the header if any).
   Any iterators obtained before this call must not be advanced afterwards. */
void   csv_rewind(csv_t *csv);

/* Return the last error. CSV_OK (0) means no error. The error state is
   sticky and does not reset between calls. */
csv_err_t csv_error(csv_t *csv);

/* ---------- helpers ----------------------------------------------------- */

/* Always returns a non-NULL string representation of the field. */
const char *csv_field_str(const csv_field_t *f);

/* Returns the column index for a given name, or -1 if not found or no header. */
int         csv_col_index(csv_t *csv, const char *name);

/* ---------- row iterator ------------------------------------------------ */

typedef struct {
    csv_t     *csv;
    csv_row_t  row;
    b32        done;
} csv_row_iter_t;

/* Rewinds csv and returns an iterator positioned before the first row. */
csv_row_iter_t csv_rows(csv_t *csv);

/* Advances to the next row. Returns 1 if it->row is valid, 0 at EOF. */
b32            csv_row_next(csv_row_iter_t *it);

/* Frees resources held by the iterator. Safe to call even if done. */
void           csv_row_iter_free(csv_row_iter_t *it);

/* ---------- column iterator --------------------------------------------- */

/* Iterates values in a single column across all rows without materialising
   each full row — useful for aggregating over wide tables. */
typedef struct {
    csv_t       *csv;
    int          col;
    csv_field_t  field;
    size_t       row_index;
    b32          done;
    /* internal cursor, kept separate from csv->pos */
    const char  *_pos;
} csv_col_iter_t;

csv_col_iter_t csv_column(csv_t *csv, int col);
csv_col_iter_t csv_column_by_name(csv_t *csv, const char *name);

/* Advances to the next value. Returns 1 if it->field is valid, 0 at EOF. */
b32            csv_col_next(csv_col_iter_t *it);

/* ---------- filtered row iterator --------------------------------------- */

/* Return non-zero from the predicate to keep the row. */
typedef int (*csv_filter_fn)(const csv_row_t *row, void *userdata);

typedef struct {
    csv_row_iter_t  _base;
    csv_filter_fn   filter;
    void           *userdata;
    b32             done;
} csv_filter_iter_t;

/* it->_base.row holds the current row when csv_filter_next returns 1. */
csv_filter_iter_t csv_filter(csv_t *csv, csv_filter_fn fn, void *userdata);
b32               csv_filter_next(csv_filter_iter_t *it);

/* =========================================================================
 * IMPLEMENTATION
 * ========================================================================= */
#ifdef CSV_IMPLEMENTATION

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/mman.h>

/* ---- internal: delimiter detection ------------------------------------ */

static char csv__detect_delim(const char *data, size_t len) {
    static const char cands[] = {',', ';', '\t', '|'};
    int counts[4] = {0};
    size_t scan = len < 4096 ? len : 4096;
    for (size_t i = 0; i < scan; i++)
        for (int j = 0; j < 4; j++)
            if (data[i] == cands[j]) counts[j]++;
    int best = 0;
    for (int j = 1; j < 4; j++)
        if (counts[j] > counts[best]) best = j;
    return counts[best] > 0 ? cands[best] : ',';
}

/* ---- internal: field parser ------------------------------------------- */

/* Parse one field from *p, advance past the delimiter or line ending.
   Sets *eol when this is the last field on the line.
   Returns a malloc'd string; ownership is transferred to the caller. */
static char *csv__parse_field(const char **p, const char *end, char delim, b32 *eol) {
    const char *s = *p;
    *eol = 0;

    size_t cap = 64, len = 0;
    char  *out = malloc(cap);
    if (!out) abort();

#define PUSH(c) do { \
    if (len + 1 >= cap) { \
        cap *= 2; \
        char *_t = realloc(out, cap); \
        if (!_t) abort(); \
        out = _t; \
    } \
    out[len++] = (c); \
} while (0)

    if (s < end && *s == '"') {
        s++;
        while (s < end) {
            if (*s == '"') {
                s++;
                if (s < end && *s == '"') { PUSH('"'); s++; }
                else break;
            } else {
                PUSH(*s++);
            }
        }
        /* consume trailing junk up to delimiter */
        while (s < end && *s != delim && *s != '\n' && *s != '\r') s++;
    } else {
        while (s < end && *s != delim && *s != '\n' && *s != '\r')
            PUSH(*s++);
        /* trim trailing spaces for unquoted fields */
        while (len > 0 && out[len - 1] == ' ') len--;
    }
    PUSH('\0');

#undef PUSH

    if (s >= end || *s == '\n' || *s == '\r') {
        *eol = 1;
        if (s < end && *s == '\r') s++;
        if (s < end && *s == '\n') s++;
    } else {
        s++; /* skip delimiter */
    }
    *p = s;
    return out;
}

/* ---- internal: type inference & field construction -------------------- */

static csv_type_t csv__infer(const char *s) {
    if (!s || !*s) return CSV_TYPE_NULL;
    /* "1" and "0" are treated as integers, not bools. Accepting bare digits
       as booleans conflates two distinct types and makes INT inference
       unreliable for any single-digit column. Only explicit true/false strings
       are recognised as boolean. */
    if (!strcmp(s,"true") || !strcmp(s,"false") ||
        !strcmp(s,"TRUE") || !strcmp(s,"FALSE"))
        return CSV_TYPE_BOOL;
    char *e;
    strtoll(s, &e, 10);
    if (*e == '\0') return CSV_TYPE_INT;
    strtod(s, &e);
    if (*e == '\0') return CSV_TYPE_FLOAT;
    return CSV_TYPE_STRING;
}

/* Takes ownership of raw (must be a malloc'd buffer from csv__parse_field).
   Assigns it directly to f.str, avoiding a second allocation and copy.
   strtoll/strtod are called again here even though csv__infer already called
   them. This duplication is acceptable: inference is a classification scan
   that discards the parsed value; merging the two steps would require
   threading parsed values through the type system for a gain that is
   negligible compared to I/O. */
static csv_field_t csv__make_field(char *raw, csv_type_t hint) {
    csv_field_t f = {0};
    f.str = raw;  /* always takes ownership */
    if (!raw || !*raw) {
        f.type    = CSV_TYPE_NULL;
        f.is_null = 1;
        return f;
    }
    csv_type_t t = (hint == CSV_TYPE_NULL) ? csv__infer(raw) : hint;
    f.type = t;
    switch (t) {
        case CSV_TYPE_INT:   f.i = strtoll(raw, NULL, 10); break;
        case CSV_TYPE_FLOAT: f.f = strtod(raw, NULL);      break;
        case CSV_TYPE_BOOL:  f.b = (raw[0]=='t' || raw[0]=='T'); break;
        default: break;
    }
    return f;
}

static void csv__free_row(csv_row_t *row) {
    for (size_t i = 0; i < row->count; i++) free(row->fields[i].str);
    free(row->fields);
    row->fields = NULL;
    row->count  = 0;
}

/* ---- internal: parse one row from csv->pos ---------------------------- */

/* Precondition: row must be zero-initialised (fields == NULL, count == 0).
   This function is internal and only called with stack-allocated or freshly
   zeroed rows, so the precondition holds at every call site. Calling it on
   a row that already holds data would leak that data. */
static int csv__next_row(csv_t *csv, csv_row_t *row) {
    /* skip blank lines */
    while (csv->pos < csv->end && (*csv->pos == '\r' || *csv->pos == '\n'))
        csv->pos++;
    if (csv->pos >= csv->end) return 0;

    csv_field_t *fields = NULL;
    size_t count = 0, cap = 0;

    while (1) {
        b32 eol = 0;
        /* raw is malloc'd by csv__parse_field; ownership transfers to
           csv__make_field, which assigns it directly to field.str. */
        char *raw = csv__parse_field(&csv->pos, csv->end, csv->delimiter, &eol);

        if (count >= cap) {
            cap = cap ? cap * 2 : 16;
            csv_field_t *tmp = realloc(fields, cap * sizeof(csv_field_t));
            if (!tmp) abort();
            fields = tmp;
        }

        csv_type_t hint = CSV_TYPE_NULL;
        if (csv->schema.types && count < csv->schema.count)
            hint = csv->schema.types[count];

        fields[count++] = csv__make_field(raw, hint);
        if (eol) break;
    }

    row->fields = fields;
    row->count  = count;
    row->index  = csv->row_index++;
    return 1;
}

/* ---- internal: shared init -------------------------------------------- */

static int csv__init(csv_t *csv) {
    if (csv->delimiter == 0)
        csv->delimiter = csv__detect_delim(csv->data, csv->size);

    csv->pos = csv->data;
    csv->end = csv->data + csv->size;

    if (csv->has_header) {
        csv_row_t hdr = {0};
        if (!csv__next_row(csv, &hdr)) return CSV_ERR_PARSE;
        csv->schema.names = malloc(hdr.count * sizeof(char *));
        if (!csv->schema.names) abort();
        csv->schema.count = hdr.count;
        for (size_t i = 0; i < hdr.count; i++) {
            csv->schema.names[i] = hdr.fields[i].str;
            hdr.fields[i].str    = NULL; /* transfer ownership */
        }
        csv__free_row(&hdr);
    }
    return CSV_OK;
}

static csv_t *csv__alloc(char delimiter, b32 has_header) {
    /* calloc(1, n) allocates exactly one element of n bytes, zero-initialised.
       calloc(0, n) is implementation-defined and must not be used to obtain a
       valid object. The '1' is an element count, not a byte count. */
    csv_t *c = calloc(1, sizeof(*c));
    if (!c) abort();
    c->delimiter  = delimiter;
    c->has_header = has_header;
    c->fd         = -1;
    return c;
}

/* ---- public: open / close --------------------------------------------- */

csv_t *csv_open_with_delim(const char *path, char delimiter, b32 has_header) {
    int fd = open(path, O_RDONLY);
    if (fd < 0) return NULL;

    struct stat st;
    if (fstat(fd, &st) < 0) { close(fd); return NULL; }

    csv_t *c = csv__alloc(delimiter, has_header);
    c->fd   = fd;
    c->size = (size_t)st.st_size;

    if (c->size >= CSV_MMAP_THRESHOLD) {
        c->data = mmap(NULL, c->size, PROT_READ, MAP_PRIVATE, fd, 0);
        if (c->data != MAP_FAILED) { c->storage = CSV_STORAGE_MMAP; goto init; }
        c->data = NULL;
    }

    /* fallback: read into heap */
    c->data = malloc(c->size + 1);
    if (!c->data) abort();
    {
        ssize_t r = read(fd, c->data, c->size);
        if (r < 0) { free(c->data); close(fd); free(c); return NULL; }
        c->size = (size_t)r;
    }
    c->data[c->size] = '\0';
    c->storage = CSV_STORAGE_HEAP;

init:
    if (csv__init(c) != CSV_OK) { csv_close(c); return NULL; }
    return c;
}

csv_t *csv_open(const char *path, b32 has_header) {
    return csv_open_with_delim(path, 0, has_header);
}

csv_t *csv_open_mem(const char *buf, size_t len, char delimiter, b32 has_header) {
    csv_t *c = csv__alloc(delimiter, has_header);
    c->data    = (char *)buf;
    c->size    = len;
    c->storage = CSV_STORAGE_BORROWED;
    if (csv__init(c) != CSV_OK) { csv_close(c); return NULL; }
    return c;
}

void csv_set_schema(csv_t *csv, const csv_type_t *types, size_t count) {
    free(csv->schema.types);
    csv->schema.types = malloc(count * sizeof(csv_type_t));
    if (!csv->schema.types) abort();
    memcpy(csv->schema.types, types, count * sizeof(csv_type_t));
    /* Only update count if we don't have names yet, to avoid mismatch. */
    if (!csv->schema.names) csv->schema.count = count;
}

void csv_close(csv_t *csv) {
    if (!csv) return;
    if      (csv->storage == CSV_STORAGE_MMAP) munmap(csv->data, csv->size);
    else if (csv->storage == CSV_STORAGE_HEAP) free(csv->data);
    if (csv->fd >= 0) close(csv->fd);
    if (csv->schema.names)
        for (size_t i = 0; i < csv->schema.count; i++)
            free(csv->schema.names[i]);
    free(csv->schema.names);
    free(csv->schema.types);
    free(csv);
}

void csv_rewind(csv_t *csv) {
    csv->pos       = csv->data;
    csv->row_index = 0;
    if (csv->has_header) {
        csv_row_t dummy = {0};
        csv__next_row(csv, &dummy);
        csv__free_row(&dummy);
        csv->row_index = 0; /* header doesn't count */
    }
}

csv_err_t   csv_error(csv_t *csv)               { return csv->error; }
const char *csv_field_str(const csv_field_t *f) { return f->str ? f->str : ""; }

int csv_col_index(csv_t *csv, const char *name) {
    if (!csv->schema.names || !name) return -1;
    for (size_t i = 0; i < csv->schema.count; i++)
        if (csv->schema.names[i] && strcmp(csv->schema.names[i], name) == 0)
            return (int)i;
    return -1;
}

/* ---- row iterator ------------------------------------------------------ */

csv_row_iter_t csv_rows(csv_t *csv) {
    csv_rewind(csv);
    csv_row_iter_t it = {0};
    it.csv = csv;
    return it;
}

b32 csv_row_next(csv_row_iter_t *it) {
    if (it->done) return 0;
    csv__free_row(&it->row);
    if (!csv__next_row(it->csv, &it->row)) { it->done = 1; return 0; }
    return 1;
}

void csv_row_iter_free(csv_row_iter_t *it) {
    csv__free_row(&it->row);
}

/* ---- column iterator --------------------------------------------------- */

static const char *csv__data_start(csv_t *csv) {
    const char *saved_pos = csv->pos;
    size_t      saved_idx = csv->row_index;
    csv->pos       = csv->data;
    csv->row_index = 0;
    if (csv->has_header) {
        csv_row_t dummy = {0};
        csv__next_row(csv, &dummy);
        csv__free_row(&dummy);
    }
    const char *start = csv->pos;
    csv->pos          = saved_pos;
    csv->row_index    = saved_idx;
    return start;
}

csv_col_iter_t csv_column(csv_t *csv, int col) {
    csv_col_iter_t it = {0};
    it.csv  = csv;
    it.col  = col;
    it._pos = csv__data_start(csv);
    return it;
}

csv_col_iter_t csv_column_by_name(csv_t *csv, const char *name) {
    return csv_column(csv, csv_col_index(csv, name));
}

b32 csv_col_next(csv_col_iter_t *it) {
    if (it->done || it->col < 0) return 0;

    free(it->field.str);
    it->field = (csv_field_t){0};

    const char *saved_pos = it->csv->pos;
    size_t      saved_idx = it->csv->row_index;
    it->csv->pos       = it->_pos;
    it->csv->row_index = it->row_index;

    csv_row_t row = {0};
    int ok = csv__next_row(it->csv, &row);

    it->_pos      = it->csv->pos;
    it->row_index = it->csv->row_index;
    it->csv->pos       = saved_pos;
    it->csv->row_index = saved_idx;

    if (!ok) { it->done = 1; return 0; }

    if ((size_t)it->col < row.count) {
        it->field               = row.fields[it->col];
        row.fields[it->col].str = NULL; /* take ownership */
    }
    csv__free_row(&row);
    return 1;
}

/* ---- filtered row iterator -------------------------------------------- */

csv_filter_iter_t csv_filter(csv_t *csv, csv_filter_fn fn, void *userdata) {
    csv_filter_iter_t it = {0};
    it._base    = csv_rows(csv);
    it.filter   = fn;
    it.userdata = userdata;
    return it;
}

b32 csv_filter_next(csv_filter_iter_t *it) {
    if (it->done) return 0;
    while (csv_row_next(&it->_base)) {
        if (!it->filter || it->filter(&it->_base.row, it->userdata))
            return 1;
    }
    it->done = 1;
    return 0;
}

#endif /* CSV_IMPLEMENTATION */
#endif /* CSV_H */
