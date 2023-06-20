#include "pg_group.h"
#include "spdk/env.h"
#include "spdk/log.h"
#include "spdk/thread.h"

#define TIMER_PERIOD_MSEC 500    //毫秒

#define  ELECTION_TIMER_PERIOD_MSEC  1000   //毫秒

pg_group_t global_pg_group;

static inline char * pg_id_to_name(char *name, size_t size, uint64_t pool_id, uint64_t pg_id){
    snprintf(name, size, "%lu.%lu", pool_id, pg_id);
    return name;
}

static int pg_cmp(pg_t *pg1, pg_t *pg2)
{
	return strcmp(pg1->name, pg2->name);
}
RB_GENERATE_STATIC(pgs_tree, pg_t, node, pg_cmp);

static int pg_core_cmp(pg_core_t *pg1, pg_core_t *pg2)
{
	return strcmp(pg1->name, pg2->name);
}
RB_GENERATE_STATIC(pg_core_tree, pg_core_t, node, pg_core_cmp);

static int _pg_add(raft_server_t *rs, uint64_t pool_id, uint64_t pg_id, pg_t **res_pg){
    pg_t *pg = spdk_zmalloc(sizeof(pg_t), 0x20, NULL, SPDK_ENV_LCORE_ID_ANY, SPDK_MALLOC_DMA);
    pg_t *tmp = NULL;

    pg->raft = rs;
    pg_id_to_name(pg->name, sizeof(pg->name), pool_id, pg_id);
    tmp = RB_INSERT(pgs_tree, &global_pg_group.pgs, pg);
    if(tmp){
        SPDK_ERRLOG("pg %lu.%lu already exists\n", pool_id, pg_id);
        spdk_free(pg);
        return -EEXIST;
    }
    if(res_pg){
        *res_pg = pg;
    }
    return 0;
}

pg_t * get_pg_by_id(uint64_t pool_id, uint64_t pg_id){
    pg_t pg;

    pg_id_to_name(pg.name, sizeof(pg.name), pool_id, pg_id);
    return RB_FIND(pgs_tree, &global_pg_group.pgs, &pg);
}

static void free_pg(pg_t *pg){
    //可能还需要其它处理 ？

    spdk_poller_unregister(&pg->timer);
    raft_destroy_nodes(pg->raft);
    raft_free(pg->raft);
    spdk_free(pg);    
}

static int _pg_remove(uint64_t pool_id, uint64_t pg_id){
    pg_t *pg = get_pg_by_id(pool_id, pg_id);
    if(!pg){
        return 0;
    }
    RB_REMOVE(pgs_tree, &global_pg_group.pgs, pg);
    free_pg(pg);
    return 0;
}

void pg_group_init(int current_node_id){
    RB_INIT(&global_pg_group.pgs);
    RB_INIT(&global_pg_group.core_table);
    global_pg_group.next_core = 0;
    global_pg_group.core_num = spdk_env_get_last_core() + 1;
    global_pg_group.current_node_id = current_node_id;
}

static uint32_t get_next_core_id(){
    uint32_t core_id = global_pg_group.next_core;
    global_pg_group.next_core = (global_pg_group.next_core + 1) % global_pg_group.core_num;
    return core_id;
}

static int _add_pg_core(uint64_t pool_id, uint64_t pg_id, uint32_t core_id){
    pg_core_t *core = spdk_zmalloc(sizeof(pg_core_t), 0x20, NULL, SPDK_ENV_LCORE_ID_ANY, SPDK_MALLOC_DMA);
    pg_core_t *tmp = NULL;

    core->core = core_id;
    pg_id_to_name(core->name, sizeof(core->name), pool_id, pg_id);
    tmp = RB_INSERT(pg_core_tree, &global_pg_group.core_table, core);
    if(tmp){
        SPDK_ERRLOG("pg %lu.%lu already exists\n", pool_id, pg_id);
        spdk_free(core);
        return -EEXIST;
    }    
    return 0;
}

pg_core_t * get_core_by_id(uint64_t pool_id, uint64_t pg_id){
    pg_core_t core;

    pg_id_to_name(core.name, sizeof(core.name), pool_id, pg_id);
    return RB_FIND(pg_core_tree, &global_pg_group.core_table, &core);
}

static int _pg_core_remove(uint64_t pool_id, uint64_t pg_id){
    pg_core_t *core = get_core_by_id(pool_id, pg_id);
    if(!core){
        return -EEXIST;
    }
    RB_REMOVE(pg_core_tree, &global_pg_group.core_table, core);
    spdk_free(core);
    return 0;
}


/** Raft callback for handling periodic logic */
static int _periodic(void* arg){
    pg_t* pg = (pg_t*)arg;
	SPDK_NOTICELOG("_periodic\n");
    raft_periodic(pg->raft);
    return 0;
}

static void start_raft_periodic_timer(pg_t* pg){
    pg->timer = SPDK_POLLER_REGISTER(_periodic, pg, TIMER_PERIOD_MSEC * 1000);
	raft_set_election_timeout(pg->raft, ELECTION_TIMER_PERIOD_MSEC);
}

raft_cbs_t raft_funcs = {

};

int create_pg(uint64_t pool_id, uint64_t pg_id, osd_info_t *osds, int num_osd){
    raft_server_t* raft = NULL;
    uint32_t core_id;
    pg_t *pg = NULL;
    int ret = 0;
    int i = 0;

    if(get_pg_by_id(pool_id, pg_id)){
        return -EEXIST;
    }
    raft = raft_new();
    // raft_set_callbacks(raft, &raft_funcs, NULL);

    core_id = get_next_core_id();
    ret = _pg_add(raft, pool_id, pg_id, &pg);
    if(ret != 0){
        raft_free(raft);
        return ret; 
    }

    ret = _add_pg_core(pool_id, pg_id, core_id);
    if(ret != 0){
        _pg_remove(pool_id, pg_id);
        return ret; 
    }   
    for(i = 0; i < num_osd; i++){
        if(osds[i].node_id == global_pg_group.current_node_id){
            raft_add_node(raft, NULL, osds[i].node_id, 1);
        }else{
            /*
               这里需要连接osd。raft_add_node函数的user_data参数可以是osd连接的接口
            */

            raft_add_node(raft, NULL, osds[i].node_id, 0);
        }
    }

    raft_set_current_term(raft, 1);
    start_raft_periodic_timer(pg);

    return 0;
}

void delete_pg(uint64_t pool_id, uint64_t pg_id){
    if(_pg_core_remove(pool_id, pg_id) != 0){
        return;
    }
    _pg_remove(pool_id, pg_id);
}


