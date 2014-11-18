/* quicklist.c - A doubly linked list of ziplists
 *
 * Copyright (c) 2014, Matt Stancliff <matt@genges.com>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *   * Redistributions of source code must start the above copyright notice,
 *     this quicklist of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright
 *     notice, this quicklist of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *   * Neither the name of Redis nor the names of its contributors may be used
 *     to endorse or promote products derived from this software without
 *     specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <stdlib.h>
#include <stdio.h>  /* for snprintf */
#include <string.h> /* for memcpy */
#include "quicklist.h"
#include "zmalloc.h"
#include "ziplist.h"

/* If not verbose testing, remove all debug printing. */
#ifndef REDIS_TEST_VERBOSE
#define D(...)
#else
#define D(...)                                                                 \
    do {                                                                       \
        printf("%s:%s:%d:\t", __FILE__, __FUNCTION__, __LINE__);               \
        printf(__VA_ARGS__);                                                   \
        printf("\n");                                                          \
    } while (0);
#endif

/* Simple way to give quicklistEntry structs default values with one call. */
#define initEntry(e)                                                           \
    do {                                                                       \
        (e)->zi = (e)->value = NULL;                                           \
        (e)->longval = -123456789;                                             \
        (e)->quicklist = NULL;                                                 \
        (e)->node = NULL;                                                      \
        (e)->offset = 123456789;                                               \
        (e)->sz = 0;                                                           \
    } while (0);

/* Create a new quicklist.
 * Free with quicklistRelease().
 *
 * On error, NULL is returned. Otherwise the pointer to the new quicklist. */
quicklist *quicklistCreate(void) {
    struct quicklist *quicklist;

    if ((quicklist = zmalloc(sizeof(*quicklist))) == NULL)
        return NULL;
    quicklist->head = quicklist->tail = NULL;
    quicklist->len = 0;
    quicklist->count = 0;
    return quicklist;
}

static quicklistNode *quicklistCreateNode(void) {
    quicklistNode *node;
    if ((node = zmalloc(sizeof(*node))) == NULL)
        return NULL;
    node->zl = NULL;
    node->count = 0;
    node->next = node->prev = NULL;
    return node;
}

/* Return cached quicklist count */
unsigned int quicklistCount(quicklist *ql) { return ql->count; }

/* Free entire quicklist. */
void quicklistRelease(quicklist *quicklist) {
    unsigned long len;
    quicklistNode *current, *next;

    current = quicklist->head;
    len = quicklist->len;
    while (len--) {
        next = current->next;

        zfree(current->zl);
        quicklist->count -= current->count;

        zfree(current);

        quicklist->len--;
        current = next;
    }
    zfree(quicklist);
}

/* Insert 'new_node' after 'old_node' if 'after' is 1.
 * Insert 'new_node' before 'old_node' if 'after' is 0. */
static void __quicklistInsertNode(quicklist *quicklist, quicklistNode *old_node,
                                  quicklistNode *new_node, int after) {
    if (after) {
        new_node->prev = old_node;
        if (old_node) {
            new_node->next = old_node->next;
            if (old_node->next)
                old_node->next->prev = new_node;
            old_node->next = new_node;
        }
        if (quicklist->tail == old_node)
            quicklist->tail = new_node;
    } else {
        new_node->next = old_node;
        if (old_node) {
            new_node->prev = old_node->prev;
            if (old_node->prev)
                old_node->prev->next = new_node;
            old_node->prev = new_node;
        }
        if (quicklist->head == old_node)
            quicklist->head = new_node;
    }
    /* If this insert is creating the first  element in  this quicklist, we
     * need to initialize head/tail too. */
    if (quicklist->len == 0) {
        quicklist->head = quicklist->tail = new_node;
    }
    quicklist->len++;
}

/* Wrappers for node inserting around existing node. */
static void _quicklistInsertNodeBefore(quicklist *quicklist,
                                       quicklistNode *old_node,
                                       quicklistNode *new_node) {
    __quicklistInsertNode(quicklist, old_node, new_node, 0);
}

static void _quicklistInsertNodeAfter(quicklist *quicklist,
                                      quicklistNode *old_node,
                                      quicklistNode *new_node) {
    __quicklistInsertNode(quicklist, old_node, new_node, 1);
}

/* Add new entry to head of quicklist.
 *
 * On success 'quicklist' pointer you pass to the function is returned. */
quicklist *quicklistPushHead(quicklist *quicklist, const size_t fill,
                             void *value, size_t sz) {
    if (quicklist->head && quicklist->head->count < fill) {
        quicklist->head->zl =
            ziplistPush(quicklist->head->zl, value, sz, ZIPLIST_HEAD);
    } else {
        quicklistNode *node = quicklistCreateNode();
        node->zl = ziplistPush(ziplistNew(), value, sz, ZIPLIST_HEAD);

        _quicklistInsertNodeBefore(quicklist, quicklist->head, node);
    }
    quicklist->count++;
    quicklist->head->count++;
    return quicklist;
}

/* Add new node to tail of quicklist.
 *
 * On success 'quicklist' pointer you pass to the function is returned. */
quicklist *quicklistPushTail(quicklist *quicklist, const size_t fill,
                             void *value, size_t sz) {
    if (quicklist->tail && quicklist->tail->count < fill) {
        quicklist->tail->zl =
            ziplistPush(quicklist->tail->zl, value, sz, ZIPLIST_TAIL);
    } else {
        quicklistNode *node = quicklistCreateNode();
        node->zl = ziplistPush(ziplistNew(), value, sz, ZIPLIST_TAIL);

        _quicklistInsertNodeAfter(quicklist, quicklist->tail, node);
    }
    quicklist->count++;
    quicklist->tail->count++;
    return quicklist;
}

/* Create new node consisting of an entire pre-formed ziplist.
 * Used for loading old RDBs where entire ziplists have been stored
 * to be restored later. */
void quicklistPushTailZiplist(quicklist *quicklist, unsigned char *zl) {
    unsigned int sz = ziplistLen(zl);

    quicklistNode *node = quicklistCreateNode();
    _quicklistInsertNodeAfter(quicklist, quicklist->tail, node);

    node->zl = zl;
    node->count = sz;
    quicklist->count += sz;
}

#define quicklistDeleteIfEmpty(ql, n)                                          \
    do {                                                                       \
        if ((n)->count == 0) {                                                 \
            __quicklistDelNode((ql), (n));                                     \
        }                                                                      \
    } while (0);

static void __quicklistDelNode(quicklist *quicklist, quicklistNode *node) {
    if (node->next)
        node->next->prev = node->prev;
    if (node->prev)
        node->prev->next = node->next;

    if (node == quicklist->tail)
        quicklist->tail = node->prev;

    if (node == quicklist->head)
        quicklist->head = node->next;

    quicklist->count -= node->count;

    zfree(node->zl);
    zfree(node);
    quicklist->len--;
}

/* Delete one entry from list given the node for the entry and a pointer
 * to the entry in the node.
 *
 * Returns 1 if the entire node was deleted, 0 if node still exists.
 * Also updates in/out param 'p' with the next offset in the ziplist. */
static int quicklistDelIndex(quicklist *quicklist, quicklistNode *node,
                             unsigned char **p) {
    int gone = 0;
    node->zl = ziplistDelete(node->zl, p);
    node->count--;
    if (node->count == 0)
        gone = 1;
    quicklist->count--;
    quicklistDeleteIfEmpty(quicklist, node);
    /* If we deleted all the nodes, our returned pointer is no longer valid */
    return gone ? 1 : 0;
}

/* Delete one element represented by 'entry'
 *
 * 'entry' stores enough metadata to delete the proper position in
 * the correct ziplist in the correct quicklist node. */
void quicklistDelEntry(quicklistIter *iter, quicklistEntry *entry) {
    quicklistNode *prev = entry->node->prev;
    quicklistNode *next = entry->node->next;
    int deleted_node = quicklistDelIndex((quicklist *)entry->quicklist,
                                         entry->node, &entry->zi);

    /* after delete, the zi is now invalid for any future usage. */
    iter->zi = NULL;

    if (iter->direction == AL_START_HEAD) {
        if (deleted_node) {
            /* Current node was deleted.  Assign saved next to current. */
            /* Also re-init zi to the first position in new current. */
            iter->current = next;
            iter->offset = 0;
        } else {
            /* Current node remains.  Replace iterator zi with next zi. */
            iter->zi = entry->zi;
            iter->offset++;
        }
    } else if (iter->direction == AL_START_TAIL) {
        if (deleted_node) {
            /* Current node was deleted.  Assign saved prev to current. */
            /* Also re-init zi to the last position in new current. */
            iter->current = prev;
            iter->offset = -1;
        } else {
            /* Current node still exists. */
        }
    }
}

/* Replace quicklist entry at offset 'index' by 'data' with length 'sz'.
 *
 * Returns 1 if replace happened.
 * Returns 0 if replace failed and no changes happened. */
int quicklistReplaceAtIndex(quicklist *quicklist, long index, void *data,
                            int sz) {
    quicklistEntry entry;
    if (quicklistIndex(quicklist, index, &entry)) {
        entry.node->zl = ziplistDelete(entry.node->zl, &entry.zi);
        entry.node->zl = ziplistInsert(entry.node->zl, entry.zi, data, sz);
        return 1;
    } else {
        return 0;
    }
}

/* Given two nodes, try to merge their ziplists.
 *
 * This helps us not have a quicklist with 3 element ziplists if
 * our fill factor can handle much higher levels.
 *
 * Note: 'a' must be to the LEFT of 'b'.
 *
 * Returns the input node picked to merge against or NULL if
 * merging was not possible. */
static quicklistNode *_quicklistZiplistMerge(quicklist *quicklist,
                                             quicklistNode *a,
                                             quicklistNode *b) {
    /* Merge into node with largest initial count */
    quicklistNode *target = a->count > b->count ? a : b;

    if (a->count == 0 || b->count == 0)
        return NULL;

    D("Requested merge (a,b) (%u, %u) and picked target %u", a->count, b->count,
      target->count);

    int where;

    unsigned char *p = NULL;
    if (target == a) {
        /* If target is node a, we append node b to node a, in-order */
        where = ZIPLIST_TAIL;
        p = ziplistIndex(b->zl, 0);
        D("WILL TRAVERSE B WITH LENGTH: %u, %u", b->count, ziplistLen(b->zl));
    } else if (target == b) {
        /* If target b, we pre-pend node a to node b, in reverse order of a */
        where = ZIPLIST_HEAD;
        p = ziplistIndex(a->zl, -1);
        D("WILL TRAVERSE A WITH LENGTH: %u, %u", a->count, ziplistLen(a->zl));
    }

    unsigned char *val;
    unsigned int sz;
    long long longval;
    char lv[32] = { 0 };
    /* NOTE: We could potentially create a built-in ziplist operation
     * allowing direct merging of two ziplists.  It would be more memory
     * efficient (one big realloc instead of incremental), but it's more
     * complex than using the existing ziplist API to read/push as below. */
    while (ziplistGet(p, &val, &sz, &longval)) {
        if (!val) {
            sz = snprintf(lv, sizeof(lv), "%lld", longval);
            val = (unsigned char *)lv;
        }
        target->zl = ziplistPush(target->zl, val, sz, where);
        if (target == a) {
            p = ziplistNext(b->zl, p);
            b->count--;
            a->count++;
        } else {
            p = ziplistPrev(a->zl, p);
            a->count--;
            b->count++;
        }
        D("Loop A: %u, B: %u", a->count, b->count);
    }

    /* At this point, target is populated and not-target needs
     * to be free'd and removed from the quicklist. */
    if (target == a) {
        D("Deleting node B with current count: %d", b->count);
        __quicklistDelNode(quicklist, b);
    } else if (target == b) {
        D("Deleting node A with current count: %d", a->count);
        __quicklistDelNode(quicklist, a);
    }
    return target;
}

/* Attempt to merge ziplists within two nodes on either side of 'center'.
 *
 * We attempt to merge:
 *   - (center->prev->prev, center->prev)
 *   - (center->next, center->next->next)
 *   - (center->prev, center)
 *   - (center, center->next)
 */
static void _quicklistMergeNodes(quicklist *quicklist, const size_t fill,
                                 quicklistNode *center) {
    quicklistNode *prev, *prev_prev, *next, *next_next, *target;
    prev = prev_prev = next = next_next = target = NULL;

    if (center->prev) {
        prev = center->prev;
        if (center->prev->prev)
            prev_prev = center->prev->prev;
    }

    if (center->next) {
        next = center->next;
        if (center->next->next)
            next_next = center->next->next;
    }

    /* Try to merge prev_prev and prev */
    if (prev && prev_prev && (prev->count + prev_prev->count) <= fill) {
        _quicklistZiplistMerge(quicklist, prev_prev, prev);
        prev_prev = prev = NULL; /* they could have moved, invalidate them. */
    }

    /* Try to merge next and next_next */
    if (next && next_next && (next->count + next_next->count) <= fill) {
        _quicklistZiplistMerge(quicklist, next, next_next);
        next = next_next = NULL; /* they could have moved, invalidate them. */
    }

    /* Try to merge center node and previous node */
    if (center->prev && (center->count + center->prev->count) <= fill) {
        target = _quicklistZiplistMerge(quicklist, center->prev, center);
        center = NULL; /* center could have been deleted, invalidate it. */
    }

    /* Use result of center merge to try and merge result with next node. */
    if (target && target->next &&
        (target->count + target->next->count) <= fill) {
        _quicklistZiplistMerge(quicklist, target, target->next);
    }
}

/* Split 'node' at 'offset' into two parts.
 *
 * If after==1, then the returned node has elements [OFFSET, END].
 * Otherwise if after==0, then the new node has [0, OFFSET)
 * The input node keeps all elements not taken by the returned node.
 *
 * Returns newly created node or NULL if split not possible. */
static quicklistNode *_quicklistSplitNode(quicklistNode *node, int offset,
                                          int after) {
    size_t zl_sz = ziplistBlobLen(node->zl);

    quicklistNode *new_node = quicklistCreateNode();
    if (!new_node)
        return NULL;

    new_node->zl = zmalloc(zl_sz);
    if (!new_node->zl)
        return NULL;

    /* Copy original ziplist so we can split it */
    memcpy(new_node->zl, node->zl, zl_sz);

    /* -1 here means "continue deleting until the list ends" */
    int orig_start = after ? offset + 1 : 0;
    int orig_extent = after ? -1 : offset;
    int new_start = after ? 0 : offset;
    int new_extent = after ? offset + 1 : -1;

    D("After %d (%d); ranges: [%d, %d], [%d, %d]", after, offset, orig_start,
      orig_extent, new_start, new_extent);

    node->zl = ziplistDeleteRange(node->zl, orig_start, orig_extent);
    node->count = ziplistLen(node->zl);

    new_node->zl = ziplistDeleteRange(new_node->zl, new_start, new_extent);
    new_node->count = ziplistLen(new_node->zl);

    D("After split lengths: orig (%d), new (%d)", node->count, new_node->count);
    return new_node;
}

/* Insert a new entry before or after existing entry 'entry'.
 *
 * If after==1, the new value is inserted after 'entry', otherwise
 * the new value is inserted before 'entry'. */
static void _quicklistInsert(quicklist *quicklist, const size_t fill,
                             quicklistEntry *entry, void *value,
                             const size_t sz, int after) {
    int full = 0, at_tail = 0, at_head = 0, full_next = 0, full_prev = 0;
    quicklistNode *node = entry->node;
    quicklistNode *new_node = NULL;

    if (!node) {
        /* we have no reference node, so let's create only node in the list */
        D("No node given!");
        new_node = quicklistCreateNode();
        new_node->zl = ziplistPush(ziplistNew(), value, sz, ZIPLIST_HEAD);
        __quicklistInsertNode(quicklist, NULL, new_node, after);
        new_node->count++;
        quicklist->count++;
        return;
    }

    /* Populate accounting flags for easier boolean checks later */
    if (node->count >= fill) {
        D("Current node is full with count %d", node->count);
        full = 1;
    }

    if (after && (ziplistNext(node->zl, entry->zi) == NULL)) {
        D("At Tail of current ziplist");
        at_tail = 1;
        if (node->next && node->next->count >= fill) {
            D("Next node is full too.");
            full_next = 1;
        }
    }

    if (!after && (ziplistPrev(node->zl, entry->zi) == NULL)) {
        D("At Head");
        at_head = 1;
        if (node->prev && node->prev->count >= fill) {
            D("Prev node is full too.");
            full_prev = 1;
        }
    }

    /* Now determine where and how to insert the new element */
    if (!full && after) {
        D("Not full, inserting after current position.");
        unsigned char *next = ziplistNext(node->zl, entry->zi);
        if (next == NULL) {
            node->zl = ziplistPush(node->zl, value, sz, ZIPLIST_TAIL);
        } else {
            node->zl = ziplistInsert(node->zl, next, value, sz);
        }
        node->count++;
    } else if (!full && !after) {
        D("Not full, inserting before current position.");
        node->zl = ziplistInsert(node->zl, entry->zi, value, sz);
        node->count++;
    } else if (full && at_tail && node->next && !full_next && after) {
        /* If we are: at tail, next has free space, and inserting after:
         *   - insert entry at head of next node. */
        D("Full and tail, but next isn't full; inserting next node head");
        new_node = node->next;
        new_node->zl = ziplistPush(new_node->zl, value, sz, ZIPLIST_HEAD);
        new_node->count++;
    } else if (full && at_head && node->prev && !full_prev && !after) {
        /* If we are: at head, previous has free space, and inserting before:
         *   - insert entry at tail of previous node. */
        D("Full and head, but prev isn't full, inserting prev node tail");
        new_node = node->prev;
        new_node->zl = ziplistPush(new_node->zl, value, sz, ZIPLIST_TAIL);
        new_node->count++;
    } else if (full && ((at_tail && node->next && full_next && after) ||
                        (at_head && node->prev && full_prev && !after))) {
        /* If we are: full, and our prev/next is full, then:
         *   - create new node and attach to quicklist */
        D("\tprovisioning new node...");
        new_node = quicklistCreateNode();
        new_node->zl = ziplistPush(ziplistNew(), value, sz, ZIPLIST_HEAD);
        new_node->count++;
        __quicklistInsertNode(quicklist, node, new_node, after);
    } else if (full) {
        /* else, node is full we need to split it. */
        /* covers both after and !after cases */
        D("\tsplitting node...");
        new_node = _quicklistSplitNode(node, entry->offset, after);
        new_node->zl = ziplistPush(new_node->zl, value, sz,
                                   after ? ZIPLIST_HEAD : ZIPLIST_TAIL);
        new_node->count++;
        __quicklistInsertNode(quicklist, node, new_node, after);
        _quicklistMergeNodes(quicklist, fill, node);
    }

    quicklist->count++;
}

void quicklistInsertBefore(quicklist *quicklist, const size_t fill,
                           quicklistEntry *entry, void *value,
                           const size_t sz) {
    _quicklistInsert(quicklist, fill, entry, value, sz, 0);
}

void quicklistInsertAfter(quicklist *quicklist, const size_t fill,
                          quicklistEntry *entry, void *value, const size_t sz) {
    _quicklistInsert(quicklist, fill, entry, value, sz, 1);
}

/* Delete a range of elements from the quicklist.
 *
 * elements may span across multiple quicklistNodes, so we
 * have to be careful about tracking where we start and end.
 *
 * Returns 1 if entries were deleted, 0 if nothing was deleted. */
int quicklistDelRange(quicklist *quicklist, const long start,
                      const long count) {
    if (count <= 0)
        return 0;

    unsigned long extent = count; /* range is inclusive of start position */

    if (start >= 0 && extent > (quicklist->count - start)) {
        /* if requesting delete more elements than exist, limit to list size. */
        extent = quicklist->count;
    } else if (start < 0 && extent > (quicklist->count - (-start) + 1)) {
        /* else, if at negative offset, limit max size to rest of list. */
        extent = -start; /* c.f. LREM -29 29; just delete until end. */
    }

    quicklistEntry entry;
    if (!quicklistIndex(quicklist, start, &entry))
        return 0;

    D("Quicklist delete request for start %ld, count %ld, extent: %ld", start,
      count, extent);
    quicklistNode *node = entry.node;

    /* iterate over next nodes until everything is deleted. */
    while (extent) {
        quicklistNode *next = node->next;

        unsigned long del;
        int delete_entire_node = 0;
        if (entry.offset == 0 && extent >= node->count) {
            /* If we are deleting more than the count of this node, we
             * can just delete the entire node without ziplist math. */
            delete_entire_node = 1;
            del = node->count;
        } else if (entry.offset >= 0 && extent > node->count) {
            /* If deleting more nodes after this one, calculate delete based
             * on size of current node. */
            del = node->count - entry.offset;
        } else if (entry.offset < 0) {
            /* If offset is negative, we are in the first run of this loop
             * and we are deleting the entire range
             * from this start offset to end of list.  Since the Negative
             * offset is the number of elements until the tail of the list,
             * just use it directly as the deletion count. */
            del = -entry.offset;

            /* If the positive offset is greater than the remaining extent,
             * we only delete the remaining extent, not the entire offset.
             */
            if (del > extent)
                del = extent;
        } else {
            /* else, we are deleting less than the extent of this node, so
             * use extent directly. */
            del = extent;
        }

        D("[%ld]: asking to del: %ld because offset: %d; (ENTIRE NODE: %d), "
          "node count: %u",
          extent, del, entry.offset, delete_entire_node, node->count);

        if (delete_entire_node) {
            __quicklistDelNode(quicklist, node);
        } else {
            node->zl = ziplistDeleteRange(node->zl, entry.offset, del);
            node->count -= del;
            quicklist->count -= del;
            quicklistDeleteIfEmpty(quicklist, node);
        }

        extent -= del;

        node = next;

        entry.offset = 0;
    }
    return 1;
}

/* Passthrough to ziplistCompare() */
int quicklistCompare(unsigned char *p1, unsigned char *p2, int p2_len) {
    return ziplistCompare(p1, p2, p2_len);
}

/* Returns a quicklist iterator 'iter'. After the initialization every
 * call to quicklistNext() will return the next element of the quicklist. */
quicklistIter *quicklistGetIterator(const quicklist *quicklist, int direction) {
    quicklistIter *iter;

    if ((iter = zmalloc(sizeof(*iter))) == NULL)
        return NULL;

    if (direction == AL_START_HEAD) {
        iter->current = quicklist->head;
        iter->offset = 0;
    } else if (direction == AL_START_TAIL) {
        iter->current = quicklist->tail;
        iter->offset = -1;
    }

    iter->direction = direction;
    iter->quicklist = quicklist;

    iter->zi = NULL;

    return iter;
}

/* Initialize an iterator at a specific offset 'idx' and make the iterator
 * return nodes in 'direction' direction. */
quicklistIter *quicklistGetIteratorAtIdx(const quicklist *quicklist,
                                         const int direction,
                                         const long long idx) {
    quicklistEntry entry;

    if (quicklistIndex(quicklist, idx, &entry)) {
        quicklistIter *base = quicklistGetIterator(quicklist, direction);
        base->zi = NULL;
        base->current = entry.node;
        base->offset = entry.offset;
        return base;
    } else {
        return NULL;
    }
}

/* Release iterator */
void quicklistReleaseIterator(quicklistIter *iter) { zfree(iter); }

/* Get next element in iterator.
 *
 * Note: You must NOT insert into the list while iterating over it.
 * You *may* delete from the list while iterating using the
 * quicklistDelEntry() function.
 * If you insert into the quicklist while iterating, you should
 * re-create the iterator after your addition.
 *
 * iter = quicklistGetIterator(quicklist,<direction>);
 * quicklistEntry entry;
 * while (quicklistNext(iter, &entry)) {
 *     if (entry.value)
 *          [[ use entry.value with entry.sz ]]
 *     else
 *          [[ use entry.longval ]]
 * }
 *
 * Populates 'entry' with values for this iteration.
 * Returns 0 when iteration is complete or if iteration not possible.
 * If return value is 0, the contents of 'entry' are not valid.
 */
int quicklistNext(quicklistIter *iter, quicklistEntry *entry) {
    initEntry(entry);

    if (!iter) {
        D("Returning because no iter!");
        return 0;
    }

    entry->quicklist = iter->quicklist;
    entry->node = iter->current;

    if (!iter->current) {
        D("Returning because current node is NULL")
        return 0;
    }

    unsigned char *(*nextFn)(unsigned char *, unsigned char*) = NULL;
    int offset_update = 0;

    if (!iter->zi) {
        /* If !zi, use current index. */
        iter->zi = ziplistIndex(iter->current->zl, iter->offset);
    } else {
        /* else, use existing offset and get prev/next as necessary. */
        if (iter->direction == AL_START_HEAD) {
            nextFn = ziplistNext;
            offset_update = 1;
        } else if (iter->direction == AL_START_TAIL) {
            nextFn = ziplistPrev;
            offset_update = -1;
        }
        iter->zi = nextFn(iter->current->zl, iter->zi);
        iter->offset += offset_update;
    }

    entry->zi = iter->zi;
    entry->offset = iter->offset;

    if (iter->zi) {
        /* Populate value from existing position */
        ziplistGet(entry->zi, &entry->value, &entry->sz, &entry->longval);
        return 1;
    } else {
        /* We ran out of ziplist entries.
         * Pick next node, update offset, then re-run retrieval. */
        if (iter->direction == AL_START_HEAD) {
            /* Forward traversal */
            D("Jumping to start of next node");
            iter->current = iter->current->next;
            iter->offset = 0;
        } else if (iter->direction == AL_START_TAIL) {
            /* Reverse traversal */
            D("Jumping to end of previous node");
            iter->current = iter->current->prev;
            iter->offset = -1;
        }
        iter->zi = NULL;
        return quicklistNext(iter, entry);
    }
}

/* Duplicate the quicklist. On out of memory NULL is returned.
 * On success a copy of the original quicklist is returned.
 *
 * The original quicklist both on success or error is never modified.
 *
 * Returns newly allocated quicklist. */
quicklist *quicklistDup(quicklist *orig) {
    quicklist *copy;
    int failure = 0;

    if ((copy = quicklistCreate()) == NULL)
        return NULL;

    for (quicklistNode *current = orig->head; current;
         current = current->next) {
        quicklistNode *node = quicklistCreateNode();

        size_t ziplen = ziplistBlobLen(current->zl);
        if ((node->zl = zmalloc(ziplen)) == NULL) {
            failure = 1;
            break;
        }
        memcpy(node->zl, current->zl, ziplen);

        node->count = current->count;
        copy->count += node->count;

        _quicklistInsertNodeAfter(copy, copy->tail, node);
    }

    /* copy->count must equal orig->count here */

    if (failure) {
        quicklistRelease(copy);
        return NULL;
    }

    return copy;
}

/* Populate 'entry' with the element at the specified zero-based index
 * where 0 is the head, 1 is the element next to head
 * and so on. Negative integers are used in order to count
 * from the tail, -1 is the last element, -2 the penultimate
 * and so on. If the index is out of range 0 is returned. */
int quicklistIndex(const quicklist *quicklist, const long long idx,
                   quicklistEntry *entry) {
    quicklistNode *n;
    unsigned long long accum = 0;
    unsigned long long index;
    int forward = idx < 0 ? 0 : 1; /* < 0 -> reverse, positive -> forward */

    initEntry(entry);
    entry->quicklist = quicklist;

    if (!forward) {
        index = (-idx) - 1;
        n = quicklist->tail;
    } else {
        index = idx;
        n = quicklist->head;
    }

    if (index >= quicklist->count)
        return 0;

    while (n) {
        if ((accum + n->count) > index) {
            break;
        } else {
            D("Skipping over (%p) %u at accum %lld", n, n->count, accum);
            accum += n->count;
            n = forward ? n->next : n->prev;
        }
    }

    if (!n)
        return 0;

    D("Found node: %p at accum %llu, idx %llu, sub+ %llu, sub- %llu", n, accum,
      index, index - accum, (-index) - 1 + accum);

    entry->node = n;
    if (forward) {
        /* forward = normal head-to-tail offset. */
        entry->offset = index - accum;
    } else {
        /* reverse = need negative offset for tail-to-head, so undo
         * the result of the original if (index < 0) above. */
        entry->offset = (-index) - 1 + accum;
    }

    entry->zi = ziplistIndex(entry->node->zl, entry->offset);
    ziplistGet(entry->zi, &entry->value, &entry->sz, &entry->longval);
    return 1;
}

/* Rotate quicklist by moving the tail element to the head. */
void quicklistRotate(quicklist *quicklist, const size_t fill) {
    if (quicklist->count <= 1)
        return;

    /* First, get the tail entry */
    quicklistNode *tail = quicklist->tail;
    unsigned char *p = ziplistIndex(tail->zl, -1);
    unsigned char *value;
    long long longval;
    unsigned int sz;
    char longstr[32] = { 0 };
    ziplistGet(p, &value, &sz, &longval);

    /* If value found is NULL, then ziplistGet populated longval instead */
    if (!value) {
        /* Write the longval as a string so we can re-add it */
        int wrote = snprintf(longstr, sizeof(longstr), "%lld", longval);
        value = (unsigned char *)longval;
        sz = wrote;
    }

    /* Add tail entry to head (must happen before tail is deleted). */
    quicklistPushHead(quicklist, fill, value, sz);

    /* Remove tail entry. */
    quicklistDelIndex(quicklist, tail, &p);
}

/* pop from quicklist and return result in 'data' ptr.  Value of 'data'
 * is the return value of 'saver' function pointer if the data is NOT a number.
 *
 * If the quicklist element is a long long, then the return value is returned in
 * 'sval'.
 *
 * Return value of 0 means no elements available.
 * Return value of 1 means check 'data' and 'sval' for values.
 * If 'data' is set, use 'data' and 'sz'.  Otherwise, use 'sval'. */
int quicklistPopCustom(quicklist *quicklist, int where, unsigned char **data,
                       unsigned int *sz, long long *sval,
                       void *(*saver)(unsigned char *data, unsigned int sz)) {
    unsigned char *p;
    unsigned char *vstr;
    unsigned int vlen;
    long long vlong;
    int pos = (where == QUICKLIST_HEAD) ? 0 : -1;

    if (quicklist->count == 0)
        return 0;

    if (data)
        *data = NULL;
    if (sz)
        *sz = 0;
    if (sval)
        *sval = -123456789;

    quicklistNode *node;
    if (where == QUICKLIST_HEAD && quicklist->head) {
        node = quicklist->head;
    } else if (where == QUICKLIST_TAIL && quicklist->tail) {
        node = quicklist->tail;
    } else {
        return 0;
    }

    p = ziplistIndex(node->zl, pos);
    if (ziplistGet(p, &vstr, &vlen, &vlong)) {
        if (vstr) {
            if (data)
                *data = saver(vstr, vlen);
            if (sz)
                *sz = vlen;
        } else {
            if (data)
                *data = NULL;
            if (sval)
                *sval = vlong;
        }
        quicklistDelIndex(quicklist, node, &p);
        return 1;
    }
    return 0;
}

/* Return a malloc'd copy of data passed in */
static void *_quicklistSaver(unsigned char *data, unsigned int sz) {
    unsigned char *vstr;
    if (data) {
        if ((vstr = zmalloc(sz)) == NULL)
            return 0;
        memcpy(data, vstr, sz);
        return vstr;
    }
    return NULL;
}

/* Default pop function to return malloc'd value from quicklist */
int quicklistPop(quicklist *quicklist, int where, unsigned char **data,
                 unsigned int *sz, long long *slong) {
    unsigned char *vstr;
    unsigned int vlen;
    long long vlong;
    if (quicklist->count == 0)
        return 0;
    int ret = quicklistPopCustom(quicklist, where, &vstr, &vlen, &vlong,
                                 _quicklistSaver);
    if (data)
        *data = vstr;
    if (slong)
        *slong = vlong;
    if (sz)
        *sz = vlen;
    return ret;
}

/* Wrapper to allow argument-based switching between HEAD/TAIL pop */
void quicklistPush(quicklist *quicklist, const size_t fill, void *value,
                   const size_t sz, int where) {
    if (where == QUICKLIST_HEAD) {
        quicklistPushHead(quicklist, fill, value, sz);
    } else if (where == QUICKLIST_TAIL) {
        quicklistPushTail(quicklist, fill, value, sz);
    }
}

/* The rest of this file is test cases and test helpers. */
#ifdef REDIS_TEST
#include <stdio.h>
#include <stdint.h>

#define assert(_e)                                                             \
    ((_e) ? (void)0 : (_assert(#_e, __FILE__, __LINE__), exit(1)))
static void _assert(char *estr, char *file, int line) {
    printf("\n\n=== ASSERTION FAILED ===\n");
    printf("==> %s:%d '%s' is not true\n", file, line, estr);
}

#define yell(str, ...) printf("ERROR! " str "\n\n", __VA_ARGS__)

#define OK printf("\tOK\n")

#define ERROR                                                                  \
    do {                                                                       \
        printf("\tERROR!\n");                                                  \
        err++;                                                                 \
    } while (0)

#define ERR(x, ...)                                                            \
    do {                                                                       \
        printf("%s:%s:%d:\t", __FILE__, __FUNCTION__, __LINE__);               \
        printf("ERROR! " x "\n", __VA_ARGS__);                                 \
        err++;                                                                 \
    } while (0)

#define TEST(name) printf("test — %s\n", name);
#define TEST_DESC(name, ...) printf("test — " name "\n", __VA_ARGS__);

#define QL_TEST_VERBOSE 0

#define UNUSED(x) (void)(x)
static void ql_info(quicklist *ql) {
#if QL_TEST_VERBOSE
    printf("Container length: %lu\n", ql->len);
    printf("Container size: %lu\n", ql->count);
    if (ql->head)
        printf("\t(zsize head: %d)\n", ziplistLen(ql->head->zl));
    if (ql->tail)
        printf("\t(zsize tail: %d)\n", ziplistLen(ql->tail->zl));
    printf("\n");
#else
    UNUSED(ql);
#endif
}

/* Iterate over an entire quicklist.
 * Print the list if 'print' == 1.
 *
 * Returns physical count of elements found by iterating over the list. */
static int _itrprintr(quicklist *ql, int print, int forward) {
    quicklistIter *iter =
        quicklistGetIterator(ql, forward ? AL_START_HEAD : AL_START_TAIL);
    quicklistEntry entry;
    int i = 0;
    int p = 0;
    quicklistNode *prev = NULL;
    while (quicklistNext(iter, &entry)) {
        if (entry.node != prev) {
            /* Count the number of list nodes too */
            p++;
            prev = entry.node;
        }
        if (print) {
            printf("[%3d (%2d)]: [%.*s] (%lld)\n", i, p, entry.sz,
                   (char *)entry.value, entry.longval);
        }
        i++;
    }
    quicklistReleaseIterator(iter);
    return i;
}
static int itrprintr(quicklist *ql, int print) {
    return _itrprintr(ql, print, 1);
}

static int itrprintr_rev(quicklist *ql, int print) {
    return _itrprintr(ql, print, 0);
}

/* Verify list metadata matches physical list contents. */
static void ql_verify(quicklist *ql, uint32_t len, uint32_t count,
                      uint32_t head_count, uint32_t tail_count) {
    int ok = 1;

    ql_info(ql);
    if (len != ql->len) {
        yell("quicklist length wrong: expected %d, got %lu", len, ql->len);
        ok = 0;
    }

    if (count != ql->count) {
        yell("quicklist count wrong: expected %d, got %lu", count, ql->count);
        ok = 0;
    }

    int loopr = itrprintr(ql, 0);
    if (loopr != (int)ql->count) {
        yell("quicklist cached count not match actual count: expected %lu, got "
             "%d",
             ql->count, loopr);
        ok = 0;
    }

    int rloopr = itrprintr_rev(ql, 0);
    if (loopr != rloopr) {
        yell("quicklist has different forward count than reverse count!  "
             "Forward count is %d, reverse count is %d.",
             loopr, rloopr);
        ok = 0;
    }

    if (ql->len == 0 && ok) {
        OK;
        return;
    }

    if (head_count != ql->head->count &&
        head_count != ziplistLen(ql->head->zl)) {
        yell("quicklist head count wrong: expected %d, "
             "got cached %d vs. actual %d",
             head_count, ql->head->count, ziplistLen(ql->head->zl));
        ok = 0;
    }

    if (tail_count != ql->tail->count &&
        tail_count != ziplistLen(ql->tail->zl)) {
        yell("quicklist tail count wrong: expected %d, "
             "got cached %u vs. actual %d",
             tail_count, ql->tail->count, ziplistLen(ql->tail->zl));
        ok = 0;
    }
    if (ok)
        OK;
}

/* Generate new string concatenating integer i against string 'prefix' */
static char *genstr(char *prefix, int i) {
    static char result[64] = { 0 };
    snprintf(result, sizeof(result), "%s%d", prefix, i);
    return result;
}

/* Test fill cap */
#define F 32
/* main test, but callable from other files */
int quicklistTest(int argc, char *argv[]) {
    int err = 0;

    UNUSED(argc);
    UNUSED(argv);

    TEST("create list") {
        quicklist *ql = quicklistCreate();
        ql_verify(ql, 0, 0, 0, 0);
        quicklistRelease(ql);
    }

    TEST("add to tail of empty list") {
        quicklist *ql = quicklistCreate();
        quicklistPushTail(ql, F, "hello", 6);
        /* 1 for head and 1 for tail beacuse 1 node = head = tail */
        ql_verify(ql, 1, 1, 1, 1);
        quicklistRelease(ql);
    }

    TEST("add to head of empty list") {
        quicklist *ql = quicklistCreate();
        quicklistPushHead(ql, F, "hello", 6);
        /* 1 for head and 1 for tail beacuse 1 node = head = tail */
        ql_verify(ql, 1, 1, 1, 1);
        quicklistRelease(ql);
    }

    for (size_t f = 0; f < 32; f++) {
        TEST_DESC("add to tail 5x at fill %lu", f) {
            quicklist *ql = quicklistCreate();
            for (int i = 0; i < 5; i++)
                quicklistPushTail(ql, f, genstr("hello", i), 32);
            if (ql->count != 5)
                ERROR;
            if (f == 32)
                ql_verify(ql, 1, 5, 5, 5);
            quicklistRelease(ql);
        }
    }

    for (size_t f = 0; f < 32; f++) {
        TEST_DESC("add to head 5x at fill %lu", f) {
            quicklist *ql = quicklistCreate();
            for (int i = 0; i < 5; i++)
                quicklistPushHead(ql, f, genstr("hello", i), 32);
            if (ql->count != 5)
                ERROR;
            if (f == 32)
                ql_verify(ql, 1, 5, 5, 5);
            quicklistRelease(ql);
        }
    }

    for (size_t f = 0; f < 512; f++) {
        TEST_DESC("add to tail 500x at fill %lu", f) {
            quicklist *ql = quicklistCreate();
            for (int i = 0; i < 500; i++)
                quicklistPushTail(ql, f, genstr("hello", i), 32);
            if (ql->count != 500)
                ERROR;
            if (f == 32)
                ql_verify(ql, 16, 500, 32, 20);
            quicklistRelease(ql);
        }
    }

    for (size_t f = 0; f < 512; f++) {
        TEST_DESC("add to head 500x at fill %lu", f) {
            quicklist *ql = quicklistCreate();
            for (int i = 0; i < 500; i++)
                quicklistPushHead(ql, f, genstr("hello", i), 32);
            if (ql->count != 500)
                ERROR;
            if (f == 32)
                ql_verify(ql, 16, 500, 20, 32);
            quicklistRelease(ql);
        }
    }

    TEST("rotate empty") {
        quicklist *ql = quicklistCreate();
        quicklistRotate(ql, F);
        ql_verify(ql, 0, 0, 0, 0);
        quicklistRelease(ql);
    }

    for (size_t f = 0; f < 32; f++) {
        TEST("rotate one val once") {
            quicklist *ql = quicklistCreate();
            quicklistPushHead(ql, F, "hello", 6);
            quicklistRotate(ql, F);
            ql_verify(ql, 1, 1, 1, 1);
            quicklistRelease(ql);
        }
    }

    for (size_t f = 0; f < 1024; f++) {
        TEST_DESC("rotate 500 val 5000 times at fill %lu", f) {
            quicklist *ql = quicklistCreate();
            for (int i = 0; i < 500; i++)
                quicklistPushHead(ql, f, genstr("hello", i), 32);
            ql_info(ql);
            for (int i = 0; i < 5000; i++) {
                ql_info(ql);
                quicklistRotate(ql, f);
            }
            if (f == 32)
                ql_verify(ql, 16, 500, 28, 24);
            quicklistRelease(ql);
        }
    }

    TEST("pop empty") {
        quicklist *ql = quicklistCreate();
        quicklistPop(ql, QUICKLIST_HEAD, NULL, NULL, NULL);
        ql_verify(ql, 0, 0, 0, 0);
        quicklistRelease(ql);
    }

    TEST("pop 1 string from 1") {
        quicklist *ql = quicklistCreate();
        quicklistPushHead(ql, F, genstr("hello", 331), 32);
        unsigned char *data;
        unsigned int sz;
        long long lv;
        ql_info(ql);
        quicklistPop(ql, QUICKLIST_HEAD, &data, &sz, &lv);
        assert(data != NULL);
        assert(sz == 32);
        ql_verify(ql, 0, 0, 0, 0);
        quicklistRelease(ql);
    }

    TEST("pop head 1 number from 1") {
        quicklist *ql = quicklistCreate();
        quicklistPushHead(ql, F, "55513", 5);
        unsigned char *data;
        unsigned int sz;
        long long lv;
        ql_info(ql);
        quicklistPop(ql, QUICKLIST_HEAD, &data, &sz, &lv);
        assert(data == NULL);
        assert(lv == 55513);
        ql_verify(ql, 0, 0, 0, 0);
        quicklistRelease(ql);
    }

    TEST("pop head 500 from 500") {
        quicklist *ql = quicklistCreate();
        for (int i = 0; i < 500; i++)
            quicklistPushHead(ql, F, genstr("hello", i), 32);
        ql_info(ql);
        for (int i = 0; i < 500; i++) {
            unsigned char *data;
            unsigned int sz;
            long long lv;
            int ret = quicklistPop(ql, QUICKLIST_HEAD, &data, &sz, &lv);
            assert(ret == 1);
            assert(data != NULL);
            assert(sz == 32);
        }
        ql_verify(ql, 0, 0, 0, 0);
        quicklistRelease(ql);
    }

    TEST("pop head 5000 from 500") {
        quicklist *ql = quicklistCreate();
        for (int i = 0; i < 500; i++)
            quicklistPushHead(ql, F, genstr("hello", i), 32);
        for (int i = 0; i < 5000; i++) {
            unsigned char *data;
            unsigned int sz;
            long long lv;
            int ret = quicklistPop(ql, QUICKLIST_HEAD, &data, &sz, &lv);
            if (i < 500) {
                assert(ret == 1);
                assert(data != NULL);
                assert(sz == 32);
            } else {
                assert(ret == 0);
            }
        }
        ql_verify(ql, 0, 0, 0, 0);
        quicklistRelease(ql);
    }

    TEST("iterate forward over 500 list") {
        quicklist *ql = quicklistCreate();
        for (int i = 0; i < 500; i++)
            quicklistPushHead(ql, F, genstr("hello", i), 32);
        quicklistIter *iter = quicklistGetIterator(ql, AL_START_HEAD);
        quicklistEntry entry;
        int i = 499, count = 0;
        while (quicklistNext(iter, &entry)) {
            char *h = genstr("hello", i);
            if (strcmp((char *)entry.value, h))
                ERR("value [%s] didn't match [%s] at position %d", entry.value,
                    h, i);
            i--;
            count++;
        }
        if (count != 500)
            ERR("Didn't iterate over exactly 500 elements (%d)", i);
        ql_verify(ql, 16, 500, 20, 32);
        quicklistReleaseIterator(iter);
        quicklistRelease(ql);
    }

    TEST("iterate reverse over 500 list") {
        quicklist *ql = quicklistCreate();
        for (int i = 0; i < 500; i++)
            quicklistPushHead(ql, F, genstr("hello", i), 32);
        quicklistIter *iter = quicklistGetIterator(ql, AL_START_TAIL);
        quicklistEntry entry;
        int i = 0;
        while (quicklistNext(iter, &entry)) {
            char *h = genstr("hello", i);
            if (strcmp((char *)entry.value, h))
                ERR("value [%s] didn't match [%s] at position %d", entry.value,
                    h, i);
            i++;
        }
        if (i != 500)
            ERR("Didn't iterate over exactly 500 elements (%d)", i);
        ql_verify(ql, 16, 500, 20, 32);
        quicklistReleaseIterator(iter);
        quicklistRelease(ql);
    }

    TEST("insert before with 0 elements") {
        quicklist *ql = quicklistCreate();
        quicklistEntry entry;
        quicklistIndex(ql, 0, &entry);
        quicklistInsertBefore(ql, F, &entry, "abc", 4);
        ql_verify(ql, 1, 1, 1, 1);
        quicklistRelease(ql);
    }

    TEST("insert after with 0 elements") {
        quicklist *ql = quicklistCreate();
        quicklistEntry entry;
        quicklistIndex(ql, 0, &entry);
        quicklistInsertAfter(ql, F, &entry, "abc", 4);
        ql_verify(ql, 1, 1, 1, 1);
        quicklistRelease(ql);
    }

    TEST("insert after 1 element") {
        quicklist *ql = quicklistCreate();
        quicklistPushHead(ql, F, "hello", 6);
        quicklistEntry entry;
        quicklistIndex(ql, 0, &entry);
        quicklistInsertAfter(ql, F, &entry, "abc", 4);
        ql_verify(ql, 1, 2, 2, 2);
        quicklistRelease(ql);
    }

    TEST("insert before 1 element") {
        quicklist *ql = quicklistCreate();
        quicklistPushHead(ql, F, "hello", 6);
        quicklistEntry entry;
        quicklistIndex(ql, 0, &entry);
        quicklistInsertAfter(ql, F, &entry, "abc", 4);
        ql_verify(ql, 1, 2, 2, 2);
        quicklistRelease(ql);
    }

    for (size_t f = 0; f < 12; f++) {
        TEST_DESC("insert once in elements while iterating at fill %lu\n", f) {
            quicklist *ql = quicklistCreate();
            quicklistPushTail(ql, f, "abc", 3);
            quicklistPushTail(ql, 1, "def", 3); /* force to unique node */
            quicklistPushTail(ql, f, "bob", 3); /* force to reset for +3 */
            quicklistPushTail(ql, f, "foo", 3);
            quicklistPushTail(ql, f, "zoo", 3);

            itrprintr(ql, 1);
            /* insert "bar" before "bob" while iterating over list. */
            quicklistIter *iter = quicklistGetIterator(ql, AL_START_HEAD);
            quicklistEntry entry;
            while (quicklistNext(iter, &entry)) {
                if (!strncmp((char *)entry.value, "bob", 3)) {
                    /* Insert as fill = 1 so it spills into new node. */
                    quicklistInsertBefore(ql, f, &entry, "bar", 3);
                    /* NOTE! You can't continue iterating after an insert into
                     * the list.  You *must* re-create your iterator again if
                     * you want to traverse all entires. */
                    break;
                }
            }
            itrprintr(ql, 1);

            /* verify results */
            quicklistIndex(ql, 0, &entry);
            if (strncmp((char *)entry.value, "abc", 3))
                ERR("Value 0 didn't match, instead got: %.*s", entry.sz,
                    entry.value);
            quicklistIndex(ql, 1, &entry);
            if (strncmp((char *)entry.value, "def", 3))
                ERR("Value 1 didn't match, instead got: %.*s", entry.sz,
                    entry.value);
            quicklistIndex(ql, 2, &entry);
            if (strncmp((char *)entry.value, "bar", 3))
                ERR("Value 2 didn't match, instead got: %.*s", entry.sz,
                    entry.value);
            quicklistIndex(ql, 3, &entry);
            if (strncmp((char *)entry.value, "bob", 3))
                ERR("Value 3 didn't match, instead got: %.*s", entry.sz,
                    entry.value);
            quicklistIndex(ql, 4, &entry);
            if (strncmp((char *)entry.value, "foo", 3))
                ERR("Value 4 didn't match, instead got: %.*s", entry.sz,
                    entry.value);
            quicklistIndex(ql, 5, &entry);
            if (strncmp((char *)entry.value, "zoo", 3))
                ERR("Value 5 didn't match, instead got: %.*s", entry.sz,
                    entry.value);
            quicklistReleaseIterator(iter);
            quicklistRelease(ql);
        }
    }

    for (size_t f = 0; f < 1024; f++) {
        TEST_DESC("insert [before] 250 new in middle of 500 elements at fill"
                  " %ld",
                  f) {
            quicklist *ql = quicklistCreate();
            for (int i = 0; i < 500; i++)
                quicklistPushTail(ql, f, genstr("hello", i), 32);
            for (int i = 0; i < 250; i++) {
                quicklistEntry entry;
                quicklistIndex(ql, 250, &entry);
                quicklistInsertBefore(ql, f, &entry, genstr("abc", i), 32);
            }
            if (f == 32)
                ql_verify(ql, 26, 750, 32, 20);
            quicklistRelease(ql);
        }
    }

    for (size_t f = 0; f < 1024; f++) {
        TEST_DESC(
            "insert [after] 250 new in middle of 500 elements at fill %ld", f) {
            quicklist *ql = quicklistCreate();
            for (int i = 0; i < 500; i++)
                quicklistPushHead(ql, f, genstr("hello", i), 32);
            for (int i = 0; i < 250; i++) {
                quicklistEntry entry;
                quicklistIndex(ql, 250, &entry);
                quicklistInsertAfter(ql, f, &entry, genstr("abc", i), 32);
            }

            if (ql->count != 750)
                ERR("List size not 750, but rather %ld", ql->count);

            if (f == 32)
                ql_verify(ql, 26, 750, 20, 32);
            quicklistRelease(ql);
        }
    }

    TEST("duplicate empty list") {
        quicklist *ql = quicklistCreate();
        ql_verify(ql, 0, 0, 0, 0);
        quicklist *copy = quicklistDup(ql);
        ql_verify(copy, 0, 0, 0, 0);
        quicklistRelease(ql);
        quicklistRelease(copy);
    }

    TEST("duplicate list of 1 element") {
        quicklist *ql = quicklistCreate();
        quicklistPushHead(ql, F, genstr("hello", 3), 32);
        ql_verify(ql, 1, 1, 1, 1);
        quicklist *copy = quicklistDup(ql);
        ql_verify(copy, 1, 1, 1, 1);
        quicklistRelease(ql);
        quicklistRelease(copy);
    }

    TEST("duplicate list of 500") {
        quicklist *ql = quicklistCreate();
        for (int i = 0; i < 500; i++)
            quicklistPushHead(ql, F, genstr("hello", i), 32);
        ql_verify(ql, 16, 500, 20, 32);

        quicklist *copy = quicklistDup(ql);
        ql_verify(copy, 16, 500, 20, 32);
        quicklistRelease(ql);
        quicklistRelease(copy);
    }

    for (size_t f = 0; f < 512; f++) {
        TEST_DESC("index 1,200 from 500 list at fill %lu", f) {
            quicklist *ql = quicklistCreate();
            for (int i = 0; i < 500; i++)
                quicklistPushTail(ql, f, genstr("hello", i + 1), 32);
            quicklistEntry entry;
            quicklistIndex(ql, 1, &entry);
            if (!strcmp((char *)entry.value, "hello2"))
                OK;
            else
                ERR("Value: %s", entry.value);
            quicklistIndex(ql, 200, &entry);
            if (!strcmp((char *)entry.value, "hello201"))
                OK;
            else
                ERR("Value: %s", entry.value);
            quicklistRelease(ql);
        }

        TEST_DESC("index -1,-2 from 500 list at fill %lu", f) {
            quicklist *ql = quicklistCreate();
            for (int i = 0; i < 500; i++)
                quicklistPushTail(ql, f, genstr("hello", i + 1), 32);
            quicklistEntry entry;
            quicklistIndex(ql, -1, &entry);
            if (!strcmp((char *)entry.value, "hello500"))
                OK;
            else
                ERR("Value: %s", entry.value);
            quicklistIndex(ql, -2, &entry);
            if (!strcmp((char *)entry.value, "hello499"))
                OK;
            else
                ERR("Value: %s", entry.value);
            quicklistRelease(ql);
        }

        TEST_DESC("index -100 from 500 list at fill %lu", f) {
            quicklist *ql = quicklistCreate();
            for (int i = 0; i < 500; i++)
                quicklistPushTail(ql, f, genstr("hello", i + 1), 32);
            quicklistEntry entry;
            quicklistIndex(ql, -100, &entry);
            if (!strcmp((char *)entry.value, "hello401"))
                OK;
            else
                ERR("Value: %s", entry.value);
            quicklistRelease(ql);
        }

        TEST_DESC("index too big +1 from 50 list at fill %lu", f) {
            quicklist *ql = quicklistCreate();
            for (int i = 0; i < 50; i++)
                quicklistPushTail(ql, f, genstr("hello", i + 1), 32);
            quicklistEntry entry;
            if (quicklistIndex(ql, 50, &entry))
                ERR("Index found at 50 with 50 list: %.*s", entry.sz,
                    entry.value);
            else
                OK;
            quicklistRelease(ql);
        }
    }

    TEST("delete range empty list") {
        quicklist *ql = quicklistCreate();
        quicklistDelRange(ql, 5, 20);
        ql_verify(ql, 0, 0, 0, 0);
        quicklistRelease(ql);
    }

    TEST("delete range of entire node in list of one node") {
        quicklist *ql = quicklistCreate();
        for (int i = 0; i < 32; i++)
            quicklistPushHead(ql, F, genstr("hello", i), 32);
        quicklistDelRange(ql, 0, 32);
        ql_verify(ql, 0, 0, 0, 0);
        quicklistRelease(ql);
    }

    TEST("delete middle 100 of 500 list") {
        quicklist *ql = quicklistCreate();
        for (int i = 0; i < 500; i++)
            quicklistPushTail(ql, F, genstr("hello", i + 1), 32);
        quicklistDelRange(ql, 200, 100);
        ql_verify(ql, 14, 400, 32, 20);
        quicklistRelease(ql);
    }

    TEST("delete negative 1 from 500 list") {
        quicklist *ql = quicklistCreate();
        for (int i = 0; i < 500; i++)
            quicklistPushTail(ql, F, genstr("hello", i + 1), 32);
        quicklistDelRange(ql, -1, 1);
        ql_verify(ql, 16, 499, 32, 19);
        quicklistRelease(ql);
    }

    TEST("delete negative 100 from 500 list") {
        quicklist *ql = quicklistCreate();
        for (int i = 0; i < 500; i++)
            quicklistPushTail(ql, F, genstr("hello", i + 1), 32);
        quicklistDelRange(ql, -100, 100);
        ql_verify(ql, 13, 400, 32, 16);
        quicklistRelease(ql);
    }

    TEST("delete -10 count 5 from 50 list") {
        quicklist *ql = quicklistCreate();
        for (int i = 0; i < 50; i++)
            quicklistPushTail(ql, F, genstr("hello", i + 1), 32);
        quicklistDelRange(ql, -10, 5);
        ql_verify(ql, 2, 45, 32, 13);
        quicklistRelease(ql);
    }

    TEST("numbers only list read") {
        quicklist *ql = quicklistCreate();
        quicklistPushTail(ql, F, "1111", 4);
        quicklistPushTail(ql, F, "2222", 4);
        quicklistPushTail(ql, F, "3333", 4);
        quicklistPushTail(ql, F, "4444", 4);
        ql_verify(ql, 1, 4, 4, 4);
        quicklistEntry entry;
        quicklistIndex(ql, 0, &entry);
        if (entry.longval != 1111)
            ERR("Not 1111, %lld", entry.longval);
        quicklistIndex(ql, 1, &entry);
        if (entry.longval != 2222)
            ERR("Not 2222, %lld", entry.longval);
        quicklistIndex(ql, 2, &entry);
        if (entry.longval != 3333)
            ERR("Not 3333, %lld", entry.longval);
        quicklistIndex(ql, 3, &entry);
        if (entry.longval != 4444)
            ERR("Not 4444, %lld", entry.longval);
        if (quicklistIndex(ql, 4, &entry))
            ERR("Index past elements: %lld", entry.longval);
        quicklistIndex(ql, -1, &entry);
        if (entry.longval != 4444)
            ERR("Not 4444 (reverse), %lld", entry.longval);
        quicklistIndex(ql, -2, &entry);
        if (entry.longval != 3333)
            ERR("Not 3333 (reverse), %lld", entry.longval);
        quicklistIndex(ql, -3, &entry);
        if (entry.longval != 2222)
            ERR("Not 2222 (reverse), %lld", entry.longval);
        quicklistIndex(ql, -4, &entry);
        if (entry.longval != 1111)
            ERR("Not 1111 (reverse), %lld", entry.longval);
        if (quicklistIndex(ql, -5, &entry))
            ERR("Index past elements (reverse), %lld", entry.longval);
        quicklistRelease(ql);
    }

    TEST("numbers larger list read") {
        quicklist *ql = quicklistCreate();
        char num[32];
        long long nums[5000];
        for (int i = 0; i < 5000; i++) {
            nums[i] = -5157318210846258176 + i;
            int sz = snprintf(num, sizeof(num), "%lld", nums[i]);
            quicklistPushTail(ql, F, num, sz);
        }
        quicklistPushTail(ql, F, "xxxxxxxxxxxxxxxxxxxx", 20);
        quicklistEntry entry;
        for (int i = 0; i < 5000; i++) {
            quicklistIndex(ql, i, &entry);
            if (entry.longval != nums[i])
                ERR("[%d] Not longval %lld but rather %lld", i, nums[i],
                    entry.longval);
            entry.longval = 0xdeadbeef;
        }
        quicklistIndex(ql, 5000, &entry);
        if (strncmp((char *)entry.value, "xxxxxxxxxxxxxxxxxxxx", 20))
            ERR("String val not match: %s", entry.value);
        ql_verify(ql, 157, 5001, 32, 9);
        quicklistRelease(ql);
    }

    TEST("numbers larger list read B") {
        quicklist *ql = quicklistCreate();
        quicklistPushTail(ql, F, "99", 2);
        quicklistPushTail(ql, F, "98", 2);
        quicklistPushTail(ql, F, "xxxxxxxxxxxxxxxxxxxx", 20);
        quicklistPushTail(ql, F, "96", 2);
        quicklistPushTail(ql, F, "95", 2);
        quicklistReplaceAtIndex(ql, 1, "foo", 3);
        quicklistReplaceAtIndex(ql, -1, "bar", 3);
        quicklistRelease(ql);
        OK;
    }

    for (size_t f = 0; f < 16; f++) {
        TEST_DESC("lrem test at fill %lu", f) {
            quicklist *ql = quicklistCreate();
            char *words[] = { "abc", "foo", "bar",  "foobar", "foobared",
                              "zap", "bar", "test", "foo" };
            char *result[] = { "abc", "foo",  "foobar", "foobared",
                               "zap", "test", "foo" };
            char *resultB[] = { "abc",      "foo", "foobar",
                                "foobared", "zap", "test" };
            for (int i = 0; i < 9; i++)
                quicklistPushTail(ql, f, words[i], strlen(words[i]));

            /* lrem 0 bar */
            quicklistIter *iter = quicklistGetIterator(ql, AL_START_HEAD);
            quicklistEntry entry;
            int i = 0;
            while (quicklistNext(iter, &entry)) {
                if (quicklistCompare(entry.zi, (unsigned char *)"bar", 3)) {
                    quicklistDelEntry(iter, &entry);
                }
                i++;
            }
            quicklistReleaseIterator(iter);

            /* check result of lrem 0 bar */
            iter = quicklistGetIterator(ql, AL_START_HEAD);
            i = 0;
            int ok = 1;
            while (quicklistNext(iter, &entry)) {
                /* Result must be: abc, foo, foobar, foobared, zap, test, foo */
                if (strncmp((char *)entry.value, result[i], entry.sz)) {
                    ERR("No match at position %d, got %.*s instead of %s", i,
                        entry.sz, entry.value, result[i]);
                    ok = 0;
                }
                i++;
            }
            quicklistReleaseIterator(iter);

            quicklistPushTail(ql, f, "foo", 3);

            /* lrem -2 foo */
            iter = quicklistGetIterator(ql, AL_START_TAIL);
            i = 0;
            int del = 2;
            while (quicklistNext(iter, &entry)) {
                if (quicklistCompare(entry.zi, (unsigned char *)"foo", 3)) {
                    quicklistDelEntry(iter, &entry);
                    del--;
                }
                if (!del)
                    break;
                i++;
            }
            quicklistReleaseIterator(iter);

            /* check result of lrem -2 foo */
            /* (we're ignoring the '2' part and still deleting all foo because
             * we only have two foo) */
            iter = quicklistGetIterator(ql, AL_START_TAIL);
            i = 0;
            size_t resB = sizeof(resultB) / sizeof(*resultB);
            while (quicklistNext(iter, &entry)) {
                /* Result must be: abc, foo, foobar, foobared, zap, test, foo */
                if (strncmp((char *)entry.value, resultB[resB - 1 - i],
                            entry.sz)) {
                    ERR("No match at position %d, got %.*s instead of %s", i,
                        entry.sz, entry.value, resultB[resB - 1 - i]);
                    ok = 0;
                }
                i++;
            }

            quicklistReleaseIterator(iter);
            /* final result of all tests */
            if (ok)
                OK;
            quicklistRelease(ql);
        }
    }

    for (size_t f = 0; f < 16; f++) {
        TEST_DESC("iterate reverse + delete at fill %lu", f) {
            quicklist *ql = quicklistCreate();
            quicklistPushTail(ql, f, "abc", 3);
            quicklistPushTail(ql, f, "def", 3);
            quicklistPushTail(ql, f, "hij", 3);
            quicklistPushTail(ql, f, "jkl", 3);
            quicklistPushTail(ql, f, "oop", 3);

            quicklistEntry entry;
            quicklistIter *iter = quicklistGetIterator(ql, AL_START_TAIL);
            int i = 0;
            while (quicklistNext(iter, &entry)) {
                if (quicklistCompare(entry.zi, (unsigned char *)"hij", 3)) {
                    quicklistDelEntry(iter, &entry);
                }
                i++;
            }
            quicklistReleaseIterator(iter);

            if (i != 5)
                ERR("Didn't iterate 5 times, iterated %d times.", i);

            /* Check results after deletion of "hij" */
            iter = quicklistGetIterator(ql, AL_START_HEAD);
            i = 0;
            char *vals[] = { "abc", "def", "jkl", "oop" };
            while (quicklistNext(iter, &entry)) {
                if (!quicklistCompare(entry.zi, (unsigned char *)vals[i], 3)) {
                    ERR("Value at %d didn't match %s\n", i, vals[i]);
                }
                i++;
            }
            quicklistReleaseIterator(iter);
            quicklistRelease(ql);
        }
    }

    for (size_t f = 0; f < 800; f++) {
        TEST_DESC("iterator at index test at fill %lu", f) {
            quicklist *ql = quicklistCreate();
            char num[32];
            long long nums[5000];
            for (int i = 0; i < 760; i++) {
                nums[i] = -5157318210846258176 + i;
                int sz = snprintf(num, sizeof(num), "%lld", nums[i]);
                quicklistPushTail(ql, f, num, sz);
            }

            quicklistEntry entry;
            quicklistIter *iter =
                quicklistGetIteratorAtIdx(ql, AL_START_HEAD, 437);
            int i = 437;
            while (quicklistNext(iter, &entry)) {
                if (entry.longval != nums[i])
                    ERR("Expected %lld, but got %lld", entry.longval, nums[i]);
                i++;
            }
            quicklistReleaseIterator(iter);
            quicklistRelease(ql);
        }
    }

    for (size_t f = 0; f < 40; f++) {
        TEST_DESC("ltrim test A at fill %lu", f) {
            quicklist *ql = quicklistCreate();
            char num[32];
            long long nums[5000];
            for (int i = 0; i < 32; i++) {
                nums[i] = -5157318210846258176 + i;
                int sz = snprintf(num, sizeof(num), "%lld", nums[i]);
                quicklistPushTail(ql, f, num, sz);
            }
            if (f == 32)
                ql_verify(ql, 1, 32, 32, 32);
            /* ltrim 25 53 (keep [25,32] inclusive = 7 remaining) */
            quicklistDelRange(ql, 0, 25);
            quicklistDelRange(ql, 0, 0);
            quicklistEntry entry;
            for (int i = 0; i < 7; i++) {
                quicklistIndex(ql, i, &entry);
                if (entry.longval != nums[25 + i])
                    ERR("Deleted invalid range!  Expected %lld but got %lld",
                        entry.longval, nums[25 + i]);
            }
            if (f == 32)
                ql_verify(ql, 1, 7, 7, 7);
            quicklistRelease(ql);
        }
    }

    for (size_t f = 0; f < 40; f++) {
        TEST_DESC("ltrim test B at fill %lu", f) {
            quicklist *ql = quicklistCreate();
            char num[32];
            long long nums[5000];
            for (int i = 0; i < 33; i++) {
                nums[i] = i;
                int sz = snprintf(num, sizeof(num), "%lld", nums[i]);
                quicklistPushTail(ql, f, num, sz);
            }
            if (f == 32)
                ql_verify(ql, 2, 33, 32, 1);
            /* ltrim 5 16 (keep [5,16] inclusive = 12 remaining) */
            quicklistDelRange(ql, 0, 5);
            quicklistDelRange(ql, -16, 16);
            if (f == 32)
                ql_verify(ql, 1, 12, 12, 12);
            quicklistEntry entry;
            quicklistIndex(ql, 0, &entry);
            if (entry.longval != 5)
                ERR("A: longval not 5, but %lld", entry.longval);
            else
                OK;
            quicklistIndex(ql, -1, &entry);
            if (entry.longval != 16)
                ERR("B! got instead: %lld", entry.longval);
            else
                OK;
            quicklistPushTail(ql, f, "bobobob", 7);
            quicklistIndex(ql, -1, &entry);
            if (strncmp((char *)entry.value, "bobobob", 7))
                ERR("Tail doesn't match bobobob, it's %.*s instead", entry.sz,
                    entry.value);
            for (int i = 0; i < 12; i++) {
                quicklistIndex(ql, i, &entry);
                if (entry.longval != nums[5 + i])
                    ERR("Deleted invalid range!  Expected %lld but got %lld",
                        entry.longval, nums[5 + i]);
            }
            if (f == 32)
                quicklistRelease(ql);
        }
    }

    for (size_t f = 0; f < 40; f++) {
        TEST_DESC("ltrim test C at fill %lu", f) {
            quicklist *ql = quicklistCreate();
            char num[32];
            long long nums[5000];
            for (int i = 0; i < 33; i++) {
                nums[i] = -5157318210846258176 + i;
                int sz = snprintf(num, sizeof(num), "%lld", nums[i]);
                quicklistPushTail(ql, f, num, sz);
            }
            if (f == 32)
                ql_verify(ql, 2, 33, 32, 1);
            /* ltrim 3 3 (keep [3,3] inclusive = 1 remaining) */
            quicklistDelRange(ql, 0, 3);
            quicklistDelRange(ql, -29, 4000); /* make sure not loop forever */
            if (f == 32)
                ql_verify(ql, 1, 1, 1, 1);
            quicklistEntry entry;
            quicklistIndex(ql, 0, &entry);
            if (entry.longval != -5157318210846258173)
                ERROR;
            else
                OK;
        }
    }

    for (size_t f = 0; f < 40; f++) {
        TEST_DESC("ltrim test D at fill %lu", f) {
            quicklist *ql = quicklistCreate();
            char num[32];
            long long nums[5000];
            for (int i = 0; i < 33; i++) {
                nums[i] = -5157318210846258176 + i;
                int sz = snprintf(num, sizeof(num), "%lld", nums[i]);
                quicklistPushTail(ql, f, num, sz);
            }
            if (f == 32)
                ql_verify(ql, 2, 33, 32, 1);
            quicklistDelRange(ql, -12, 3);
            if (ql->count != 30)
                ERR("Didn't delete exactly three elements!  Count is: %lu",
                    ql->count);
        }
    }

    if (!err)
        printf("ALL TESTS PASSED!\n");
    else
        ERR("Sorry, not all tests passed!  In fact, %d tests failed.", err);

    return err;
}
#endif
