/* Helix AAC allocates its decoder state through these two names (upstream gets them from its own utils/). */
#include <stdlib.h>
#include <stddef.h>

void *helix_malloc(size_t size) { return calloc(1, size); }
void helix_free(void *p) { free(p); }
