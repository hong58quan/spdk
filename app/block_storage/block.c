#include "spdk/stdinc.h"

#include "spdk/event.h"
#include "spdk/log.h"
#include "raft/include/raft.h"
#include "pg_group.h"

static const char *g_pid_path = NULL;
static int global_osd_id = 0;

typedef struct
{
    /* the server's node ID */
    int node_id;
}server_t;

// server_t global_server;

static void
block_usage(void)
{
	printf(" -f <path>                 save pid to file under given path\n");
	printf(" -I <id>                   save osd id\n");
}

static void
save_pid(const char *pid_path)
{
	FILE *pid_file;

	pid_file = fopen(pid_path, "w");
	if (pid_file == NULL) {
		fprintf(stderr, "Couldn't create pid file '%s': %s\n", pid_path, strerror(errno));
		exit(EXIT_FAILURE);
	}

	fprintf(pid_file, "%d\n", getpid());
	fclose(pid_file);
}

static int
block_parse_arg(int ch, char *arg)
{
	switch (ch) {
	case 'f':
		g_pid_path = arg;
		break;
	case 'I':
	    global_osd_id = atoi(arg);
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static void
block_started(void *arg1)
{
    server_t *server = (server_t *)arg1;
    SPDK_NOTICELOG("block start\n");

    pg_group_init(server->node_id);
}

int
main(int argc, char *argv[])
{
	struct spdk_app_opts opts = {};
	server_t server = {};
	int rc;

	spdk_app_opts_init(&opts, sizeof(opts));
	opts.name = "block";

	if ((rc = spdk_app_parse_args(argc, argv, &opts, "f:I:", NULL,
				      block_parse_arg, block_usage)) !=
	    SPDK_APP_PARSE_ARGS_SUCCESS) {
		exit(rc);
	}
	server.node_id = global_osd_id;

	if (g_pid_path) {
		save_pid(g_pid_path);
	}

	/* Blocks until the application is exiting */
	rc = spdk_app_start(&opts, block_started, &server);

	spdk_app_fini();

	return rc;
}