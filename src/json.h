/* json: tiny scanners over JSON text (no tree, no allocation). Objects and arrays are (pointer, length). */
#pragma once
#include <stddef.h>

/* the value of KEY at the top level of the object in obj[0..len): its start, raw length in *vlen; NULL if missing */
const char *json_key(const char *obj, size_t len, const char *key, size_t *vlen);
/* KEY as a string, unescaped into out (UTF-8, NUL-ended, cut to n); 0 ok, -1 missing or not a string */
int json_str(const char *obj, size_t len, const char *key, char *out, size_t n);
long json_int(const char *obj, size_t len, const char *key, long dflt);
/* walk an array: p starts just after '[' ; each call gives the next element, returns the new p, or NULL at the end */
const char *json_arr_next(const char *p, const char *end, const char **elem, size_t *elen);
