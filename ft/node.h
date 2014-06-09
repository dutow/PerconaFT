/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil -*- */
// vim: ft=cpp:expandtab:ts=8:sw=4:softtabstop=4:

/*
COPYING CONDITIONS NOTICE:

  This program is free software; you can redistribute it and/or modify
  it under the terms of version 2 of the GNU General Public License as
  published by the Free Software Foundation, and provided that the
  following conditions are met:

      * Redistributions of source code must retain this COPYING
        CONDITIONS NOTICE, the COPYRIGHT NOTICE (below), the
        DISCLAIMER (below), the UNIVERSITY PATENT NOTICE (below), the
        PATENT MARKING NOTICE (below), and the PATENT RIGHTS
        GRANT (below).

      * Redistributions in binary form must reproduce this COPYING
        CONDITIONS NOTICE, the COPYRIGHT NOTICE (below), the
        DISCLAIMER (below), the UNIVERSITY PATENT NOTICE (below), the
        PATENT MARKING NOTICE (below), and the PATENT RIGHTS
        GRANT (below) in the documentation and/or other materials
        provided with the distribution.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
  02110-1301, USA.

COPYRIGHT NOTICE:

  TokuDB, Tokutek Fractal Tree Indexing Library.
  Copyright (C) 2007-2013 Tokutek, Inc.

DISCLAIMER:

  This program is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  General Public License for more details.

UNIVERSITY PATENT NOTICE:

  The technology is licensed by the Massachusetts Institute of
  Technology, Rutgers State University of New Jersey, and the Research
  Foundation of State University of New York at Stony Brook under
  United States of America Serial No. 11/760379 and to the patents
  and/or patent applications resulting from it.

PATENT MARKING NOTICE:

  This software is covered by US Patent No. 8,185,551.
  This software is covered by US Patent No. 8,489,638.

PATENT RIGHTS GRANT:

  "THIS IMPLEMENTATION" means the copyrightable works distributed by
  Tokutek as part of the Fractal Tree project.

  "PATENT CLAIMS" means the claims of patents that are owned or
  licensable by Tokutek, both currently or in the future; and that in
  the absence of this license would be infringed by THIS
  IMPLEMENTATION or by using or running THIS IMPLEMENTATION.

  "PATENT CHALLENGE" shall mean a challenge to the validity,
  patentability, enforceability and/or non-infringement of any of the
  PATENT CLAIMS or otherwise opposing any of the PATENT CLAIMS.

  Tokutek hereby grants to you, for the term and geographical scope of
  the PATENT CLAIMS, a non-exclusive, no-charge, royalty-free,
  irrevocable (except as stated in this section) patent license to
  make, have made, use, offer to sell, sell, import, transfer, and
  otherwise run, modify, and propagate the contents of THIS
  IMPLEMENTATION, where such license applies only to the PATENT
  CLAIMS.  This grant does not include claims that would be infringed
  only as a consequence of further modifications of THIS
  IMPLEMENTATION.  If you or your agent or licensee institute or order
  or agree to the institution of patent litigation against any entity
  (including a cross-claim or counterclaim in a lawsuit) alleging that
  THIS IMPLEMENTATION constitutes direct or contributory patent
  infringement, or inducement of patent infringement, then any rights
  granted to you under this License shall terminate as of the date
  such litigation is filed.  If you or your agent or exclusive
  licensee institute or order or agree to the institution of a PATENT
  CHALLENGE, then Tokutek may terminate any rights granted to you
  under this License.
*/

#pragma once

#include "ft/cachetable.h"
#include "ft/bndata.h"
#include "ft/fttypes.h"
#include "ft/msg_buffer.h"

struct ftnode {
    MSN      max_msn_applied_to_node_on_disk; // max_msn_applied that will be written to disk
    unsigned int flags;
    BLOCKNUM thisnodename;   // Which block number is this node?
    int    layout_version; // What version of the data structure?
    int    layout_version_original;	// different (<) from layout_version if upgraded from a previous version (useful for debugging)
    int    layout_version_read_from_disk;  // transient, not serialized to disk, (useful for debugging)
    uint32_t build_id;       // build_id (svn rev number) of software that wrote this node to disk
    int    height; /* height is always >= 0.  0 for leaf, >0 for nonleaf. */
    int    dirty;
    uint32_t fullhash;
    int n_children; //for internal nodes, if n_children==fanout+1 then the tree needs to be rebalanced.
                    // for leaf nodes, represents number of basement nodes
    unsigned int    totalchildkeylens;
    DBT *childkeys;   /* Pivot keys.  Child 0's keys are <= childkeys[0].  Child 1's keys are <= childkeys[1].
                                                                        Child 1's keys are > childkeys[0]. */

    // What's the oldest referenced xid that this node knows about? The real oldest
    // referenced xid might be younger, but this is our best estimate. We use it
    // as a heuristic to transition provisional mvcc entries from provisional to
    // committed (from implicity committed to really committed).
    //
    // A better heuristic would be the oldest live txnid, but we use this since it
    // still works well most of the time, and its readily available on the inject
    // code path.
    TXNID oldest_referenced_xid_known;

    // array of size n_children, consisting of ftnode partitions
    // each one is associated with a child
    // for internal nodes, the ith partition corresponds to the ith message buffer
    // for leaf nodes, the ith partition corresponds to the ith basement node
    struct ftnode_partition *bp;
    struct ctpair *ct_pair;
};

// data of an available partition of a leaf ftnode
struct ftnode_leaf_basement_node {
    bn_data data_buffer;
    unsigned int seqinsert;         // number of sequential inserts to this leaf 
    MSN max_msn_applied;            // max message sequence number applied
    bool stale_ancestor_messages_applied;
    STAT64INFO_S stat64_delta;      // change in stat64 counters since basement was last written to disk
};

enum pt_state {  // declare this to be packed so that when used below it will only take 1 byte.
    PT_INVALID = 0,
    PT_ON_DISK = 1,
    PT_COMPRESSED = 2,
    PT_AVAIL = 3};

enum ftnode_child_tag {
    BCT_INVALID = 0,
    BCT_NULL,
    BCT_SUBBLOCK,
    BCT_LEAF,
    BCT_NONLEAF
};

typedef toku::omt<int32_t> off_omt_t;
typedef toku::omt<int32_t, int32_t, true> marked_off_omt_t;

// data of an available partition of a nonleaf ftnode
struct ftnode_nonleaf_childinfo {
    message_buffer msg_buffer;
    off_omt_t broadcast_list;
    marked_off_omt_t fresh_message_tree;
    off_omt_t stale_message_tree;
    uint64_t flow[2];  // current and last checkpoint
};
    
typedef struct ftnode_child_pointer {
    union {
	struct sub_block *subblock;
	struct ftnode_nonleaf_childinfo *nonleaf;
	struct ftnode_leaf_basement_node *leaf;
    } u;
    enum ftnode_child_tag tag;
} FTNODE_CHILD_POINTER;

struct ftnode_disk_data {
    //
    // stores the offset to the beginning of the partition on disk from the ftnode, and the length, needed to read a partition off of disk
    // the value is only meaningful if the node is clean. If the node is dirty, then the value is meaningless
    //  The START is the distance from the end of the compressed node_info data, to the beginning of the compressed partition
    //  The SIZE is the size of the compressed partition.
    // Rationale:  We cannot store the size from the beginning of the node since we don't know how big the header will be.
    //  However, later when we are doing aligned writes, we won't be able to store the size from the end since we want things to align.
    uint32_t start;
    uint32_t size;
};
#define BP_START(node_dd,i) ((node_dd)[i].start)
#define BP_SIZE(node_dd,i) ((node_dd)[i].size)

// a ftnode partition, associated with a child of a node
struct ftnode_partition {
    // the following three variables are used for nonleaf nodes
    // for leaf nodes, they are meaningless
    BLOCKNUM     blocknum; // blocknum of child 

    // How many bytes worth of work was performed by messages in each buffer.
    uint64_t     workdone;

    //
    // pointer to the partition. Depending on the state, they may be different things
    // if state == PT_INVALID, then the node was just initialized and ptr == NULL
    // if state == PT_ON_DISK, then ptr == NULL
    // if state == PT_COMPRESSED, then ptr points to a struct sub_block*
    // if state == PT_AVAIL, then ptr is:
    //         a struct ftnode_nonleaf_childinfo for internal nodes, 
    //         a struct ftnode_leaf_basement_node for leaf nodes
    //
    struct ftnode_child_pointer ptr;
    //
    // at any time, the partitions may be in one of the following three states (stored in pt_state):
    //   PT_INVALID - means that the partition was just initialized
    //   PT_ON_DISK - means that the partition is not in memory and needs to be read from disk. To use, must read off disk and decompress
    //   PT_COMPRESSED - means that the partition is compressed in memory. To use, must decompress
    //   PT_AVAIL - means the partition is decompressed and in memory
    //
    enum pt_state state; // make this an enum to make debugging easier.  

    // clock count used to for pe_callback to determine if a node should be evicted or not
    // for now, saturating the count at 1
    uint8_t clock_count;
};

//
// TODO: Fix all these names
//       Organize declarations
//       Fix widespread parameter ordering inconsistencies
//
BASEMENTNODE toku_create_empty_bn(void);
BASEMENTNODE toku_create_empty_bn_no_buffer(void); // create a basement node with a null buffer.
NONLEAF_CHILDINFO toku_clone_nl(NONLEAF_CHILDINFO orig_childinfo);
BASEMENTNODE toku_clone_bn(BASEMENTNODE orig_bn);
NONLEAF_CHILDINFO toku_create_empty_nl(void);
void destroy_basement_node (BASEMENTNODE bn);
void destroy_nonleaf_childinfo (NONLEAF_CHILDINFO nl);
void toku_destroy_ftnode_internals(FTNODE node);
void toku_ftnode_free (FTNODE *node);
bool toku_ftnode_fully_in_memory(FTNODE node);
void toku_ftnode_assert_fully_in_memory(FTNODE node);
void toku_evict_bn_from_memory(FTNODE node, int childnum, FT h);
BASEMENTNODE toku_detach_bn(FTNODE node, int childnum);
void toku_ftnode_update_disk_stats(FTNODE ftnode, FT ft, bool for_checkpoint);
void toku_ftnode_clone_partitions(FTNODE node, FTNODE cloned_node);

void toku_initialize_empty_ftnode(FTNODE node, BLOCKNUM nodename, int height, int num_children, 
                                  int layout_version, unsigned int flags);

int toku_ftnode_which_child(FTNODE node, const DBT *k,
                            DESCRIPTOR desc, ft_compare_func cmp);

//
// Field in ftnode_fetch_extra that tells the 
// partial fetch callback what piece of the node
// is needed by the ydb
//
enum ftnode_fetch_type {
    ftnode_fetch_none=1, // no partitions needed.  
    ftnode_fetch_subset, // some subset of partitions needed
    ftnode_fetch_prefetch, // this is part of a prefetch call
    ftnode_fetch_all, // every partition is needed
    ftnode_fetch_keymatch, // one child is needed if it holds both keys
};

static bool is_valid_ftnode_fetch_type(enum ftnode_fetch_type type) UU();
static bool is_valid_ftnode_fetch_type(enum ftnode_fetch_type type) {
    switch (type) {
        case ftnode_fetch_none:
        case ftnode_fetch_subset:
        case ftnode_fetch_prefetch:
        case ftnode_fetch_all:
        case ftnode_fetch_keymatch:
            return true;
        default:
            return false;
    }
}

//
// An extra parameter passed to cachetable functions 
// That is used in all types of fetch callbacks.
// The contents help the partial fetch and fetch
// callbacks retrieve the pieces of a node necessary
// for the ensuing operation (flush, query, ...)
//
struct ft_search;
struct ftnode_fetch_extra {
    enum ftnode_fetch_type type;
    // needed for reading a node off disk
    FT h;
    // used in the case where type == ftnode_fetch_subset
    // parameters needed to find out which child needs to be decompressed (so it can be read)
    ft_search *search;
    DBT range_lock_left_key, range_lock_right_key;
    bool left_is_neg_infty, right_is_pos_infty;
    // states if we should try to aggressively fetch basement nodes 
    // that are not specifically needed for current query, 
    // but may be needed for other cursor operations user is doing
    // For example, if we have not disabled prefetching,
    // and the user is doing a dictionary wide scan, then
    // even though a query may only want one basement node,
    // we fetch all basement nodes in a leaf node.
    bool disable_prefetching;
    // this value will be set during the fetch_callback call by toku_ftnode_fetch_callback or toku_ftnode_pf_req_callback
    // thi callbacks need to evaluate this anyway, so we cache it here so the search code does not reevaluate it
    int child_to_read;
    // when we read internal nodes, we want to read all the data off disk in one I/O
    // then we'll treat it as normal and only decompress the needed partitions etc.

    bool read_all_partitions;
    // Accounting: How many bytes were read, and how much time did we spend doing I/O?
    uint64_t bytes_read;
    tokutime_t io_time;
    tokutime_t decompress_time;
    tokutime_t deserialize_time;
};
typedef struct ftnode_fetch_extra *FTNODE_FETCH_EXTRA;

//
// TODO: put the heaviside functions into their respective 'struct .*extra;' namespaces
//
struct toku_msg_buffer_key_msn_heaviside_extra {
    DESCRIPTOR desc;
    ft_compare_func cmp;
    message_buffer *msg_buffer;
    const DBT *key;
    MSN msn;
};
int toku_msg_buffer_key_msn_heaviside(const int32_t &v, const struct toku_msg_buffer_key_msn_heaviside_extra &extra);

struct toku_msg_buffer_key_msn_cmp_extra {
    DESCRIPTOR desc;
    ft_compare_func cmp;
    message_buffer *msg_buffer;
};
int toku_msg_buffer_key_msn_cmp(const struct toku_msg_buffer_key_msn_cmp_extra &extrap, const int &a, const int &b);

struct toku_msg_leafval_heaviside_extra {
    ft_compare_func compare_fun;
    DESCRIPTOR desc;
    DBT const * const key;
};
int toku_msg_leafval_heaviside(DBT const &kdbt, const struct toku_msg_leafval_heaviside_extra &be);

unsigned int toku_bnc_nbytesinbuf(NONLEAF_CHILDINFO bnc);
int toku_bnc_n_entries(NONLEAF_CHILDINFO bnc);
long toku_bnc_memory_size(NONLEAF_CHILDINFO bnc);
long toku_bnc_memory_used(NONLEAF_CHILDINFO bnc);
void toku_bnc_insert_msg(NONLEAF_CHILDINFO bnc, const void *key, ITEMLEN keylen, const void *data, ITEMLEN datalen, enum ft_msg_type type, MSN msn, XIDS xids, bool is_fresh, DESCRIPTOR desc, ft_compare_func cmp);
void toku_bnc_empty(NONLEAF_CHILDINFO bnc);
void toku_bnc_flush_to_child(FT h, NONLEAF_CHILDINFO bnc, FTNODE child, TXNID parent_oldest_referenced_xid_known);
bool toku_bnc_should_promote(FT ft, NONLEAF_CHILDINFO bnc) __attribute__((const, nonnull));

bool toku_ftnode_nonleaf_is_gorged(FTNODE node, uint32_t nodesize);
uint32_t toku_ftnode_leaf_num_entries(FTNODE node);
void toku_ftnode_leaf_rebalance(FTNODE node, unsigned int basementnodesize);

void toku_ftnode_leaf_run_gc(FT ft, FTNODE node);

enum reactivity {
    RE_STABLE,
    RE_FUSIBLE,
    RE_FISSIBLE
};

enum reactivity toku_ftnode_get_reactivity(FT ft, FTNODE node);
enum reactivity toku_ftnode_get_nonleaf_reactivity(FTNODE node, unsigned int fanout);
enum reactivity toku_ftnode_get_leaf_reactivity(FTNODE node, uint32_t nodesize);

/**
 * Finds the next child for HOT to flush to, given that everything up to
 * and including k has been flattened.
 *
 * If k falls between pivots in node, then we return the childnum where k
 * lies.
 *
 * If k is equal to some pivot, then we return the next (to the right)
 * childnum.
 */
int toku_ftnode_hot_next_child(FTNODE node, const DBT *k,
                               DESCRIPTOR desc, ft_compare_func cmp);

void toku_ftnode_put_msg(ft_compare_func compare_fun, ft_update_func update_fun,
                         DESCRIPTOR desc, FTNODE node, int target_childnum,
                         FT_MSG msg, bool is_fresh, txn_gc_info *gc_info,
                         size_t flow_deltas[], STAT64INFO stats_to_update);

void toku_ft_bn_apply_msg_once(BASEMENTNODE bn, const FT_MSG msg, uint32_t idx,
                               uint32_t le_keylen, LEAFENTRY le, txn_gc_info *gc_info,
                               uint64_t *workdonep, STAT64INFO stats_to_update);

void toku_ft_bn_apply_msg(ft_compare_func compare_fun, ft_update_func update_fun,
                          DESCRIPTOR desc, BASEMENTNODE bn, FT_MSG msg, txn_gc_info *gc_info,
                          uint64_t *workdone, STAT64INFO stats_to_update);

void toku_ft_leaf_apply_msg(ft_compare_func compare_fun, ft_update_func update_fun,
                            DESCRIPTOR desc, FTNODE node, int target_childnum,
                            FT_MSG msg, txn_gc_info *gc_info,
                            uint64_t *workdone, STAT64INFO stats_to_update);

CACHETABLE_WRITE_CALLBACK get_write_callbacks_for_node(FT ft);

//
// Message management for orthopush
//

struct ancestors {
    // This is the root node if next is NULL (since the root has no ancestors)
    FTNODE node;
    // Which buffer holds messages destined to the node whose ancestors this list represents.
    int childnum;
    struct ancestors *next;
};
typedef struct ancestors *ANCESTORS;

void toku_ft_bnc_move_messages_to_stale(FT ft, NONLEAF_CHILDINFO bnc);

void toku_move_ftnode_messages_to_stale(FT ft, FTNODE node);

// TODO: Should ft_handle just be FT?
void toku_apply_ancestors_messages_to_node(FT_HANDLE t, FTNODE node, ANCESTORS ancestors,
                                           struct pivot_bounds const *const bounds,
                                           bool *msgs_applied, int child_to_read);

bool toku_ft_leaf_needs_ancestors_messages(FT ft, FTNODE node, ANCESTORS ancestors,
                                           struct pivot_bounds const *const bounds,
                                           MSN *const max_msn_in_path, int child_to_read);

void toku_ft_bn_update_max_msn(FTNODE node, MSN max_msn_applied, int child_to_read);

struct ft_search;
int toku_ft_search_which_child(DESCRIPTOR desc, ft_compare_func cmp, FTNODE node, ft_search *search);

//
// internal node inline functions
// TODO: Turn the macros into real functions
//

static inline void set_BNULL(FTNODE node, int i) {
    paranoid_invariant(i >= 0);
    paranoid_invariant(i < node->n_children);
    node->bp[i].ptr.tag = BCT_NULL;
}

static inline bool is_BNULL (FTNODE node, int i) {
    paranoid_invariant(i >= 0);
    paranoid_invariant(i < node->n_children);
    return node->bp[i].ptr.tag == BCT_NULL;
}

static inline NONLEAF_CHILDINFO BNC(FTNODE node, int i) {
    paranoid_invariant(i >= 0);
    paranoid_invariant(i < node->n_children);
    FTNODE_CHILD_POINTER p = node->bp[i].ptr;
    paranoid_invariant(p.tag==BCT_NONLEAF);
    return p.u.nonleaf;
}

static inline void set_BNC(FTNODE node, int i, NONLEAF_CHILDINFO nl) {
    paranoid_invariant(i >= 0);
    paranoid_invariant(i < node->n_children);
    FTNODE_CHILD_POINTER *p = &node->bp[i].ptr;
    p->tag = BCT_NONLEAF;
    p->u.nonleaf = nl;
}

static inline BASEMENTNODE BLB(FTNODE node, int i) {
    paranoid_invariant(i >= 0);
    // The optimizer really doesn't like it when we compare
    // i to n_children as signed integers. So we assert that
    // n_children is in fact positive before doing a comparison
    // on the values forcibly cast to unsigned ints.
    paranoid_invariant(node->n_children > 0);
    paranoid_invariant((unsigned) i < (unsigned) node->n_children);
    FTNODE_CHILD_POINTER p = node->bp[i].ptr;
    paranoid_invariant(p.tag==BCT_LEAF);
    return p.u.leaf;
}

static inline void set_BLB(FTNODE node, int i, BASEMENTNODE bn) {
    paranoid_invariant(i >= 0);
    paranoid_invariant(i < node->n_children);
    FTNODE_CHILD_POINTER *p = &node->bp[i].ptr;
    p->tag = BCT_LEAF;
    p->u.leaf = bn;
}

static inline SUB_BLOCK BSB(FTNODE node, int i) {
    paranoid_invariant(i >= 0);
    paranoid_invariant(i < node->n_children);
    FTNODE_CHILD_POINTER p = node->bp[i].ptr;
    paranoid_invariant(p.tag==BCT_SUBBLOCK);
    return p.u.subblock;
}

static inline void set_BSB(FTNODE node, int i, SUB_BLOCK sb) {
    paranoid_invariant(i >= 0);
    paranoid_invariant(i < node->n_children);
    FTNODE_CHILD_POINTER *p = &node->bp[i].ptr;
    p->tag = BCT_SUBBLOCK;
    p->u.subblock = sb;
}

// ftnode partition macros
// BP stands for ftnode_partition
#define BP_BLOCKNUM(node,i) ((node)->bp[i].blocknum)
#define BP_STATE(node,i) ((node)->bp[i].state)
#define BP_WORKDONE(node, i)((node)->bp[i].workdone)

//
// macros for managing a node's clock
// Should be managed by ft-ops.c, NOT by serialize/deserialize
//

//
// BP_TOUCH_CLOCK uses a compare and swap because multiple threads
// that have a read lock on an internal node may try to touch the clock
// simultaneously
//
#define BP_TOUCH_CLOCK(node, i) ((node)->bp[i].clock_count = 1)
#define BP_SWEEP_CLOCK(node, i) ((node)->bp[i].clock_count = 0)
#define BP_SHOULD_EVICT(node, i) ((node)->bp[i].clock_count == 0)
// not crazy about having these two here, one is for the case where we create new
// nodes, such as in splits and creating new roots, and the other is for when
// we are deserializing a node and not all bp's are touched
#define BP_INIT_TOUCHED_CLOCK(node, i) ((node)->bp[i].clock_count = 1)
#define BP_INIT_UNTOUCHED_CLOCK(node, i) ((node)->bp[i].clock_count = 0)

// ftnode leaf basementnode macros, 
#define BLB_MAX_MSN_APPLIED(node,i) (BLB(node,i)->max_msn_applied)
#define BLB_MAX_DSN_APPLIED(node,i) (BLB(node,i)->max_dsn_applied)
#define BLB_DATA(node,i) (&(BLB(node,i)->data_buffer))
#define BLB_NBYTESINDATA(node,i) (BLB_DATA(node,i)->get_disk_size())
#define BLB_SEQINSERT(node,i) (BLB(node,i)->seqinsert)