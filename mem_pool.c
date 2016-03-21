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

    // ensure that it's called only once until mem_free
    if(pool_store != NULL) {
        return ALLOC_CALLED_AGAIN;
    }

    // allocate the pool store with initial capacity
    pool_store = (pool_mgr_pt*) calloc(MEM_POOL_STORE_INIT_CAPACITY, sizeof(pool_mgr_pt));

    //sets the initial fill size to 0
    pool_store_size = 0;

    //sets the initial pool store max capacity
    pool_store_capacity = MEM_POOL_STORE_INIT_CAPACITY;

    if(pool_store != NULL) {
        return ALLOC_OK;
    }
    else return ALLOC_FAIL;

}

alloc_status mem_free() {

    // ensure that it's called only once for each mem_init
    if(!pool_store) {
        return ALLOC_CALLED_AGAIN;
    }

    // make sure all pool managers have been deallocated
    for(int i = 0; i < pool_store_size; ++i){
        if(pool_store[i] != NULL) {
            if(mem_pool_close(&pool_store[i]->pool) != ALLOC_OK) {
                return ALLOC_FAIL;
            }
        }

    }

    // can free the pool store array
    free(pool_store);

    // update static variables
    pool_store_size = 0;
    pool_store_capacity = 0;
    pool_store = NULL;

    // one last NULL check
    if(pool_store == NULL)
        // Success!
        return ALLOC_OK;
    else
        // Mem free failed
        return ALLOC_FAIL;
}

pool_pt mem_pool_open(size_t size, alloc_policy policy) {
    // make sure there the pool store is allocated
    // expand the pool store, if necessary
    // allocate a new mem pool mgr
    // check success, on error return null
    // allocate a new memory pool
    // check success, on error deallocate mgr and return null
    // allocate a new node heap
    // check success, on error deallocate mgr/pool and return null
    // allocate a new gap index
    // check success, on error deallocate mgr/pool/heap and return null
    // assign all the pointers and update meta data:
    //   initialize top node of node heap
    //   initialize top node of gap index
    //   initialize pool mgr
    //   link pool mgr to pool store
    // return the address of the mgr, cast to (pool_pt)
    //

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

    // assign all the pointers and update meta data

    //   initialize top node of node heap
    mgr->node_heap[0].next = NULL;
    mgr->node_heap[0].prev = NULL;
    mgr->node_heap[0].allocated = 0;
    mgr->node_heap[0].used = 0;
    mgr->node_heap[0].alloc_record.mem = mgr->pool.mem;
    mgr->node_heap[0].alloc_record.size = size;

    // initialize top node of gap index
    mgr->gap_ix[0].size = mgr->node_heap->alloc_record.size;
    mgr->gap_ix[0].node = mgr->node_heap;
    mgr->gap_ix_capacity = MEM_GAP_IX_INIT_CAPACITY;

    // initialize pool mgr
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

    // note: don't decrement pool_store_size, because it only grows

    // get mgr from pool by casting the pointer to (pool_mgr_pt)
    pool_mgr_pt manager = (pool_mgr_pt)pool;

    // check if this pool is allocated
    if(pool == NULL)
        return ALLOC_NOT_FREED;

    // check if it has zero allocations
    if(pool->num_gaps > 1) {
        return ALLOC_NOT_FREED;
    }

    // check for zero allocations
    if(pool->num_allocs > 0) {
        return ALLOC_NOT_FREED;
    }

    // free memory pool
    free(manager->pool.mem);

    // free node heap
    free(manager->node_heap);

    // free gap index
    free(manager->gap_ix);

    // find mgr in pool store and set to null
    int i = 0;
    while(pool_store[i] != manager){
        ++i;
    }

    // set to NULL
    pool_store[i] = NULL;

    // free mgr
    free(manager); //freeing memory

    return ALLOC_OK;
}

alloc_pt mem_new_alloc(pool_pt pool, size_t size) {

    // get mgr from pool by casting the pointer to (pool_mgr_pt)
    pool_mgr_pt manager = (pool_mgr_pt) pool;
    // alloc_pt newAlloc;

    // check if any gaps, return null if none
    if(manager->gap_ix[0].node == NULL){
        return NULL;
    }

    // expand heap node, if necessary, quit on error
    if (((float) manager -> used_nodes / manager -> total_nodes) > MEM_NODE_HEAP_FILL_FACTOR) {
        alloc_status resize = _mem_resize_node_heap(manager);
        if (resize != ALLOC_OK) {
            return NULL;
        }
    }

    // check used nodes fewer than total nodes, quit on error
    if (manager -> used_nodes >= manager -> total_nodes) {
        return NULL;
    }

    // get a node for allocation:
    node_pt new_node;
    int i = 0;

    // if FIRST_FIT, then find the first sufficient node in the node heap
    if(manager -> pool.policy == FIRST_FIT) {
        node_pt this_node = manager -> node_heap;
       while ((manager -> node_heap[i].allocated != 0) || (manager -> node_heap[i].alloc_record.size < size) && i < manager -> total_nodes) {
           ++i;
       }

        if ( i == manager -> total_nodes) {
            return NULL;
        }

        new_node = &manager -> node_heap[i];
    }

    // if BEST_FIT, then find the first sufficient node in the gap index
    else if (manager -> pool.policy == BEST_FIT) {
        if (manager ->pool.num_gaps > 0) {
            while (i < manager -> pool.num_gaps && manager -> gap_ix[i+1].size >= size) {
                if (manager -> gap_ix[i].size == size ) {
                    break;
                }
                ++i;
            }
        } else {
            return NULL;
        }

        // check if node found
        new_node = manager -> gap_ix[i].node;
    }

    // update metadata (num_allocs, alloc_size)
    manager -> pool.num_allocs++;
    manager -> pool.alloc_size += size;

    // calculate the size of the remaining gap, if any
    int size_of_gap = 0;
    if(new_node -> alloc_record.size - size > 0) {
        size_of_gap = new_node -> alloc_record.size - size;
    }

    // remove node from gap index
    if(_mem_remove_from_gap_ix(manager,size,new_node) != ALLOC_OK){
        return NULL;
    }

    // convert gap_node to an allocation node of given size
    new_node -> allocated = 1;
    new_node -> used = 1;
    new_node -> alloc_record.size = size;


    // adjust node heap:
    //   if remaining gap, need a new node
    if (size_of_gap > 0) {
        int j = 0;

        //   find an unused one in the node heap
        while (manager -> node_heap[j].used != 0) { // change .used to .allocated
            ++j;
        }

        node_pt new_gap_created = &manager -> node_heap[j];


        //   make sure one was found
        if (new_gap_created != NULL) {

            //   initialize it to a gap node
            new_gap_created -> used = 1;
            new_gap_created -> allocated = 0;
            new_gap_created -> alloc_record.size = size_of_gap;
        }

        //   update metadata (used_nodes)
        manager -> used_nodes++;

        // update linked list (new node right after the node for allocation)
        new_gap_created -> next = new_node -> next;

        if(new_node -> next != NULL) {
            new_node -> next -> prev = new_gap_created;
        }

        new_node -> next = new_gap_created;

        new_gap_created -> prev = new_node;

        // add to gap index
        _mem_add_to_gap_ix(manager, size_of_gap, new_gap_created);

    }

    // return allocation record by casting the node to (alloc_pt)
    return (alloc_pt) new_node;

    //   check if successful
}

alloc_status mem_del_alloc(pool_pt pool, alloc_pt alloc) {
    // get mgr from pool by casting the pointer to (pool_mgr_pt)
    // get node from alloc by casting the pointer to (node_pt)
    // find the node in the node heap
    // this is node-to-delete
    // make sure it's found
    // convert to gap node
    // update metadata (num_allocs, alloc_size)
    // if the next node in the list is also a gap, merge into node-to-delete
    //   remove the next node from gap index
    //   check success
    //   add the size to the node-to-delete
    //   update node as unused
    //   update metadata (used nodes)
    //   update linked list:
    /*
                    if (next->next) {
                        next->next->prev = node_to_del;
                        node_to_del->next = next->next;
                    } else {
                        node_to_del->next = NULL;
                    }
                    next->next = NULL;
                    next->prev = NULL;
     */

    // this merged node-to-delete might need to be added to the gap index
    // but one more thing to check...
    // if the previous node in the list is also a gap, merge into previous!
    //   remove the previous node from gap index
    //   check success
    //   add the size of node-to-delete to the previous
    //   update node-to-delete as unused
    //   update metadata (used_nodes)
    //   update linked list
    /*
                    if (node_to_del->next) {
                        prev->next = node_to_del->next;
                        node_to_del->next->prev = prev;
                    } else {
                        prev->next = NULL;
                    }
                    node_to_del->next = NULL;
                    node_to_del->prev = NULL;
     */
    //   change the node to add to the previous node!
    // add the resulting node to the gap index
    // check success

    pool_mgr_pt manager = (pool_mgr_pt) pool;

    node_pt node = (node_pt) alloc;

    node_pt delete_node = NULL;

    for(int i = 0; i < manager -> total_nodes; ++i) {
        if (node == &manager -> node_heap[i]) {
            delete_node = &manager -> node_heap[i];
            break;
        }
    }

    if(delete_node == NULL) {
        return ALLOC_FAIL;
    }

    delete_node -> allocated = 0;

    manager -> pool.num_allocs--;
    manager -> pool.alloc_size -= delete_node -> alloc_record.size;

    

    return ALLOC_OK;
}

void mem_inspect_pool(pool_pt pool, pool_segment_pt *segments, unsigned *num_segments) {
    // get the mgr from the pool
    // allocate the segments array with size == used_nodes
    // check successful
    // loop through the node heap and the segments array
    //    for each node, write the size and allocated in the segment
    // "return" the values:
    /*
                    *segments = segs;
                    *num_segments = pool_mgr->used_nodes;
     */
}



/***********************************/
/*                                 */
/* Definitions of static functions */
/*                                 */
/***********************************/

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