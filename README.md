# csv.h — single-header CSV parser
## USAGE
  `#define CSV_IMPLEMENTATION` in one translation unit before including.
## FEATURES
  - Configurable delimiter (or auto-detect from , ; \t |)
  - mmap for files >= `CSV_MMAP_THRESHOLD` (default 1 MB), read() fallback
  - Optional header row parsed into named schema
  - Per-column type hints (STRING / INT / FLOAT / BOOL) or auto-inference
  - Row iterator, column iterator, filtered row iterator
  - In-memory buffer mode (csv_open_mem)
## QUICK EXAMPLE
```c
  #define CSV_IMPLEMENTATION
  #include "csv.h"
  csv_csv *c = csv_open("data.csv", 1);   // auto-detect delimiter
  csv_row_iter it = csv_rows(c);
  while (csv_row_next(&it)) {
      csv_row *row = &it.row;
      printf("%s\n", csv_field_str(&row->fields[0]));
  }
  csv_row_iter_free(&it);
  csv_close(c);
```
