//
// This file contains all of the implementations of the replacement_policy
// constructors from the replacement_policies.h file.
//
// It also contains stubs of all of the functions that are added to each
// replacement_policy struct at construction time.
//
// ============================================================================
// NOTE: It is recommended that you read the comments in the
// replacement_policies.h file for further context on what each function is
// for.
// ============================================================================
//

#include "replacement_policies.h"
#include <unistd.h>

// LRU Replacement Policy
// ============================================================================
// TODO feel free to create additional structs/enums as necessary
struct lru_policy_data {
    // total sets
    uint32_t sets;
    // lines in each set
    uint32_t associativity;
    // goes up each access
    uint64_t clock;
    // time used for each line
    uint64_t *last_used;
};
struct rand_policy_data {
    uint32_t associativity;
};
struct lru_prefer_clean_policy_data {
    uint32_t sets;
    uint32_t associativity;
    uint64_t clock;
    uint64_t *last_used;
};

void lru_cache_access(struct replacement_policy *replacement_policy,
                      struct cache_system *cache_system, uint32_t set_idx, uint32_t tag)
{
    struct lru_policy_data *data = replacement_policy->data;
    uint32_t set_start = set_idx * data->associativity;

    // line touched now is newest
    data->clock++;
    for (uint32_t i = 0; i < data->associativity; i++) {
        struct cache_line *cl = &cache_system->cache_lines[set_start + i];
        if (cl->status != INVALID && cl->tag == tag) {
            data->last_used[set_start + i] = data->clock;
            return;
        }
    }
}

uint32_t lru_eviction_index(struct replacement_policy *replacement_policy,
                            struct cache_system *cache_system, uint32_t set_idx)
{
    (void)cache_system;
    struct lru_policy_data *data = replacement_policy->data;
    uint32_t set_start = set_idx * data->associativity;
    // pick least recent one
    uint32_t evict_idx = 0;
    uint64_t oldest = data->last_used[set_start];

    for (uint32_t i = 1; i < data->associativity; i++) {
        uint64_t ts = data->last_used[set_start + i];
        if (ts < oldest) {
            oldest = ts;
            evict_idx = i;
        }
    }

    return evict_idx;
}

void lru_replacement_policy_cleanup(struct replacement_policy *replacement_policy)
{
    struct lru_policy_data *data = replacement_policy->data;
    // free metadata
    free(data->last_used);
    free(data);
}

struct replacement_policy *lru_replacement_policy_new(uint32_t sets, uint32_t associativity)
{
    struct replacement_policy *lru_rp = calloc(1, sizeof(struct replacement_policy));
    lru_rp->cache_access = &lru_cache_access;
    lru_rp->eviction_index = &lru_eviction_index;
    lru_rp->cleanup = &lru_replacement_policy_cleanup;

    struct lru_policy_data *data = calloc(1, sizeof(struct lru_policy_data));
    data->sets = sets;
    data->associativity = associativity;
    data->clock = 0;
    data->last_used = calloc(sets * associativity, sizeof(uint64_t));
    lru_rp->data = data;

    return lru_rp;
}

// RAND Replacement Policy
// ============================================================================
void rand_cache_access(struct replacement_policy *replacement_policy,
                       struct cache_system *cache_system, uint32_t set_idx, uint32_t tag)
{
    (void)replacement_policy;
    (void)cache_system;
    (void)set_idx;
    (void)tag;
    // no state updates for rand
}

uint32_t rand_eviction_index(struct replacement_policy *replacement_policy,
                             struct cache_system *cache_system, uint32_t set_idx)
{
    (void)cache_system;
    (void)set_idx;
    struct rand_policy_data *data = replacement_policy->data;

    // any line can be evicted
    return rand() % data->associativity;
}

void rand_replacement_policy_cleanup(struct replacement_policy *replacement_policy)
{
    free(replacement_policy->data);
}

struct replacement_policy *rand_replacement_policy_new(uint32_t sets, uint32_t associativity)
{
    (void)sets;
    // Seed randomness once with sub-second resolution and process entropy
    static bool seeded = false;
    if (!seeded) {
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        unsigned int seed = (unsigned int)(ts.tv_nsec ^ ts.tv_sec ^ getpid());
        srand(seed);
        seeded = true;
    }

    struct replacement_policy *rand_rp = malloc(sizeof(struct replacement_policy));
    rand_rp->cache_access = &rand_cache_access;
    rand_rp->eviction_index = &rand_eviction_index;
    rand_rp->cleanup = &rand_replacement_policy_cleanup;

    struct rand_policy_data *data = malloc(sizeof(struct rand_policy_data));
    data->associativity = associativity;
    rand_rp->data = data;

    return rand_rp;
}

// LRU_PREFER_CLEAN Replacement Policy
// ============================================================================
void lru_prefer_clean_cache_access(struct replacement_policy *replacement_policy,
                                   struct cache_system *cache_system, uint32_t set_idx,
                                   uint32_t tag)
{
    struct lru_prefer_clean_policy_data *data = replacement_policy->data;
    uint32_t set_start = set_idx * data->associativity;

    data->clock++;
    for (uint32_t i = 0; i < data->associativity; i++) {
        struct cache_line *cl = &cache_system->cache_lines[set_start + i];
        if (cl->status != INVALID && cl->tag == tag) {
            data->last_used[set_start + i] = data->clock;
            return;
        }
    }
}

uint32_t lru_prefer_clean_eviction_index(struct replacement_policy *replacement_policy,
                                         struct cache_system *cache_system, uint32_t set_idx)
{
    struct lru_prefer_clean_policy_data *data = replacement_policy->data;
    uint32_t set_start = set_idx * data->associativity;

    bool found_clean = false;
    // best clean victim
    uint32_t clean_evict_idx = 0;
    uint64_t clean_oldest = 0;

    uint32_t any_evict_idx = 0;
    uint64_t any_oldest = data->last_used[set_start];

    for (uint32_t i = 0; i < data->associativity; i++) {
        struct cache_line *cl = &cache_system->cache_lines[set_start + i];
        uint64_t ts = data->last_used[set_start + i];

        if (i == 0 || ts < any_oldest) {
            any_oldest = ts;
            any_evict_idx = i;
        }

        if (cl->status != MODIFIED) {
            if (!found_clean || ts < clean_oldest) {
                found_clean = true;
                clean_oldest = ts;
                clean_evict_idx = i;
            }
        }
    }

    return found_clean ? clean_evict_idx : any_evict_idx;
}

void lru_prefer_clean_replacement_policy_cleanup(struct replacement_policy *replacement_policy)
{
    struct lru_prefer_clean_policy_data *data = replacement_policy->data;
    // free metadata again here
    free(data->last_used);
    free(data);
}

struct replacement_policy *lru_prefer_clean_replacement_policy_new(uint32_t sets,
                                                                   uint32_t associativity)
{
    struct replacement_policy *lru_prefer_clean_rp = malloc(sizeof(struct replacement_policy));
    lru_prefer_clean_rp->cache_access = &lru_prefer_clean_cache_access;
    lru_prefer_clean_rp->eviction_index = &lru_prefer_clean_eviction_index;
    lru_prefer_clean_rp->cleanup = &lru_prefer_clean_replacement_policy_cleanup;

    struct lru_prefer_clean_policy_data *data =
        calloc(1, sizeof(struct lru_prefer_clean_policy_data));
    data->sets = sets;
    data->associativity = associativity;
    data->clock = 0;
    data->last_used = calloc(sets * associativity, sizeof(uint64_t));
    lru_prefer_clean_rp->data = data;

    return lru_prefer_clean_rp;
}
