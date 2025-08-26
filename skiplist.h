#ifndef SKIPLIST_H
#define SKIPLIST_H

#include <limits.h>
#include <stddef.h>

#define MAX_LEVEL 16
#define P 0.5
// #define PATH_MAX 1024
#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>

typedef struct sl_node_ptr {
    struct sl_node *next, *prev;
} sl_node_ptr;

typedef struct sl_node {
    struct sl_node_ptr *list;
    int lv;
} sl_node;

typedef struct sl_data {
    char path[PATH_MAX];
    double pgrk;
    struct sl_node node;
} sl_data;

typedef struct skiplist {
    struct sl_node head;
    int lv;
} sl;

#ifdef __cplusplus
extern "C" {
#endif

void init_random(void);
sl *sl_create(void);
sl_node *sl_lookup(sl *sl, const char *path, sl_node **update);
int sl_insert(sl *sl, const char *path, double pgrk);
int sl_delete(sl *sl, const char *path);
int sl_lookup_prefix(sl *sl, const char *prefix, sl_data **results, int max);
void sl_free(sl *sl);

#ifdef __cplusplus
}
#endif

#endif /* SKIPLIST_H */
