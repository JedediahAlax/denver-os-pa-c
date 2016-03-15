#include <stdlib.h>
#include <assert.h>
#include <stdio.h> // for perror()

#include "mem_pool.h"

/* Constants */
static const float      MEM_FILL_FACTOR                 = 0.75;
static const unsigned   MEM_EXPAND_FACTOR               = 2;

static const unsigned   MEM_POOL_STORE_INIT_CAPACITY    = 20;
static const float      MEM_POOL_STORE_FILL_FACTOR      = 0.75;
static const unsigned   MEM_POOL_STORE_EXPAND_FACTOR    = 2;

static const unsigned   MEM_NODE_HEAP_INIT_CAPACITY     = 40;
static const float      MEM_NODE_HEAP_FILL_FACTOR       = 0.75;
static const unsigned   MEM_NODE_HEAP_EXPAND_FACTOR     = 2;

static const unsigned   MEM_GAP_IX_INIT_CAPACITY        = 40;
static const float      MEM_GAP_IX_FILL_FACTOR          = 0.75;
static const unsigned   MEM_GAP_IX_EXPAND_FACTOR        = 2;

/* Type declarations */
typedef struct _node {
    alloc_t alloc_record;
    unsigned used;
    unsigned allocated;
    struct _node *next, *prev; // doubly-linked list for gap deletion
} node_t, *node_pt;

typedef struct _gap {
    size_t size;
    node_pt node;
} gap_t, *gap_pt;

typedef struct _pool_mgr {
    pool_t pool;
    node_pt node_heap;
    unsigned total_nodes;
    unsigned used_nodes;
    gap_pt gap_ix;
    unsigned gap_ix_capacity;
} pool_mgr_t, *pool_mgr_pt;


/* Static global variables */
static pool_mgr_pt *pool_store = NULL; // an array of pointers, only expand
static unsigned pool_store_size = 0;
static unsigned pool_store_capacity = 0;


/* Forward declarations of static functions */
static alloc_status _mem_resize_pool_store();
static alloc_status _mem_resize_node_heap(pool_mgr_pt pool_mgr);
static alloc_status _mem_resize_gap_ix(pool_mgr_pt pool_mgr);
static alloc_status _mem_add_to_gap_ix(pool_mgr_pt pool_mgr, size_t size, node_pt node);
static alloc_status _mem_remove_from_gap_ix(pool_mgr_pt pool_mgr, size_t size, node_pt node);
static alloc_status _mem_sort_gap_ix(pool_mgr_pt pool_mgr);


/* Definitions of user-facing functions */
alloc_status mem_init() {
    // TODO double check work
    if(pool_store != NULL) return ALLOC_CALLED_AGAIN;
    /* Allocates the pool_store with initial size */
    pool_store = (pool_mgr_pt*) calloc(MEM_POOL_STORE_INIT_CAPACITY, sizeof(pool_mgr_pt));
    pool_store_size = 0; //sets the initial fill size to 0
    pool_store_capacity = MEM_POOL_STORE_INIT_CAPACITY; //sets the initial pool store max capacity
    if(pool_store != NULL) return ALLOC_OK;
    else return ALLOC_FAIL;
}

alloc_status mem_free() {
    // ensure that it's called only once for each mem_init
    // make sure all pool managers have been deallocated
    if(!pool_store) return ALLOC_CALLED_AGAIN;
    for(int i = 0; i < pool_store_size; ++i){
        if(pool_store[i] != NULL)
            mem_pool_close(&pool_store[i]->pool);
    }
    free(pool_store);//Frees the pool_store memory
    pool_store_size = 0;
    pool_store_capacity = 0;
    pool_store = NULL;
    if(pool_store == NULL)
        return ALLOC_OK;
    else
        return ALLOC_FAIL;
}

pool_pt mem_pool_open(size_t size, alloc_policy policy) {
    // TODO ask questions

    // make sure there the pool store is allocated
    assert(pool_store);

    // expand the pool store, if necessary
    if (((float) pool_store_size / pool_store_capacity) > MEM_POOL_STORE_FILL_FACTOR) {
        alloc_status resize = _mem_resize_pool_store();
        if(resize != ALLOC_OK){
            return NULL;
        }
    }

    // allocate a new mem pool mgr
    pool_mgr_pt mgr = (pool_mgr_pt) malloc(sizeof(pool_mgr_t));

    // check success, on error return null
    if(mgr == NULL){
        return NULL;
    }

    // allocate a new memory pool
    mgr->pool.mem = (char *) malloc(size);

    // check success, on error deallocate mgr and return null
    if(mgr->pool.mem == NULL){
        free(mgr->pool.mem);
        return NULL;
    }

    // allocate a new node heap
    mgr->node_heap = (node_pt) calloc(MEM_NODE_HEAP_INIT_CAPACITY, sizeof(node_t));

    // check success, on error deallocate mgr/pool and return null
    if(mgr->node_heap == NULL){
        free(mgr->pool.mem);
        free(mgr);
        return NULL;
    }

    // allocate a new gap index
    mgr->gap_ix = (gap_pt) calloc(MEM_GAP_IX_INIT_CAPACITY, sizeof(gap_t));
    for(int i = 0; i < MEM_GAP_IX_INIT_CAPACITY; ++i){
        mgr->gap_ix[i].size = 0;
    }

    // check success, on error deallocate mgr/pool/heap and return null
    if(mgr->gap_ix == NULL){
        free(mgr->pool.mem);
        free(mgr->node_heap);
        free(mgr);
        return NULL;
    }

    // assign all the pointers and update meta data:

    //   initialize top node of node heap
    mgr->node_heap[0].next = NULL;
    mgr->node_heap[0].prev = NULL;
    mgr->node_heap[0].allocated = 0;
    mgr->node_heap[0].used = 0;
    mgr->node_heap[0].alloc_record.mem = mgr->pool.mem;
    mgr->node_heap[0].alloc_record.size = size;

    //   initialize top node of gap index
    mgr->gap_ix[0].size = mgr->node_heap->alloc_record.size;
    mgr->gap_ix[0].node = mgr->node_heap;
    mgr->gap_ix_capacity = MEM_GAP_IX_INIT_CAPACITY;

    //   initialize pool mgr
    mgr->pool.alloc_size = 0;
    mgr->pool.total_size = size;
    mgr->pool.num_allocs = 0;
    mgr->pool.num_gaps = 1;
    mgr->pool.policy = policy;
    mgr->total_nodes = MEM_NODE_HEAP_INIT_CAPACITY;
    mgr->used_nodes = 1;


    //   link pool mgr to pool store
    int i = 0;
    while (pool_store[i] != NULL) {
        ++i;
    }
    pool_store[i] = mgr; // inserting the new manager
    ++pool_store_size; //incrementing the size of the pool store

    // return the address of the mgr, cast to (pool_pt)
    return (pool_pt) mgr;
}

alloc_status mem_pool_close(pool_pt pool) {
    // get mgr from pool by casting the pointer to (pool_mgr_pt)
    pool_mgr_pt mgr = (pool_mgr_pt)pool;

    // check if the pool is allocated
    if(pool == NULL)
        return ALLOC_NOT_FREED;
    // check if pool has only one gap
    if(pool->num_gaps > 1) {
        return ALLOC_NOT_FREED;
    }
    // check for zero allocations
    if(pool->num_allocs > 0)
        return ALLOC_NOT_FREED;
    // free memory pool
    free(mgr->pool.mem);
    // free node heap
    free(mgr->node_heap);
    // free gap index
    free(mgr->gap_ix);

    // find mgr in pool store and set to null
    int i = 0;
    while(pool_store[i] != mgr){
        ++i;
    }
    pool_store[i] = NULL;

    // free mgr
    free(mgr); //freeing memory

    return ALLOC_OK;
}

alloc_pt mem_new_alloc(pool_pt pool, size_t size) {

    // get mgr from pool by casting the pointer to (pool_mgr_pt)
    pool_mgr_pt mgr = (pool_mgr_pt) pool;
    alloc_pt newAlloc;

    // check if any gaps, return null if none
    if(mgr->gap_ix[0].node == NULL){
        return NULL;
    }

}

alloc_status mem_del_alloc(pool_pt pool, alloc_pt alloc) {
    // get mgr from pool by casting the pointer to (pool_mgr_pt)
    pool_mgr_pt mgr = (pool_mgr_pt) pool;

    // get node from alloc by casting the pointer to (node_pt)
    node_pt node = (node_pt) alloc;

    node_pt del_node = NULL;
    // find the node in the node heap
    for(int i = 0; i< mgr->total_nodes; ++i){
        if(node == &mgr->node_heap[i]){
            del_node = &mgr->node_heap[i];
            break;
        }
    }
    // this is node-to-delete
    // make sure it's found
    if(del_node == NULL){
        return ALLOC_FAIL;
    }

    // convert to gap node
    del_node->allocated = 0;

    // update metadata (num_allocs, alloc_size)
    mgr->pool.num_allocs--;
    mgr->pool.alloc_size -= del_node->alloc_record.size;


    // if the next node in the list is also a gap, merge into node-to-delete
    if(del_node->next != NULL && del_node->next->allocated == 0) {
        node_pt next = del_node->next;
        //   remove the next node from gap index
        if(_mem_remove_from_gap_ix(mgr, 0, next) == ALLOC_FAIL)
            return ALLOC_FAIL;

        //   add the size to the node-to-delete
        del_node->alloc_record.size += next->alloc_record.size;
        //   update node as unused
        next->used = 0;
        //   update metadata (used nodes)
        mgr->used_nodes--;
        //   update linked list:
        if (next->next) {
            next->next->prev = del_node;
            del_node->next = next->next;
        } else {
            del_node->next = NULL;
        }
        next->next = NULL;
        next->prev = NULL;
    }
    // this merged node-to-delete might need to be added to the gap index
    // but one more thing to check...
    // if the previous node in the list is also a gap, merge into previous!
    if(del_node->prev!= NULL && del_node->prev->allocated == 0) {
        //   remove the previous node from gap index
        node_pt previous = del_node->prev;
        if(_mem_remove_from_gap_ix(mgr, 0, previous) == ALLOC_FAIL)
            return ALLOC_FAIL;

        //   add the size of node-to-delete to the previous
        previous->alloc_record.size += del_node->alloc_record.size;
        //   update node-to-delete as unused
        del_node->used = 0;
        //   update metadata (used_nodes)
        mgr->used_nodes--;
        //   update linked list
        if (del_node->next) {
            previous->next = del_node->next;
            del_node->next->prev = previous;
        } else {
            previous->next = NULL;
        }
        del_node->next = NULL;
        del_node->prev = NULL;

        //   change the node to add to the previous node!
        del_node = previous;
    }
    // add the resulting node to the gap index
    // check success
    if(_mem_add_to_gap_ix(mgr, del_node->alloc_record.size,del_node ) != ALLOC_OK)
        return ALLOC_FAIL;

    return ALLOC_OK;
}

void mem_inspect_pool(pool_pt pool, pool_segment_pt *segments, unsigned *num_segments) {
    // get the mgr from the pool
    pool_mgr_pt pool_mgr = (pool_mgr_pt) pool;

    // allocate the segments array with size == used_nodes
    pool_segment_pt segs = (pool_segment_pt) calloc(pool_mgr->used_nodes, sizeof(pool_segment_t));

    // check successful
    assert(segs);
    node_pt current = pool_mgr->node_heap;

    // loop through the node heap and the segments array
    for(int i = 0; i < pool_mgr->used_nodes; ++i){
        //    for each node, write the size and allocated in the segment
        segs[i].size = current->alloc_record.size;
        segs[i].allocated = current->allocated;
        if(current->next != NULL) {
            current = current->next;
        }
    }

    // "return" the values:
    *segments = segs;
    *num_segments = pool_mgr->used_nodes;
}

/* Definitions of static functions */
static alloc_status _mem_resize_pool_store() {

    if (((float) pool_store_size / pool_store_capacity) > MEM_POOL_STORE_FILL_FACTOR) {
        /* Reallocating size for array */
        pool_store = realloc(pool_store, MEM_EXPAND_FACTOR * sizeof(pool_store));
        /* changing the max capacity variable */
        pool_store_capacity *= MEM_EXPAND_FACTOR;
        if (pool_store != NULL)
            return ALLOC_OK;
        else
            return ALLOC_FAIL;
    }
    return ALLOC_OK;
}

static alloc_status _mem_resize_node_heap(pool_mgr_pt pool_mgr) {

}

static alloc_status _mem_resize_gap_ix(pool_mgr_pt pool_mgr) {

    return ALLOC_OK;
}

static alloc_status _mem_add_to_gap_ix(pool_mgr_pt pool_mgr, size_t size, node_pt node) {

    return ALLOC_FAIL;
}

static alloc_status _mem_remove_from_gap_ix(pool_mgr_pt pool_mgr, size_t size, node_pt node) {
    int i = 0;
    while(pool_mgr->gap_ix[i].node != node){
        ++i;
    }
    pool_mgr->gap_ix[i].size = 0;
    pool_mgr->gap_ix[i].node = NULL;
    pool_mgr->pool.num_gaps--;
    //pool_mgr->used_nodes--;

    return ALLOC_OK;
}


static alloc_status _mem_sort_gap_ix(pool_mgr_pt pool_mgr) {


    return ALLOC_OK;
}