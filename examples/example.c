#define CSV_IMPLEMENTATION
#include "csv.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* ---- test helpers ---------------------------------------------------- */

static int g_failures = 0;

#define CHECK(cond, msg) do { \
    if (!(cond)) { fprintf(stderr, "FAIL: %s\n", msg); g_failures++; } \
    else         { printf("  ok: %s\n", msg); } \
} while (0)

/* ---- filter: keep rows where age > 30 -------------------------------- */

static int filter_older_than_30(const csv_row_t *row, void *userdata) {
    csv_t *csv = (csv_t *)userdata;
    int col = csv_col_index(csv, "age");
    if (col < 0 || (size_t)col >= row->count) return 0;
    return row->fields[col].i > 30;
}

/* ---- shared validation ----------------------------------------------- */

static void validate_file(const char *path, char delim, const char *label) {
    printf("\n=== %s ===\n", label);

    /* Use csv_open (auto-detect) when delim==0, explicit otherwise */
    csv_t *c = delim ? csv_open_with_delim(path, delim, 1) : csv_open(path, 1);
    CHECK(c != NULL, "open succeeded");
    if (!c) return;

    csv_type_t types[] = { CSV_TYPE_STRING, CSV_TYPE_INT,
                           CSV_TYPE_FLOAT,  CSV_TYPE_BOOL };
    csv_set_schema(c, types, 4);

    /* --- column names ------------------------------------------------- */
    CHECK(csv_col_index(c, "name")   == 0,  "column 'name'   at index 0");
    CHECK(csv_col_index(c, "age")    == 1,  "column 'age'    at index 1");
    CHECK(csv_col_index(c, "salary") == 2,  "column 'salary' at index 2");
    CHECK(csv_col_index(c, "active") == 3,  "column 'active' at index 3");
    CHECK(csv_col_index(c, "ghost")  == -1, "unknown column returns -1");

    /* --- row count ---------------------------------------------------- */
    int rows = 0;
    csv_row_iter_t rit = csv_rows(c);
    while (csv_row_next(&rit)) rows++;
    csv_row_iter_free(&rit);
    CHECK(rows == 6, "row count == 6");

    /* --- spot-check first row ----------------------------------------- */
    csv_row_iter_t rit2 = csv_rows(c);
    csv_row_next(&rit2);
    csv_row_t *r = &rit2.row;
    CHECK(r->count == 4,                           "first row has 4 fields");
    CHECK(strcmp(r->fields[0].str, "Alice") == 0,  "first row name  == Alice");
    CHECK(r->fields[1].i   == 35,                  "first row age   == 35");
    CHECK(r->fields[2].f   == 72500.50,            "first row salary == 72500.50");
    CHECK(r->fields[3].b   == 1,                   "first row active == true");
    CHECK(r->fields[3].type == CSV_TYPE_BOOL,      "active field type == BOOL");
    csv_row_iter_free(&rit2);

    /* --- column iterator ---------------------------------------------- */
    double salary_sum = 0.0;
    csv_col_iter_t cit = csv_column_by_name(c, "salary");
    while (csv_col_next(&cit)) salary_sum += cit.field.f;
    CHECK(salary_sum > 0.0, "salary column sum > 0");
    printf("  salary sum: %.2f\n", salary_sum);

    /* --- filter: age > 30 --------------------------------------------- */
    int filtered = 0;
    csv_filter_iter_t fit = csv_filter(c, filter_older_than_30, c);
    while (csv_filter_next(&fit)) {
        csv_row_t *fr = &fit._base.row;
        CHECK(fr->fields[1].i > 30, "filtered row age > 30");
        filtered++;
    }
    /* Alice(35), Carol(42), Dave(31), Frank(55) = 4 */
    CHECK(filtered == 4, "filter age>30 yields 4 rows");

    /* --- error state -------------------------------------------------- */
    CHECK(csv_error(c) == CSV_OK, "no error after normal use");

    csv_close(c);
}

/* ---- test in-memory buffer ------------------------------------------- */

static void test_mem(void) {
    printf("\n=== in-memory buffer ===\n");
    const char *buf = "x,y,z\n10,20,30\n40,50,60\n";
    csv_t *c = csv_open_mem(buf, strlen(buf), ',', 1);
    CHECK(c != NULL, "csv_open_mem succeeded");
    if (!c) return;

    CHECK(csv_col_index(c, "x") == 0, "mem: col 'x' == 0");
    CHECK(csv_col_index(c, "z") == 2, "mem: col 'z' == 2");

    int rows = 0;
    csv_row_iter_t it = csv_rows(c);
    while (csv_row_next(&it)) rows++;
    csv_row_iter_free(&it);
    CHECK(rows == 2, "mem: row count == 2");

    csv_close(c);
}

/* ---- test auto-detect delimiter -------------------------------------- */

static void test_autodetect(void) {
    printf("\n=== auto-detect delimiter ===\n");

    csv_t *c = csv_open("examples/data.csv", 1);
    CHECK(c != NULL, "auto-detect comma file opened");
    if (c) { CHECK(c->delimiter == ',', "auto-detected ','"); csv_close(c); }

    csv_t *s = csv_open("examples/data_semicolon.csv", 1);
    CHECK(s != NULL, "auto-detect semicolon file opened");
    if (s) { CHECK(s->delimiter == ';', "auto-detected ';'"); csv_close(s); }
}

/* ---- test "1"/"0" are INT, not BOOL ---------------------------------- */

static void test_infer_types(void) {
    printf("\n=== type inference ===\n");
    const char *buf = "val\n1\n0\ntrue\nfalse\n3.14\n42\nhello\n";
    csv_t *c = csv_open_mem(buf, strlen(buf), ',', 1);
    CHECK(c != NULL, "infer test file opened");
    if (!c) return;

    csv_type_t expected[] = {
        CSV_TYPE_INT,    /* "1"     */
        CSV_TYPE_INT,    /* "0"     */
        CSV_TYPE_BOOL,   /* "true"  */
        CSV_TYPE_BOOL,   /* "false" */
        CSV_TYPE_FLOAT,  /* "3.14"  */
        CSV_TYPE_INT,    /* "42"    */
        CSV_TYPE_STRING, /* "hello" */
    };
    int idx = 0;
    csv_col_iter_t cit = csv_column_by_name(c, "val");
    while (csv_col_next(&cit)) {
        CHECK(cit.field.type == expected[idx], csv_field_str(&cit.field));
        idx++;
    }
    CHECK(idx == 7, "all 7 inference cases checked");

    csv_close(c);
}

/* ---- main ------------------------------------------------------------ */

int main(void) {
    validate_file("examples/data.csv",           0,   "data.csv (auto-detect comma)");
    validate_file("examples/data_semicolon.csv", 0,   "data_semicolon.csv (auto-detect semicolon)");
    validate_file("examples/data_space.csv",     ' ', "data_space.csv (explicit space)");
    test_mem();
    test_autodetect();
    test_infer_types();

    printf("\n%s  (%d failure%s)\n",
           g_failures == 0 ? "ALL TESTS PASSED" : "TESTS FAILED",
           g_failures, g_failures == 1 ? "" : "s");

    return g_failures ? 1 : 0;
}
