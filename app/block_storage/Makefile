
SPDK_ROOT_DIR := $(abspath $(CURDIR)/../..)
include $(SPDK_ROOT_DIR)/mk/spdk.common.mk
include $(SPDK_ROOT_DIR)/mk/spdk.modules.mk

APP = block

C_SRCS := raft/src/raft_log.c  raft/src/raft_node.c raft/src/raft_server_properties.c raft/src/raft_server.c  block.c

CFLAGS += -Iraft/include

SPDK_LIB_LIST = $(ALL_MODULES_LIST) event 

ifeq ($(SPDK_ROOT_DIR)/lib/env_dpdk,$(CONFIG_ENV))
SPDK_LIB_LIST += env_dpdk_rpc
endif

include $(SPDK_ROOT_DIR)/mk/spdk.app.mk
