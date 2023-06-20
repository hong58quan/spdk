#ifndef PG_GROUP_H
#define PG_GROUP_H

#include "spdk/stdinc.h"
#include "spdk/tree.h"
#include "spdk/queue.h"
#include "raft/include/raft.h"

#define PG_NAME_SIZE 64

typedef struct pg_t{
	RB_ENTRY(pg_t)		node;   

    raft_server_t *raft;   
    struct spdk_poller * timer; 
    char name[PG_NAME_SIZE];
}pg_t;

typedef struct pg_core_t{
    RB_ENTRY(pg_core_t)		node;   
    char name[PG_NAME_SIZE];
    uint32_t core;  //cpu core
}pg_core_t;

typedef struct pg_group_t{
    RB_HEAD(pgs_tree, pg_t) pgs;
    RB_HEAD(pg_core_tree, pg_core_t) core_table;
    uint32_t next_core;
    uint32_t core_num;
    int current_node_id;
}pg_group_t;

typedef struct osd_info_t{
    int node_id;
    struct sockaddr_in addr;
}osd_info_t;

pg_t *get_pg_by_id(uint64_t pool_id, uint64_t pg_id);

void pg_group_init(int current_node_id);

pg_core_t * get_core_by_id(uint64_t pool_id, uint64_t pg_id);

int create_pg(uint64_t pool_id, uint64_t pg_id, osd_info_t *osds, int num_osd);

void delete_pg(uint64_t pool_id, uint64_t pg_id);

#endif