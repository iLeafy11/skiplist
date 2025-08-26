#define _GNU_SOURCE
#include "skiplist.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <unistd.h>

#ifndef container_of
#define container_of(ptr, type, member)                                                            \
    __extension__({                                                                                \
        const __typeof__(((type *)0)->member) *__pmember = (ptr);                                  \
        (type *)((char *)__pmember - offsetof(type, member));                                      \
    })
#endif

#define sl_entry(ptr, type, member) container_of(ptr, type, member)

#define INIT_SL_NODE_CIRCULAR(node, level)                                                         \
    do {                                                                                           \
        (node)->lv = (level);                                                                      \
        (node)->list = calloc(level, sizeof(sl_node_ptr));                                         \
        for (int i = 0; i < level; i++) {                                                          \
            (node)->list[i].next = (node);                                                         \
            (node)->list[i].prev = (node);                                                         \
        }                                                                                          \
    } while (0)

#define INIT_SL_HEAD(sl, level)                                                                    \
    do {                                                                                           \
        (sl)->lv = (level);                                                                        \
        INIT_SL_NODE_CIRCULAR(&(sl)->head, level);                                                 \
    } while (0)

#define sl_for_each_safe_at_level(pos, tmp, head, level)                                           \
    for ((pos) = (head)->list[level].next, (tmp) = (pos)->list[level].next; (pos) != (head);       \
         (pos) = (tmp), (tmp) = (pos)->list[level].next)

#define sl_for_each_entry_from_at_level(pos, head, level, type, member)                            \
    for (; &((pos)->member) != (head);                                                             \
         (pos) = sl_entry((pos)->member.list[level].next, type, member))

#define sl_empty_at_level(node, i) (node == (node)->list[i].next)

static inline void sl_node_add_at_level(sl_node *new, sl_node *prev, int lv) {
    sl_node *next = prev->list[lv].next;
    new->list[lv].next = next;
    new->list[lv].prev = prev;
    prev->list[lv].next = new;
    next->list[lv].prev = new;
}

static inline void sl_node_remove_at_level(sl_node *n, int lv) {
    n->list[lv].prev->list[lv].next = n->list[lv].next;
    n->list[lv].next->list[lv].prev = n->list[lv].prev;
}

static inline void sl_node_remove(sl_node *n) {
    for (int i = 0; i < n->lv; i++)
        sl_node_remove_at_level(n, i);
}

static inline int random_level() {
    static const double inv_rand_max = 1.0 / RAND_MAX;
    int lv = 1;
    while ((random() * inv_rand_max) < P && lv < MAX_LEVEL)
        lv++;
    return lv;
}

#include <stdint.h>

void init_random(void) {
    struct timeval tm;
    gettimeofday(&tm, NULL);
    srandom((unsigned int)((tm.tv_sec ^ tm.tv_usec) ^ getpid()));
}

sl *sl_create() {
    sl *skplist = calloc(1, sizeof(sl));
    if (!skplist)
        return NULL;
    INIT_SL_HEAD(skplist, MAX_LEVEL);
    return skplist;
}

sl_node *sl_lookup(sl *skplist, const char *path, sl_node **update) {
    sl_node *pos = &skplist->head;
    for (int i = skplist->lv - 1; i >= 0; i--) {
        while (pos->list[i].next != &skplist->head) {
            sl_node *next = pos->list[i].next;
            int cmp = strcmp(sl_entry(next, sl_data, node)->path, path);
            if (!cmp)
                return next;

            if (cmp > 0)
                break;

            pos = next;
        }
        if (update)
            update[i] = pos;
    }

    return NULL;
}

static inline int _sl_insert(sl *skplist, sl_node *new_node, sl_node **update) {
    if (new_node->lv > skplist->lv) {
        for (int i = skplist->lv; i < new_node->lv; i++)
            update[i] = &skplist->head;

        skplist->lv = new_node->lv;
    }

    for (int i = 0; i < new_node->lv; i++)
        sl_node_add_at_level(new_node, update[i], i);

    return 0;
}

int sl_insert(sl *skplist, const char *path, double pgrk) {
    sl_node *update[MAX_LEVEL];
    for (int i = 0; i < MAX_LEVEL; i++)
        update[i] = &skplist->head;

    if (!path)
        return -1;

    if (sl_lookup(skplist, path, update))
        return -1;

    sl_data *entry = calloc(1, sizeof(sl_data));
    if (!entry)
        return -1;

    snprintf(entry->path, PATH_MAX, "%s", path);
    entry->pgrk = pgrk;
    
    int new_lv = random_level();
    INIT_SL_NODE_CIRCULAR(&entry->node, new_lv);
    return _sl_insert(skplist, &entry->node, update);
}

int sl_delete(sl *skplist, const char *path) {
    if (!path)
        return -1;

    sl_node *target = sl_lookup(skplist, path, NULL);
    if (!target)
        return -1;

    sl_node_remove(target);
    sl_data *data = sl_entry(target, sl_data, node);

    free(target->list);
    free(data);

    while (skplist->lv > 1 && sl_empty_at_level(&skplist->head, skplist->lv - 1))
        skplist->lv--;

    return 0;
}

static inline sl_node *sl_lookup_prefix_lower_bound(sl *skplist, const char *prefix) {
    sl_node *pos = &skplist->head;
    for (int i = skplist->lv - 1; i >= 0; i--) {
        while (pos->list[i].next != &skplist->head &&
               strcmp(sl_entry(pos->list[i].next, sl_data, node)->path, prefix) < 0) {
            pos = pos->list[i].next;
        }
    }

    pos = pos->list[0].next;
    if (pos != &skplist->head) {
        return pos;
    }

    return NULL;
}

int sl_lookup_prefix(sl *skplist, const char *prefix, sl_data **results, int max) {
    int prefix_len = strlen(prefix);
    sl_node *head = &skplist->head;
    sl_node *n = sl_lookup_prefix_lower_bound(skplist, prefix);
    if (!n)
        return 0;

    sl_data *entry = sl_entry(n, sl_data, node);
    int count = 0;

    sl_for_each_entry_from_at_level(entry, head, 0, sl_data, node) {
        if (strncmp(entry->path, prefix, prefix_len) || count >= max)
            break;

        results[count++] = entry;
    }
    return count;
}

void sl_free(sl *skplist) {
    sl_node *pos, *tmp;
    sl_for_each_safe_at_level(pos, tmp, &skplist->head, 0) {
        sl_data *data = sl_entry(pos, sl_data, node);
        free(data->node.list);
        free(data);
    }
    free(skplist->head.list);
    free(skplist);
}
