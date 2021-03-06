/*
 * Filename:		dsal_test_space_stats.c
 * Description:		Test group for storage space utilization stats.
 *
 * Copyright (c) 2020 Seagate Technology LLC and/or its Affiliates
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published
 * by the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU Affero General Public License for more details.
 * You should have received a copy of the GNU Affero General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 * For any questions about this software or licensing,
 * please email opensource@seagate.com or cortx-questions@seagate.com.
 */

#include <stdio.h> /* *printf */
#include <memory.h> /* mem* functions */
#include <assert.h> /* asserts */
#include <errno.h> /* errno codes */
#include <stdlib.h> /* alloc, free */
#include "dstore.h" /* dstore operations to be tested */
#include "dstore_bufvec.h" /* data buffers and vectors */
#include "dsal_test_lib.h" /* DSAL-specific helpers for tests */

/*****************************************************************************/

/** Test environment for the test group.
 * The environment is prepared by setup() and cleaned up by teardown()
 * calls.
 */
struct env {
	/* Object IDs to be used in the test cases.
	 * Rationale of keeping it global:
	 * 1. Each test case is responsibe for releasing this IDs.
	 * 2. If test fails then the process goes down, so that
	 *    a new ID will be generated so that we won't get any collisions.
	 */
	dstore_oid_t oids[NUM_OF_OBJECTS];
	/* Dstore instance.
	 * Rationale of keeping it global:
	 * It is a singleton and it is being initialized
	 * only once. The initialization is not a part
	 * of any test case in this module.
	 */
	struct dstore *dstore;
	char *server, *client;
	char cmd[1024];
};

#define ENV_FROM_STATE(__state) (*((struct env **) __state))

/* Function to print filesystem stats using m0_filesystem_stats
 * and to print the sparse file (being used as disks in virtual
 * environment) actual size
 */ 
static void print_fs_stats(struct env *env)
{
	printf("\n filesystem stats: \n");
	system(env->cmd);
	printf("\n Sparse file size \n");
	system("ls -lsh /var/motr/disk*");
}

/* Function to prepare command line for filesystem stats */
static void prepare_fs_cmd(struct env *env)
{
	char *fs_stat_path = "/bin/m0_filesystem_stats ";
	char *profile = " 0x7000000000000001,0x1e ";
	char *lib_path = " /usr/lib64/libmotr.so";

	sprintf(env->cmd, "%s -s %s -c %s -p %s -l %s", fs_stat_path,
		env->server, env->client, profile, lib_path);
}

/*****************************************************************************/
/* Description: Create big objects
 * Strategy:
 *	Create a number of big objects
 * Expected behavior:
 *	No errors. Storage space will be occupied by Motr
 * Enviroment:
 *	Empty dstore.
 */
static void test_creat_big_objects(void **state)
{
	int rc, i, j;
	size_t buf_size = 1024*1024; /* 1 MB */
	size_t offset = 0;
	void *raw_buf = NULL;
	struct dstore_obj *obj = NULL;
	struct stat stats;
	struct env *env = ENV_FROM_STATE(state);
	size_t bsize  = 4 << 10;

	memset(&stats, 0, sizeof(stats));
	raw_buf = calloc(1, buf_size);
	ut_assert_not_null(raw_buf);
	dtlib_fill_data_block(raw_buf, buf_size);

	/* print filesystm_stats */
	printf("\n Before creating objects\n");
	print_fs_stats(env);

	/* Create multiple Objects */
	for (i = 0; i < NUM_OF_OBJECTS; i++) {
		printf("\n Creating Object ... %d", i + 1);

		rc = dstore_obj_create(env->dstore, NULL, &env->oids[i]);
		ut_assert_int_equal(rc, 0);

		rc = dstore_obj_open(env->dstore, &env->oids[i], &obj);
		ut_assert_int_equal(rc, 0);
		ut_assert_not_null(obj);

		/* write 100 MB */
		for (j = 0; j < 100; j++) {
			/* Using below API purposely, as it provides offset
			   option which can be used to create big files */
			rc = dstore_pwrite(obj, offset, buf_size, bsize,
					   raw_buf);
			ut_assert_int_equal(rc, 0);
			offset += buf_size;
		}

		rc = dstore_obj_close(obj);
		ut_assert_int_equal(rc, 0);

		obj = NULL;
	}
	free(raw_buf);

	/* print filesystm_stats */
	printf("\n After creating objects");
	print_fs_stats(env);
}

/*****************************************************************************/
/* Description: Delete the created objects.
 * Strategy:
 *	Delete the previously created files.
 * Expected behavior:
 *	No errors. Space should be released by Motr
 * Enviroment:
 *	Dstore has objects.
 */
static void test_delete_big_objects(void **state)
{
	int rc, i;
	struct env *env = ENV_FROM_STATE(state);

	for (i = 0; i < NUM_OF_OBJECTS; i++) {
		printf("\n Deleting Object ... %d", i + 1);
		rc = dstore_obj_delete(env->dstore, NULL, &env->oids[i]);
		ut_assert_int_equal(rc, 0);
	}

	/* print filesystm_stats */
	printf("\nAfter deleting objects");
	print_fs_stats(env);
}

/*****************************************************************************/
static int test_group_setup(void **state)
{
	struct env *env;

	env = calloc(sizeof(struct env), 1);
	ut_assert_not_null(env);

	dtlib_get_objids(env->oids);
	env->dstore = dtlib_dstore();
	dtlib_get_clnt_svr(&env->server, &env->client);
	prepare_fs_cmd(env);

	*state = env;

	return SUCCESS;
}

static int test_group_teardown(void **state)
{
	struct env *env = ENV_FROM_STATE(state);

	free(env->server);
	free(env->client);
	free(env);
	*state = NULL;

	return SUCCESS;
}

/*****************************************************************************/
/* Entry point for test group execution. */
int main(int argc, char *argv[])
{
	int rc;

	char *test_logs = "/var/log/cortx/test/ut/ut_dsal.logs";

	printf("Dsal space stats test\n");

	rc = ut_load_config(CONF_FILE);
	if (rc != 0) {
		printf("ut_load_config: err = %d\n", rc);
		goto out;
	}

	test_logs = ut_get_config("dsal", "log_path", test_logs);

	rc = ut_init(test_logs);
	if (rc < 0)
	{
		printf("ut_init: err = %d\n", rc);
		goto out;
	}

	struct test_case test_group[] = {
		ut_test_case(test_creat_big_objects, NULL, NULL),
		ut_test_case(test_delete_big_objects, NULL, NULL),
	};

	int test_count =  sizeof(test_group)/sizeof(test_group[0]);
	int test_failed = 0;

	rc = dtlib_setup_for_multi(argc, argv);
	if (rc) {
		printf("Failed to set up the test group environment");
		goto out;
	}

	test_failed = DSAL_UT_RUN(test_group, test_group_setup, test_group_teardown);
	dtlib_teardown();

	ut_fini();
	ut_summary(test_count, test_failed);

out:
	free(test_logs);
	return rc;
}
