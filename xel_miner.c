/*
* Copyright 2010 Jeff Garzik
* Copyright 2012-2014 pooler
* Copyright 2014 Lucas Jones
* Copyright 2016 sprocket
* Copyright 2016 Evil-Knievel
*
* This program is free software; you can redistribuSte it and/or modify it
* under the terms of the GNU General Public License as published by the Free
* Software Foundation; either version 2 of the License, or (at your option)
* any later version.
*/

#define _GNU_SOURCE

#include <curl/curl.h>
#include <ctype.h>
#include <getopt.h>
#include <jansson.h>
#include <math.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <time.h>
#include <openssl/md5.h>
#include <openssl/rand.h>
#include "miner.h"

#ifdef WIN32
#include <malloc.h>
#include "compat/winansi.h"
#else
#include <mm_malloc.h>
#include <sys/resource.h>
#include <sys/types.h>
#include <sys/syscall.h>
#include <ctype.h> // avoid implicit declaration of tolower
#endif


enum prefs {
	PREF_PROFIT,	// Estimate most profitable based on WCET vs reward
	PREF_WCET,		// Fewest cycles required by work item
	PREF_WORKID,	// Specify work ID
	PREF_COUNT
};

static const char *pref_type[] = {
	"profit",
	"wcet",
	"workid",
	"\0"
};

bool opt_debug = false;
bool opt_debug_epl = false;
bool opt_debug_vm = false;
bool opt_quiet = false;
bool opt_norenice = false;
bool opt_protocol = false;
bool opt_compile = true;
bool use_colors = true;
static int opt_retries = -1;
static int opt_fail_pause = 10;
static int opt_scantime = 60;  // Get New Work From Server At Least Every 60s
bool opt_test_miner = false;
bool opt_test_vm = false;
bool opt_opencl = false;
int opt_opencl_gthreads = 0;
int opt_opencl_vwidth = 0;
int opt_timeout = 30;
int opt_n_threads = 0;
static enum prefs opt_pref = PREF_PROFIT;
char pref_workid[32];

int num_cpus;
bool g_need_work = false;
char g_work_nm[50];
char g_work_id[22];
uint64_t g_cur_work_id;
unsigned char g_pow_target_str[33];
uint32_t g_pow_target[4];

__thread _ALIGN(64) int32_t *vm_m = NULL;
__thread _ALIGN(64) double *vm_f = NULL;
__thread uint32_t *vm_state = NULL;
__thread double vm_param_val[6];
__thread uint32_t vm_param_idx[6];
__thread uint32_t vm_param_num;
__thread bool vm_break;
__thread bool vm_continue;
__thread bool vm_bounty;

bool use_elasticpl_init;
bool use_elasticpl_math;

pthread_mutex_t applog_lock = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t work_lock = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t submit_lock = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t longpoll_lock = PTHREAD_MUTEX_INITIALIZER;

uint8_t *rpc_url = NULL;
uint8_t *rpc_user = NULL;
uint8_t *rpc_pass = NULL;
uint8_t *rpc_userpass = NULL;
uint8_t *passphrase = NULL;
uint8_t publickey[32];
uint8_t *test_filename;

static struct timeval g_miner_start_time;
static struct work g_work = { 0 };
static time_t g_work_time = 0;
static struct work_package *g_work_package;
static int g_work_package_cnt = 0;
static const uint8_t basepoint[32] = { 9 };

struct submit_req *g_submit_req;
int g_submit_req_cnt = 0;
int g_new_block = false;

uint32_t g_bounty_accepted_cnt = 0;
uint32_t g_bounty_rejected_cnt = 0;
uint32_t g_bounty_timeout_cnt = 0;
uint32_t g_bounty_deprecated_cnt = 0;
uint32_t g_bounty_error_cnt = 0;
uint32_t g_pow_accepted_cnt = 0;
uint32_t g_pow_rejected_cnt = 0;
uint32_t g_pow_discarded_cnt = 0;

int work_thr_id;
struct thr_info *thr_info;
struct work_restart *work_restart = NULL;

#ifdef USE_OPENCL
struct opencl_device *gpu;
#endif

extern uint32_t swap32(uint32_t a) {
	return ((a << 24) | ((a << 8) & 0x00FF0000) | ((a >> 8) & 0x0000FF00) | ((a >> 24) & 0x000000FF));
}

static char const usage[] = "\
Usage: " PACKAGE_NAME " [OPTIONS]\n\
Options:\n\
  -c, --config <file>         Use JSON-formated configuration file\n\
  -D, --debug                 Display debug output\n\
      --debug-epl             Display EPL source code\n\
  -h, --help                  Display this help text and exit\n\
  -m, --mining PREF[:ID]      Mining preference for choosing work\n\
                                profit       (Default) Estimate most profitable based on POW Reward / WCET\n\
                                wcet         Fewest cycles required by work item \n\
                                workid		 Specify work ID\n\
      --no-color              Don't display colored output\n\
      --no-compile            Use internal VM Interpreter instead of compiled C code\n\
      --opencl	              Run VM using compiled OpenCL code\n\
      --opencl-gthreads <n>   Max Num of Global Threads (256 - 10240, default: 1024)\n\
      --opencl-vwidth <n>	  Vector width of local work size (1 - 256, default: calculated)\n\
  -o, --url=URL               URL of mining server\n\
  -p, --pass <password>       Password for mining server\n\
  -P, --phrase <passphrase>   Secret Passphrase for Elastic account\n\
      --protocol              Display dump of protocol-level activities\n\
  -q, --quiet                 Display minimal output\n\
  -r, --retries <n>           Number of times to retry if a network call fails\n\
                              (Default: Retry indefinitely)\n\
  -R, --retry-pause <n>       Time to pause between retries (Default: 10 sec)\n\
  -s, --scan-time <n>         Max time to scan work before requesting new work (Default: 60 sec)\n\
      --test-miner <file>     Run the Miner using JSON formatted work in <file>\n\
      --test-vm <file>        Run the Parser / Compiler using the ElasticPL source code in <file>\n\
  -t, --threads <n>           Number of miner threads (Default: Number of CPUs)\n\
  -u, --user <username>       Username for mining server\n\
  -T, --timeout <n>           Timeout for rpc calls (Default: 30 sec)\n\
  -V, --version               Display version information and exit\n\
  -X  --no-renice             Do not lower the priority of miner threads\n\
Options while mining ----------------------------------------------------------\n\n\
   s + <enter>                Display mining summary\n\
   d + <enter>                Toggle Debug mode\n\
   q + <enter>                Toggle Quite mode\n\
";

static char const short_options[] = "c:Dk:hm:o:p:P:qr:R:s:t:T:u:VX";

static struct option const options[] = {
	{ "config",			1, NULL, 'c' },
	{ "debug",			0, NULL, 'D' },
	{ "debug-epl",		0, NULL, 1007 },
	{ "help",			0, NULL, 'h' },
	{ "mining",			1, NULL, 'm' },
	{ "no-color",		0, NULL, 1001 },
	{ "no-compile",		0, NULL, 1002 },
	{ "no-renice",		0, NULL, 'X' },
	{ "opencl",			0, NULL, 1006 },
	{ "opencl-gthreads", 1, NULL, 1008 },
	{ "opencl-vwidth",	1, NULL, 1009 },
	{ "pass",			1, NULL, 'p' },
	{ "phrase",			1, NULL, 'P' },
	{ "protocol",	    0, NULL, 1003 },
	{ "public",			1, NULL, 'k' },
	{ "quiet",			0, NULL, 'q' },
	{ "retries",		1, NULL, 'r' },
	{ "retry-pause",	1, NULL, 'R' },
	{ "scan-time",		1, NULL, 's' },
	{ "test-miner",		1, NULL, 1004 },
	{ "test-vm",		1, NULL, 1005 },
	{ "threads",		1, NULL, 't' },
	{ "timeout",		1, NULL, 'T' },
	{ "url",			1, NULL, 'o' },
	{ "user",			1, NULL, 'u' },
	{ "version",		0, NULL, 'V' },
	{ 0, 0, 0, 0 }
};

static void parse_cmdline(int argc, char *argv[])
{
	int key;

	while (1) {
		key = getopt_long(argc, argv, short_options, options, NULL);

		if (key < 0)
			break;

		parse_arg(key, optarg);
	}
	if (optind < argc) {
		fprintf(stderr, "%s: unsupported non-option argument -- '%s'\n",
			argv[0], argv[optind]);
		show_usage_and_exit(1);
	}
}

static void strhide(char *s)
{
	if (*s) *s++ = 'x';
	while (*s) *s++ = '\0';
}

void parse_arg(int key, char *arg)
{
	char *p, *ap, *nm;
	int v, i;

	switch (key) {
	case 'D':
		opt_debug = true;
		break;
	case 'h':
		show_usage_and_exit(0);
	case 'm':
		for (i = 0; i < PREF_COUNT; i++) {
			v = (int)strlen(pref_type[i]);
			if (!strncmp(arg, pref_type[i], v)) {
				opt_pref = (enum prefs) i;
				break;
			}
		}
		if (i == PREF_COUNT) {
			applog(LOG_ERR, "Unknown mining preference '%s'", arg);
			show_usage_and_exit(1);
		}
		if (opt_pref == PREF_WORKID) {
			p = strchr(arg, ':');
			if (!p) {
				fprintf(stderr, "Invalid MiningPreference:ID pair -- '%s'\n", arg);
				show_usage_and_exit(1);
			}
			if (p)
				strcpy(pref_workid, ++p);
		}
		break;
	case 'o':
		ap = strstr(arg, "://");
		ap = ap ? ap + 3 : arg;
		nm = strstr(arg, "/nxt");
		if (ap != arg) {
			if (strncasecmp(arg, "http://", 7) && strncasecmp(arg, "https://", 8)) {
				fprintf(stderr, "ERROR: Invalid protocol -- '%s'\n", arg);
				show_usage_and_exit(1);
			}
			free(rpc_url);
			rpc_url = strdup(arg);
		}
		else {
			if (*ap == '\0' || *ap == '/') {
				fprintf(stderr, "ERROR: Invalid URL -- '%s'\n", arg);
				show_usage_and_exit(1);
			}
			free(rpc_url);
			rpc_url = (char*)malloc(strlen(ap) + 8);
			sprintf(rpc_url, "http://%s", ap);
		}

		if (!nm) {
			rpc_url = realloc(rpc_url, strlen(rpc_url) + 5);
			sprintf(rpc_url, "%s/nxt", rpc_url);
		}

		break;
	case 'p':
		free(rpc_pass);
		rpc_pass = strdup(arg);
		strhide(arg);
		break;
	case 'P':
		passphrase = strdup(arg);
		strhide(arg);

		// Generate publickey From Secret Phrase
		char* hash_sha256 = (char*)malloc(32 * sizeof(char));
		sha256(passphrase, strlen(passphrase), hash_sha256);

		// Clamp
		hash_sha256[0] &= 248;
		hash_sha256[31] &= 127;
		hash_sha256[31] |= 64;

		// Do "donna"
		curve25519_donna(publickey, hash_sha256, basepoint);

		printf("Public Key: ");
		for (i = 0; i < 32; i++)
			printf("%02X", publickey[i]);
		printf("\n");

		free(hash_sha256);

		break;
	case 'q':
		opt_quiet = true;
		break;
	case 'r':
		v = atoi(arg);
		if (v < -1 || v > 9999)
			show_usage_and_exit(1);
		opt_retries = v;
		break;
	case 'R':
		v = atoi(arg);
		if (v < 1 || v > 9999)
			show_usage_and_exit(1);
		opt_fail_pause = v;
		break;
	case 's':
		v = atoi(arg);
		if (v < 1 || v > 9999)
			show_usage_and_exit(1);
		opt_scantime = v;
		break;
	case 't':
		v = atoi(arg);
		if (v < 1 || v > 9999)
			show_usage_and_exit(1);
		opt_n_threads = v;
		break;
	case 'T':
		v = atoi(arg);
		if (v < 1 || v > 9999)
			show_usage_and_exit(1);
		opt_timeout = v;
		break;
	case 'u':
		free(rpc_user);
		rpc_user = strdup(arg);
		break;
	case 'V':
		show_version_and_exit();
	case 'X':
		opt_norenice = true;
		break;
	case 1001:
		use_colors = false;
		break;
	case 1002:
		opt_compile = false;
		break;
	case 1003:
		opt_protocol = true;
		break;
	case 1004:
		if (!arg)
			show_usage_and_exit(1);
		test_filename = malloc(strlen(arg) + 1);
		strcpy(test_filename, arg);
		opt_test_miner = true;
		break;
	case 1005:
		if (!arg)
			show_usage_and_exit(1);
		test_filename = malloc(strlen(arg) + 1);
		strcpy(test_filename, arg);
		opt_test_vm = true;
		opt_debug = true;
		opt_debug_epl = true;
		opt_debug_vm = true;
		break;
	case 1006:
		opt_opencl = true;
		opt_compile = false;
		break;
	case 1007:
		opt_debug = true;
		opt_debug_epl = true;
		break;
	case 1008:
		v = atoi(arg);
		if (v < 256 || v > 10240)
			show_usage_and_exit(1);
		opt_opencl_gthreads = v;
		break;
	case 1009:
		v = atoi(arg);
		if (v < 1 || v > 256)
			show_usage_and_exit(1);
		else
			opt_opencl_vwidth = v;
		break;
	default:
		show_usage_and_exit(1);
	}
}

static void show_usage_and_exit(int status)
{
	if (status)
		fprintf(stderr, "Try `" PACKAGE_NAME " -h' for more information.\n");
	else
		printf(usage);
	exit(status);
}

static void show_version_and_exit(void)
{
	printf("\nBuilt on " __DATE__
#ifdef _MSC_VER
		" with VC++ 2015\n\n");
#elif defined(__GNUC__)
		" with GCC");
	printf(" %d.%d.%d\n\n", __GNUC__, __GNUC_MINOR__, __GNUC_PATCHLEVEL__);
#endif

	// Dependencies Versions
	printf("curl:     %s\n", curl_version());
#ifdef JANSSON_VERSION
	printf("jansson:  v%s\n", JANSSON_VERSION);
#endif
#ifdef PTW32_VERSION
	printf("pthreads: v%d.%d.%d.%d\n", PTW32_VERSION);
#endif
#ifdef OPENSSL_VERSION_TEXT
	printf("openssl:  %s\n", OPENSSL_VERSION_TEXT);
#endif
	exit(0);
}

static void thread_low_priority() {
#if defined (__unix__)  && defined (__MACH__)
	uint64_t tid = syscall(__NR_gettid);
	int rc = setpriority(PRIO_PROCESS, tid, 7);
	if (rc) {
		applog(LOG_ERR, "Error(%d): failed setting thread priority (you are safe to ignore this message)", rc);
	}
	else {
		if (opt_debug)
			applog(LOG_DEBUG, "setting low thread priority for thread id %ld", tid);
	}
#else
	// TODO: implement for non posix platforms
	// Possibly: Use SetThreadPriority() on windows
#endif
}

static bool load_test_file(char *buf) {
	int i, fsize, len, bytes;
	char *ptr;
	FILE *fp;

	fp = fopen(test_filename, "r");

	if (!fp) {
		applog(LOG_ERR, "ERROR: Unable to open test file: '%s'\n", test_filename);
		return false;
	}

	if (0 != fseek(fp, 0, SEEK_END)) {
		applog(LOG_ERR, "ERROR: Unable to determine size of test file: '%s'\n", test_filename);
		fclose(fp);
		return false;
	}

	fsize = ftell(fp);

	if (fsize > MAX_SOURCE_SIZE - 4) {
		applog(LOG_ERR, "ERROR: Test file exceeds max size (%d bytes): %zu bytes\n", MAX_SOURCE_SIZE, fsize);
		fclose(fp);
		return false;
	}

	rewind(fp);
	ptr = buf;
	len = fsize;
	while (len > 0) {
		bytes = fread(ptr, 1, ((len > 1024) ? 1024 : len), fp);
		if (bytes == 0) {
			if (feof(fp))
				break;
			else
				applog(LOG_ERR, "ERROR: Unable to read test file: '%s'\n", test_filename);
			return false;
		}
		len -= bytes;
		ptr += bytes;
		ptr[0] = 0;
	}
	fclose(fp);

	for (i = 0; i < fsize; i++)
		buf[i] = tolower(buf[i]);

	return true;
}

static void *test_vm_thread(void *userdata) {
	struct thr_info *mythr = (struct thr_info *) userdata;
	int thr_id = mythr->id;
	char test_code[MAX_SOURCE_SIZE];
	struct instance *inst = NULL;
	int i, rc;

	// Initialize Global Variables
	vm_state = calloc(4, sizeof(uint32_t));
	vm_m = calloc(VM_MEMORY_SIZE, sizeof(int32_t));
	vm_f = calloc(VM_FLOAT_SIZE, sizeof(double));

	if (!vm_m || !vm_f || !vm_state) {
		applog(LOG_ERR, "%s: Unable to allocate VM memory", mythr->name);
		exit(EXIT_FAILURE);
	}

	applog(LOG_DEBUG, "DEBUG: Loading Test File");
	if (!load_test_file(test_code))
		exit(EXIT_FAILURE);

	// Convert The Source Code Into ElasticPL AST
	if (!create_epl_vm(test_code)) {
		applog(LOG_ERR, "ERROR: Exiting 'test_vm'");
		exit(EXIT_FAILURE);
	}

	// Calculate WCET
	if (!calc_wcet()) {
		applog(LOG_ERR, "ERROR: Exiting 'test_vm'");
		exit(EXIT_FAILURE);
	}

	if (opt_compile) {

		// Convert The ElasticPL Source Into A C Program Library
		if (!compile_and_link("test")) {
			applog(LOG_ERR, "ERROR: Exiting 'test_vm'");
			exit(EXIT_FAILURE);
		}

		// Link To The C Program Library
		if (inst)
			free_compiler(inst);
		inst = calloc(1, sizeof(struct instance));
		create_instance(inst, "test");
		inst->initialize(vm_m, vm_f, vm_state);

		// Execute The VM Logic
		rc = inst->execute();

		free_compiler(inst);
	}
	else if (opt_opencl) {

		// Convert The ElasticPL Source Into OpenCL
		if (!create_opencl_source("test")) {
			applog(LOG_ERR, "ERROR: Unable to convert 'source' to OpenC.  Exiting 'test_vm'\n");
			exit(EXIT_FAILURE);
		}
	}
	else {
		// Execute The VM Logic
		rc = interpret_ast(true);
	}

	applog(LOG_DEBUG, "DEBUG: Bounty Found: %s", rc ? "true" : "false");

	for (i = 0; i < 4; i++)
		applog(LOG_DEBUG, "DEBUG: vm_state[%d]: %11d, Hex: %08X", i, vm_state[i], vm_state[i]);

	// Dump Non-Zero VM Values
	printf("\n\t   VM Integers:\n");
	for (i = 0; i < VM_MEMORY_SIZE; i++) {
		if (vm_m[i])
			printf("\t\t  vm_m[%d] = %d\n", i, vm_m[i]);
	}
	printf("\n\t   VM Floats:\n");
	for (i = 0; i < VM_FLOAT_SIZE; i++) {
		if (vm_f[i] != 0.0)
			printf("\t\t  vm_f[%d] = %f\n", i, vm_f[i]);
	}
	printf("\n");

	applog(LOG_NOTICE, "DEBUG: Compiler Test Complete");
	applog(LOG_WARNING, "Exiting " PACKAGE_NAME);

	if (inst) free(inst);
	if (vm_m) free(vm_m);
	if (vm_f) free(vm_f);
	if (vm_state) free(vm_state);

	tq_freeze(mythr->q);

	return NULL;
}

static bool get_vm_input(struct work *work) {
	int i;
	char msg[80];
	char hash[16];
	uint32_t *msg32 = (uint32_t *)msg;
	uint32_t *hash32 = (uint32_t *)hash;
	uint32_t *workid32 = (uint32_t *)&work->work_id;
	uint32_t *blockid32 = (uint32_t *)&work->block_id;

	memcpy(&msg[0], work->multiplicator, 32);
	memcpy(&msg[32], publickey, 32);
	memcpy(&msg[64], &workid32[1], 4);	// Swap First 4 Bytes Of Long
	memcpy(&msg[68], &workid32[0], 4);	// With Second 4 Bytes Of Long
	memcpy(&msg[72], &blockid32[1], 4);
	memcpy(&msg[76], &blockid32[0], 4);
	msg32[16] = swap32(msg32[16]);
	msg32[17] = swap32(msg32[17]);
	msg32[18] = swap32(msg32[18]);
	msg32[19] = swap32(msg32[19]);

	// Hash The Inputs
	MD5(msg, 80, hash);

	// Randomize The Inputs
	for (i = 0; i < 12; i++) {
		work->vm_input[i] = swap32(hash32[i % 4]);
		if (i > 4)
			work->vm_input[i] = work->vm_input[i] ^ work->vm_input[i - 3];
	}

	return true;
}

static bool get_opencl_base_data(struct work *work, uint32_t *vm_input) {
	char msg[80];
	uint32_t *msg32 = (uint32_t *)msg;
	uint32_t *workid32 = (uint32_t *)&work->work_id;
	uint32_t *blockid32 = (uint32_t *)&work->block_id;

	memcpy(&vm_input[0], work->multiplicator, 32);
	memcpy(&vm_input[8], publickey, 32);
	memcpy(&vm_input[16], &workid32[1], 4);	// Swap First 4 Bytes Of Long
	memcpy(&vm_input[17], &workid32[0], 4);	// With Second 4 Bytes Of Long
	memcpy(&vm_input[18], &blockid32[1], 4);
	memcpy(&vm_input[19], &blockid32[0], 4);
	vm_input[16] = swap32(vm_input[16]);
	vm_input[17] = swap32(vm_input[17]);
	vm_input[18] = swap32(vm_input[18]);
	vm_input[19] = swap32(vm_input[19]);

	// Target
	vm_input[20] = g_pow_target[0];
	vm_input[21] = g_pow_target[1];
	vm_input[22] = g_pow_target[2];
	vm_input[23] = g_pow_target[3];
	//vm_input[20] = work->pow_target[0];
	//vm_input[21] = work->pow_target[1];
	//vm_input[22] = work->pow_target[2];
	//vm_input[23] = work->pow_target[3];

	return true;
}

static int execute_vm(int thr_id, struct work *work, struct instance *inst, long *hashes_done, char* hash, bool new_work) {
	int i, rc;
	time_t t_start = time(NULL);
	char msg[64];

	uint32_t *msg32 = (uint32_t *)msg;
	uint32_t *mult32 = (uint32_t *)work->multiplicator;
	uint32_t *hash32 = (uint32_t *)hash;

	mult32[0] = thr_id;				// Ensures Each Thread Is Unique
	mult32[1] = 0;					// Value Will Be Incremented On Each Pass
	mult32[6] = genrand_int32();	// Random Number
	mult32[7] = genrand_int32();	// Random Number

	while (1) {
		// Check If New Work Is Available
		if (work_restart[thr_id].restart)
			return 0;

		// Increment Multiplicator
		mult32[2] += 1;

		// Get Values For VM Inputs
		get_vm_input(work);

		// Reset VM Memory / State
		memcpy(vm_m, work->vm_input, VM_INPUTS * sizeof(int));
		memset(vm_state, 0, 4 * sizeof(int));

		// Execute The VM Logic
		if (opt_compile)
			rc = inst->execute();
		else
			rc = interpret_ast(new_work);

		// Hee, we have found a bounty, exit immediately
		if (rc == 1)
			return rc;

		// Check For POW Result
		memcpy(&msg[0], &vm_state[0], 16);
		msg32[0] = swap32(msg32[0]);
		msg32[1] = swap32(msg32[1]);
		msg32[2] = swap32(msg32[2]);
		msg32[3] = swap32(msg32[3]);

		for (i = 0; i < VM_INPUTS; i++)
			msg32[i + 4] = swap32(work->vm_input[i]);

		MD5(msg, 64, hash);

		// Check For POW Solution
		for (i = 0; i < 4; i++) {

			hash32[i] = swap32(hash32[i]);

			if (hash32[i] <= work->pow_target[i]) {
				return 2;	// POW Solution Found
			}
			else {
				if (hash32[i] > work->pow_target[i])
					break;
			}
		}

		(*hashes_done)++;

		// Only Run For 1s Before Returning To Miner Thread
		if ((time(NULL) - t_start) >= 1)
			break;
	}
	return 0;
}


static void update_pending_cnt(uint64_t work_id, bool add) {
	int i;

	for (i = 0; i < g_work_package_cnt; i++) {
		if (work_id == g_work_package[i].work_id) {

			if (add)
				g_work_package[i].pending_bty_cnt++;
			else
				g_work_package[i].pending_bty_cnt--;

			break;
		}
	}
}

static bool add_work_package(struct work_package *work_package) {
	g_work_package = realloc(g_work_package, sizeof(struct work_package) * (g_work_package_cnt + 1));
	if (!g_work_package) {
		applog(LOG_ERR, "ERROR: Unable to allocate memory for work_package");
		return false;
	}

	memcpy(&g_work_package[g_work_package_cnt], work_package, sizeof(struct work_package));
	g_work_package_cnt++;

	return true;
}

static bool get_work(CURL *curl) {
	int err, rc;
	json_t *val;
	struct work work;
	struct timeval tv_start, tv_end, diff;

	memset(g_work_id, 0, sizeof(g_work_id));
	memset(g_work_nm, 0, sizeof(g_work_nm));
	memset(g_pow_target_str, 0, sizeof(g_pow_target_str));

	gettimeofday(&tv_start, NULL);
	if (!opt_test_miner) {
		val = json_rpc_call(curl, rpc_url, rpc_userpass, "requestType=getMineableWork&n=1", &err);
	}
	else {
		json_error_t err_val;
		val = JSON_LOAD_FILE(test_filename, &err_val);
		if (!json_is_object(val)) {
			if (err_val.line < 0)
				applog(LOG_ERR, "%s\n", err_val.text);
			else
				applog(LOG_ERR, "%s:%d: %s\n", test_filename, err_val.line, err_val.text);
			return false;
		}

		char *str = json_dumps(val, JSON_INDENT(3));
		applog(LOG_DEBUG, "DEBUG: JSON Response -\n%s", str);
		free(str);
	}

	if (!val) {
		applog(LOG_ERR, "ERROR: 'json_rpc_call' failed...retrying in %d seconds", opt_fail_pause);
		sleep(opt_fail_pause);
		return false;
	}

	gettimeofday(&tv_end, NULL);
	if (opt_protocol) {
		timeval_subtract(&diff, &tv_end, &tv_start);
		applog(LOG_DEBUG, "DEBUG: Time to get work: %.2f ms", (1000.0 * diff.tv_sec) + (0.001 * diff.tv_usec));
	}

	rc = work_decode(val, &work);
	json_decref(val);

	gettimeofday(&tv_start, NULL);
	if (opt_protocol) {
		timeval_subtract(&diff, &tv_start, &tv_end);
		applog(LOG_DEBUG, "DEBUG: Time to decode work: %.2f ms", (1000.0 * diff.tv_sec) + (0.001 * diff.tv_usec));
	}

	// Update Global Variables With Latest Work
	pthread_mutex_lock(&work_lock);
	g_work_time = time(NULL);

	if (rc > 0) {
		strncpy(g_work_id, work.work_str, 21);
		strncpy(g_work_nm, work.work_nm, 49);
		sprintf(g_pow_target_str, "%08X%08X%08X%08X", work.pow_target[0], work.pow_target[1], work.pow_target[2], work.pow_target[3]);
		memcpy(g_pow_target, work.pow_target, 4 * sizeof(uint32_t));
		memcpy(&g_work, &work, sizeof(struct work));

		// Restart Miner Threads If Work Package Changes
		if (work.work_id != g_cur_work_id) {
			applog(LOG_NOTICE, "Switching to work_id: %s (target: %s)", work.work_str, g_pow_target_str);
			restart_threads();
		}

		g_cur_work_id = work.work_id;
	}
	else {
		g_cur_work_id = 0;
		memset(&g_work, 0, sizeof(struct work));
		restart_threads();
	}

	pthread_mutex_unlock(&work_lock);

	if (!rc)
		return false;

	return true;
}

static double calc_diff(uint32_t *target) {
	double diff_1 = 0x0000FFFFFFFFFFFF;
	double diff;

	uint64_t tgt = 0x0ull;
	uint32_t *tgt32 = (uint32_t *)&tgt;

	tgt32[0] = target[1];
	tgt32[1] = target[0];

	diff = (double)tgt;

	if (!diff)
		return diff_1;

	return (double)(diff_1 / diff);
}

static int work_decode(const json_t *val, struct work *work) {
	int i, j, rc, num_pkg, best_pkg, bty_rcvd, work_pkg_id;
	uint64_t work_id;
	uint32_t best_wcet = 0xFFFFFFFF, pow_tgt[4];
	double difficulty, best_profit = 0, profit = 0;
	char *tgt = NULL, *src = NULL, *str = NULL, *best_src = NULL, *best_tgt = NULL, *elastic_src = NULL;
	json_t *wrk = NULL, *pkg = NULL;

	memset(work, 0, sizeof(struct work));

	if (opt_protocol) {
		str = json_dumps(val, JSON_INDENT(3));
		applog(LOG_DEBUG, "DEBUG: JSON Response -\n%s", str);
		free(str);
	}

	wrk = json_object_get(val, "work_packages");

	if (!wrk) {
		applog(LOG_ERR, "Invalid JSON response to getwork request");
		return 0;
	}

	// Check If Any Work Packages Are Available
	num_pkg = json_array_size(wrk);
	if (num_pkg == 0) {
		applog(LOG_INFO, "No work available...retrying in %ds", opt_scantime);
		return -1;
	}

	best_pkg = -1;

	for (i = 0; i<num_pkg; i++) {
		pkg = json_array_get(wrk, i);
		tgt = (char *)json_string_value(json_object_get(pkg, "target"));
		str = (char *)json_string_value(json_object_get(pkg, "work_id"));
		if (!str || !tgt) {
			applog(LOG_ERR, "Unable to parse work package");
			return 0;
		}
		work_id = strtoull(str, NULL, 10);
		applog(LOG_DEBUG, "DEBUG: Checking work_id: %s", str);

		// Check If Work Package Exists
		work_pkg_id = -1;
		for (j = 0; j < g_work_package_cnt; j++) {
			if (work_id == g_work_package[j].work_id) {
				work_pkg_id = j;
				break;
			}
		}

		// Add New Work Packages
		if (work_pkg_id < 0) {
			struct work_package work_package;
			memset(&work_package, 0, sizeof(struct work_package));

			work_package.work_id = work_id;
			strncpy(work_package.work_str, str, 21);
			str = (char *)json_string_value(json_object_get(pkg, "block_id"));
			work_package.block_id = strtoull(str, NULL, 10);
			str = (char *)json_string_value(json_object_get(pkg, "title"));
			strncpy(work_package.work_nm, str, 49);
			work_package.bounty_limit = (uint32_t)json_integer_value(json_object_get(pkg, "bounty_limit"));
			work_package.bty_reward = (uint64_t)json_number_value(json_object_get(pkg, "xel_per_bounty"));
			work_package.pow_reward = (uint64_t)json_number_value(json_object_get(pkg, "xel_per_pow"));
			work_package.pending_bty_cnt = 0;
			work_package.blacklisted = false;

			str = (char *)json_string_value(json_object_get(pkg, "source"));

			// Extract The ElasticPL Source Code
			if (!str || strlen(str) > MAX_SOURCE_SIZE || strlen(str) == 0) {
				work_package.blacklisted = true;
				applog(LOG_ERR, "ERROR: Invalid 'source' for work_id: %s", work_package.work_str);
				continue;
			}

			elastic_src = malloc(MAX_SOURCE_SIZE);
			if (!elastic_src) {
				applog(LOG_ERR, "ERROR: Unable to allocate memory for ElasticPL Source");
				return 0;
			}

			rc = ascii85dec(elastic_src, MAX_SOURCE_SIZE, str);
			if (!rc) {
				work_package.blacklisted = true;
				applog(LOG_ERR, "ERROR: Unable to decode 'source' for work_id: %s\n\n%s\n", work_package.work_str, str);
				free(elastic_src);
				return 0;
			}

			applog(LOG_DEBUG, "DEBUG: Running ElasticPL Parser");

			if (opt_debug_epl)
				applog(LOG_DEBUG, "DEBUG: ElasticPL Source Code -\n%s", elastic_src);

			// Convert ElasticPL Into AST
			if (!create_epl_vm(elastic_src)) {
				work_package.blacklisted = true;
				applog(LOG_ERR, "ERROR: Unable to convert 'source' to AST for work_id: %s\n\n%s\n", work_package.work_str, str);
				free(elastic_src);
				return 0;
			}

			free(elastic_src);

			// Calculate WCET
			work_package.WCET = calc_wcet();
			if (!work_package.WCET) {
				work_package.blacklisted = true;
				applog(LOG_ERR, "ERROR: Unable to calculate WCET for work_id: %s\n\n%s\n", work_package.work_str, str);
				return 0;
			}

			// Convert The ElasticPL Source Into A C Program Library
			if (opt_compile) {
				if (!compile_and_link(work_package.work_str)) {
					work_package.blacklisted = true;
					applog(LOG_ERR, "ERROR: Unable to convert 'source' to C for work_id: %s\n\n%s\n", work_package.work_str, str);
					return 0;
				}
			}
			else if (opt_opencl) {
				if (!create_opencl_source(work_package.work_str)) {
					work_package.blacklisted = true;
					applog(LOG_ERR, "ERROR: Unable to convert 'source' to OpenCL for work_id: %s\n\n%s\n", work_package.work_str, str);
					return 0;
				}
			}

			applog(LOG_DEBUG, "DEBUG: Adding work package to list, work_id: %s", work_package.work_str);

			add_work_package(&work_package);
			work_pkg_id = g_work_package_cnt - 1;
		}

		// Check If Work Has Been Blacklisted
		if (g_work_package[work_pkg_id].blacklisted) {
			applog(LOG_DEBUG, "DEBUG: Skipping blacklisted work_id: %s", g_work_package[work_pkg_id].work_str);
			continue;
		}

		// Check If Work Has Available Bounties
		bty_rcvd = (int)json_integer_value(json_object_get(pkg, "received_bounties"));
		if (g_work_package[work_pkg_id].bounty_limit <= (bty_rcvd + g_work_package[work_pkg_id].pending_bty_cnt)) {
			applog(LOG_DEBUG, "DEBUG: Skipping work_id: %s - No Bounties Left", g_work_package[work_pkg_id].work_str);
			continue;
		}

		// Get Updated Target For The Job
		rc = hex2ints(pow_tgt, 4, tgt, strlen(tgt));
		if (!rc) {
			applog(LOG_ERR, "Invalid Target in JSON response for work_id: %s", g_work_package[work_pkg_id].work_str);
			return 0;
		}

		difficulty = calc_diff(pow_tgt);
		profit = ((double)g_work_package[work_pkg_id].pow_reward / ((double)g_work_package[work_pkg_id].WCET * difficulty));

		// Select Best Work Package
		if (opt_pref == PREF_WCET && (g_work_package[work_pkg_id].WCET < best_wcet)) {
			best_pkg = work_pkg_id;
			best_src = (char *)json_string_value(json_object_get(pkg, "source"));
			best_tgt = tgt;
			best_wcet = g_work_package[work_pkg_id].WCET;
		}

		//
		// TODO:  Add Bounty Reward Profitability Check
		//

		else if ((opt_pref == PREF_PROFIT) && (profit > best_profit)) {
			best_pkg = work_pkg_id;
			best_src = (char *)json_string_value(json_object_get(pkg, "source"));
			best_tgt = tgt;
			best_profit = profit;
		}
		else if (opt_pref == PREF_WORKID && (!strcmp(g_work_package[work_pkg_id].work_str, pref_workid))) {
			best_pkg = work_pkg_id;
			best_src = (char *)json_string_value(json_object_get(pkg, "source"));
			best_tgt = tgt;
			break;
		}
	}

	// If No Work Matched Current Preference Switch To Profit Mode
	if (best_pkg < 0) {
		opt_pref = PREF_PROFIT;
		applog(LOG_INFO, "No work available that matches preference...retrying in %ds", opt_scantime);
		return -1;
	}

	// If Running VM Interpreter Instead Of Compiled VM
	if (!opt_compile && !opt_opencl && (g_work_package[best_pkg].work_id != g_cur_work_id)) {
		elastic_src = malloc(MAX_SOURCE_SIZE);
		if (!elastic_src) {
			applog(LOG_ERR, "ERROR: Unable to allocate memory for ElasticPL Source");
			return 0;
		}

		rc = ascii85dec(elastic_src, MAX_SOURCE_SIZE, best_src);
		if (!rc) {
			g_work_package[best_pkg].blacklisted = true;
			applog(LOG_ERR, "ERROR: Unable to decode 'source' for work_id: %s\n\n%s\n", g_work_package[best_pkg].work_str, str);
			free(elastic_src);
			return 0;
		}

		applog(LOG_DEBUG, "DEBUG: Running ElasticPL Parser");

		if (opt_debug_epl)
			applog(LOG_DEBUG, "DEBUG: ElasticPL Source Code -\n%s", elastic_src);

		// Convert ElasticPL Into AST
		if (!create_epl_vm(elastic_src)) {
			g_work_package[best_pkg].blacklisted = true;
			applog(LOG_ERR, "ERROR: Unable to convert 'source' to AST for work_id: %s\n\n%s\n", g_work_package[best_pkg].work_str, str);
			free(elastic_src);
			return 0;
		}

		free(elastic_src);
	}

	// Copy Work Package Details To Work
	work->block_id = g_work_package[best_pkg].block_id;
	work->work_id = g_work_package[best_pkg].work_id;
	strncpy(work->work_str, g_work_package[best_pkg].work_str, 21);
	strncpy(work->work_nm, g_work_package[best_pkg].work_nm, 49);

	// Convert Hex Target To Int Array
	rc = hex2ints(work->pow_target, 4, tgt, strlen(tgt));
	if (!rc) {
		applog(LOG_ERR, "Invalid Target in JSON response for work_id: %s", work->work_str);
		return 0;
	}

	return 1;
}

static bool submit_work(CURL *curl, struct submit_req *req) {
	int err;
	json_t *val = NULL;
	struct timeval tv_start, tv_end, diff;
	char data[1000];
	char *url, *err_desc, *accepted;

	url = calloc(1, strlen(rpc_url) + 50);
	if (!url) {
		applog(LOG_ERR, "ERROR: Unable to allocate memory for submit work url");
		return false;
	}

	if (req->req_type == SUBMIT_BTY_ANN) {
		sprintf(url, "%s?requestType=bountyAnnouncement", rpc_url);
		sprintf(data, "deadline=3&feeNQT=0&amountNQT=5000&work_id=%s&hash_announcement=%s&secretPhrase=%s", req->work_str, req->hash, passphrase);
	}
	else if (req->req_type == SUBMIT_BTY_CONF) {
		sprintf(url, "%s?requestType=getApprovedBounties", rpc_url);
		sprintf(data, "work_id=%s&hash_announcement=%s&secretPhrase=%s", req->work_str, req->hash, passphrase);
	}
	else if (req->req_type == SUBMIT_BOUNTY) {
		sprintf(url, "%s?requestType=createPoX", rpc_url);
		sprintf(data, "deadline=3&feeNQT=0&amountNQT=0&work_id=%s&multiplicator=%s&is_pow=false&secretPhrase=%s", req->work_str, req->mult, passphrase);
	}
	else if (req->req_type == SUBMIT_POW) {
		sprintf(url, "%s?requestType=createPoX", rpc_url);
		sprintf(data, "deadline=3&feeNQT=0&amountNQT=0&work_id=%s&multiplicator=%s&is_pow=true&secretPhrase=%s", req->work_str, req->mult, passphrase);
	}
	else {
		applog(LOG_ERR, "ERROR: Unknown request type");
		req->req_type = SUBMIT_COMPLETE;
		free(url);
		return true;
	}

	gettimeofday(&tv_start, NULL);
	val = json_rpc_call(curl, url, rpc_userpass, data, &err);

	if (!val)
		return false;

	gettimeofday(&tv_end, NULL);
	if (opt_protocol) {
		timeval_subtract(&diff, &tv_end, &tv_start);
		applog(LOG_DEBUG, "DEBUG: Time to submit solution: %.2f ms", (1000.0 * diff.tv_sec) + (0.001 * diff.tv_usec));
	}

	// Mask Passphrase
	data[strlen(data) - strlen(passphrase)] = 0;

	applog(LOG_DEBUG, "DEBUG: Submit request - %s %s", url, data);

	accepted = (char *)json_string_value(json_object_get(val, "approved"));
	err_desc = (char *)json_string_value(json_object_get(val, "errorDescription"));

	if (err_desc)
		applog(LOG_DEBUG, "DEBUG: Submit response error - %s", err_desc);

	if (req->req_type == SUBMIT_BTY_ANN) {
		if (err_desc) {
			if (strstr(err_desc, "Duplicate unconfirmed transaction:")) {
				applog(LOG_NOTICE, "%s: %s***** Bounty Discarded *****", thr_info[req->thr_id].name, CL_YLW);
				applog(LOG_DEBUG, "Work ID: %s - No more bounty announcement slots available", req->work_str, err_desc);
				//				req->delay_tm = time(NULL) + 30;  // Retry In 30s
				req->req_type = SUBMIT_COMPLETE;
			}
			else {
				applog(LOG_NOTICE, "%s: %s***** Bounty Rejected *****", thr_info[req->thr_id].name, CL_RED);
				applog(LOG_DEBUG, "Reason: %s", err_desc);
				//				req->delay_tm = time(NULL) + 30;  // Retry In 30s
				req->req_type = SUBMIT_COMPLETE;
				g_bounty_error_cnt++;
			}
		}
		else {
			req->req_type = SUBMIT_BTY_CONF;
		}
	}
	else if (req->req_type == SUBMIT_BTY_CONF) {
		if (accepted && !strcmp(accepted, "true")) {
			req->req_type = SUBMIT_BOUNTY;
		}
		else if (accepted && !strcmp(accepted, "deprecated")) {
			applog(LOG_NOTICE, "%s: %s***** Bounty Rejected - Deprecated *****", thr_info[req->thr_id].name, CL_RED);
			g_bounty_deprecated_cnt++;
			req->req_type = SUBMIT_COMPLETE;
		}
		else if (req->retries++ > 20) {		// Timeout After 10 Min
			applog(LOG_NOTICE, "%s: %s***** Bounty Timed Out *****", thr_info[req->thr_id].name, CL_RED);
			g_bounty_timeout_cnt++;
			req->req_type = SUBMIT_COMPLETE;
		}
		else {
			req->delay_tm = time(NULL) + 30;  // Retry In 30s
			applog(LOG_DEBUG, "DEBUG: Retry confirmation in 30s");
		}
	}
	else if (req->req_type == SUBMIT_BOUNTY) {
		if (err_desc) {
			if (strstr(err_desc, "Duplicate unconfirmed transaction:")) {
				applog(LOG_NOTICE, "%s: %s***** Bounty Discarded *****", thr_info[req->thr_id].name, CL_YLW);
				applog(LOG_DEBUG, "Work ID: %s - Work is already closed, you missed the reveal period", req->work_str, err_desc);
			}
			else {
				applog(LOG_NOTICE, "%s: %s***** Bounty Rejected! *****", thr_info[req->thr_id].name, CL_RED);
				g_bounty_rejected_cnt++;
			}
		}
		else {
			applog(LOG_NOTICE, "%s: %s***** Bounty Claimed! *****", thr_info[req->thr_id].name, CL_GRN);
			g_bounty_accepted_cnt++;
		}
		req->req_type = SUBMIT_COMPLETE;
	}
	else if (req->req_type == SUBMIT_POW) {
		if (err_desc) {
			if (strstr(err_desc, "Duplicate unconfirmed transaction:")) {
				applog(LOG_NOTICE, "%s: %s***** POW Discarded! *****", thr_info[req->thr_id].name, CL_YLW);
				applog(LOG_INFO, "Work ID: %s -%s", req->work_str, err_desc + 34);
				g_pow_discarded_cnt++;
			}
			else {
				applog(LOG_NOTICE, "%s: %s***** POW Rejected! *****", thr_info[req->thr_id].name, CL_RED);
				applog(LOG_INFO, "Work ID: %s, Reason: %s", req->work_str, err_desc);
				g_pow_rejected_cnt++;
			}
		}
		else {
			applog(LOG_NOTICE, "%s: %s***** POW Accepted! *****", thr_info[req->thr_id].name, CL_CYN);
			g_pow_accepted_cnt++;
		}
		req->req_type = SUBMIT_COMPLETE;
	}

	json_decref(val);
	free(url);

	return true;
}

static void *cpu_miner_thread(void *userdata) {
	struct thr_info *mythr = (struct thr_info *) userdata;
	int thr_id = mythr->id;
	struct work work;
	struct workio_cmd *wc = NULL;
	char s[16];
	long hashes_done;
	struct timeval tv_start, tv_end, diff;
	int rc = 0;
	unsigned char msg[41];
	uint32_t *msg32 = (uint32_t *)msg;
	uint32_t *workid32;
	double eval_rate;
	struct instance *inst = NULL;
	char hash[32];
	uint32_t *hash32 = (uint32_t *)hash;
	bool new_work = true;

	// Set lower priority
	if (!opt_norenice)
		thread_low_priority();

	// Initialize Global Variables
	vm_state = calloc(4, sizeof(uint32_t));
	vm_m = calloc(VM_MEMORY_SIZE, sizeof(int32_t));
	vm_f = calloc(VM_FLOAT_SIZE, sizeof(double));

	if (!vm_m || !vm_f || !vm_state) {
		applog(LOG_ERR, "CPU%d: Unable to allocate VM memory", thr_id);
		goto out;
	}

	hashes_done = 0;
	memset(&work, 0, sizeof(work));
	gettimeofday((struct timeval *) &tv_start, NULL);

	while (1) {

		// No Work Available
		if (!g_work.work_id) {
			if (work.work_id)
				memset(&work, 0, sizeof(struct work));
			sleep(1);
			continue;
		}

		// Check If We Are Mining The Most Current Work
		if (work.work_id != g_work.work_id) {

			// Copy Global Work Into Local Thread Work
			memcpy((void *)&work, (void *)&g_work, sizeof(struct work));
			work.thr_id = thr_id;
			new_work = true;

			// Create A Compiled VM Instance For The Thread
			if (opt_compile) {
				if (inst)
					free_compiler(inst);
				inst = calloc(1, sizeof(struct instance));
				create_instance(inst, work.work_str);
				inst->initialize(vm_m, vm_f, vm_state);
			}
		}
		// Otherwise, Just Update POW Target
		else {
			memcpy(&work.pow_target, &g_work.pow_target, 4 * sizeof(uint32_t));
		}

		work_restart[thr_id].restart = 0;

		// Run VM To Check For POW Hash & Bounties
		rc = execute_vm(thr_id, &work, inst, &hashes_done, hash, new_work);
		new_work = false;

		// Record Elapsed Time
		gettimeofday(&tv_end, NULL);
		timeval_subtract(&diff, &tv_end, &tv_start);
		if (diff.tv_sec >= 5) {
			eval_rate = (double)(hashes_done / (diff.tv_sec + diff.tv_usec * 1e-6));
			if (!opt_quiet) {
				sprintf(s, eval_rate >= 1000.0 ? "%0.2f kEval/s" : "%0.2f Eval/s", (eval_rate >= 1000.0) ? eval_rate / 1000 : eval_rate);
				applog(LOG_INFO, "CPU%d: %s", thr_id, s);
			}
			gettimeofday((struct timeval *) &tv_start, NULL);
			hashes_done = 0;
		}

		// Submit Work That Meets Bounty Criteria
		if (rc == 1) {
			applog(LOG_NOTICE, "CPU%d: Submitting Bounty Solution", thr_id);

			// Create Announcement Message
			workid32 = (uint32_t *)&work.work_id;
			memcpy(&msg[0], &workid32[1], 4);	// Swap First 4 Bytes Of Long
			memcpy(&msg[4], &workid32[0], 4);	// With Second 4 Bytes Of Long
			memcpy(&msg[8], work.multiplicator, 32);
			msg[40] = 1;
			msg32[0] = swap32(msg32[0]);
			msg32[1] = swap32(msg32[1]);

			// Create Announcment Hash
			sha256(msg, 41, work.announcement_hash);

			// Create Submit Request
			wc = (struct workio_cmd *) calloc(1, sizeof(*wc));
			if (!wc) {
				applog(LOG_ERR, "ERROR: Unable to allocate workio_cmd.  Shutting down thread for CPU%d", thr_id);
				goto out;
			}

			wc->cmd = SUBMIT_BTY_ANN;
			wc->thr = mythr;
			memcpy(&wc->work, &work, sizeof(struct work));

			// Add Solution To Queue
			if (!tq_push(thr_info[work_thr_id].q, wc)) {
				applog(LOG_ERR, "ERROR: Unable to add solution to queue.  Shutting down thread for CPU%d", thr_id);
				free(wc);
				goto out;
			}
		}

		// Submit Work That Meets POW Target
		if (rc == 2) {
			applog(LOG_NOTICE, "CPU%d: Submitting POW Solution", thr_id);
			applog(LOG_DEBUG, "DEBUG: Hash - %08X%08X%08X...  Tgt - %s", hash32[0], hash32[1], hash32[2], g_pow_target_str);
			wc = (struct workio_cmd *) calloc(1, sizeof(*wc));
			if (!wc) {
				applog(LOG_ERR, "ERROR: Unable to allocate workio_cmd.  Shutting down thread for CPU%d", thr_id);
				goto out;
			}

			wc->cmd = SUBMIT_POW;
			wc->thr = mythr;
			memcpy(&wc->work, &work, sizeof(struct work));

			// Add Solution To Queue
			if (!tq_push(thr_info[work_thr_id].q, wc)) {
				applog(LOG_ERR, "ERROR: Unable to add solution to queue.  Shutting down thread for CPU%d", thr_id);
				free(wc);
				goto out;
			}
		}
	}

out:
	if (inst) free(inst);
	if (vm_m) free(vm_m);
	if (vm_f) free(vm_f);
	if (vm_state) free(vm_state);

	tq_freeze(mythr->q);

	return NULL;
}

#ifdef USE_OPENCL
static void *gpu_miner_thread(void *userdata) {
	struct thr_info *mythr = (struct thr_info *) userdata;
	int thr_id = mythr->id;
	uint32_t *vm_out = NULL, *vm_input = NULL;
	struct work work;
	struct workio_cmd *wc = NULL;
	uint64_t hashes_done;
	struct timeval tv_start, tv_end, diff;
	int i, rc = 0;
	unsigned char *ocl_source, str[50], msg[64];
	uint32_t *msg32 = (uint32_t *)msg;
	uint32_t *workid32;
	double eval_rate;
	struct instance *inst = NULL;
	char hash[32];
	uint32_t *mult32 = (uint32_t *)work.multiplicator;
	uint32_t *hash32 = (uint32_t *)hash;

	// Set lower priority
	if (!opt_norenice)
		thread_low_priority();

	// Initialize Arrays To Hold OpenCL Core Inputs / Outputs
	vm_input = (uint32_t *)calloc(24, sizeof(uint32_t));
	vm_out = (uint32_t *)calloc(gpu[thr_id].threads, sizeof(uint32_t));

	if (!vm_out || !vm_input) {
		applog(LOG_ERR, "ERROR: Unable to allocate VM memory");
		goto out;
	}

	hashes_done = 0;
	memset(&work, 0, sizeof(work));
	gettimeofday((struct timeval *) &tv_start, NULL);

	while (1) {

		// No Work Available
		if (!g_work.work_id) {
			if (work.work_id)
				memset(&work, 0, sizeof(struct work));
			sleep(1);
			continue;
		}

		// Check If We Are Mining The Most Current Work
		if (work.work_id != g_work.work_id) {

			// Copy Global Work Into Local Thread Work
			memcpy(&work, &g_work, sizeof(struct work));
			work.thr_id = thr_id;

			// Randomize Inputs
			mult32[6] = genrand_int32();
			mult32[7] = genrand_int32();

			ocl_source = load_opencl_source(work.work_str);
			if (!ocl_source) {
				memset(&work ,0, sizeof(struct work));
				sleep(15);
				continue;
			}

			if (!init_opencl_kernel(&gpu[thr_id], ocl_source)) {
				memset(&work, 0, sizeof(struct work));
				free(ocl_source);
				sleep(15);
				continue;
			}
			free(ocl_source);
		}
		else {
			// Update Target For Work
			memcpy(&work.pow_target, &g_work.pow_target, 4 * sizeof(uint32_t));
		}

		work_restart[thr_id].restart = 0;

		// Increment multiplicator
		mult32[0] = thr_id;	// Ensures Each GPU Uses Different Inputs
		mult32[1] = 0;		// Ensures Each GPU OpenCL Thread Uses Different Inputs
		mult32[2] += 1;		// Ensures Each Evaluation Uses Different Inputs

		// Get Values For VM Inputs
		get_opencl_base_data(&work, vm_input);

		// Execute The VM
		if (!execute_kernel(&gpu[thr_id], vm_input, vm_out))
			goto out;

		// Check VM Output For Solutions
		for (i = 0; i < gpu[thr_id].threads; i++) {

			// Check For Bounty Solutions
			if (vm_out[i] == 2) {
				applog(LOG_NOTICE, "%s - %d: Submitting Bounty Solution", mythr->name, i);

				// Update Multiplicator To Include OpenCL Thread ID That Found The Bounty
				mult32[1] = i;

				// Create Announcement Message
				workid32 = (uint32_t *)&work.work_id;
				memcpy(&msg[0], &workid32[1], 4);	// Swap First 4 Bytes Of Long
				memcpy(&msg[4], &workid32[0], 4);	// With Second 4 Bytes Of Long
				memcpy(&msg[8], work.multiplicator, 32);
				msg[40] = 1;
				msg32[0] = swap32(msg32[0]);
				msg32[1] = swap32(msg32[1]);

				// Create Announcment Hash
				sha256(msg, 41, work.announcement_hash);

				// Create Submit Request
				wc = (struct workio_cmd *) calloc(1, sizeof(*wc));
				if (!wc) {
					applog(LOG_ERR, "ERROR: Unable to allocate workio_cmd.  Shutting down thread for %s", mythr->name);
					goto out;
				}

				wc->cmd = SUBMIT_BTY_ANN;
				wc->thr = mythr;
				memcpy(&wc->work, &work, sizeof(struct work));

				// Add Solution To Queue
				if (!tq_push(thr_info[work_thr_id].q, wc)) {
					applog(LOG_ERR, "ERROR: Unable to add solution to queue.  Shutting down thread for %s", mythr->name);
					free(wc);
					goto out;
				}
			}

			// Check For POW Solutions
			else if (vm_out[i] == 1) {

				// Get Hash From Kernel Data
				if (opt_debug) {
					dump_opencl_kernel_data(&gpu[thr_id], &hash32[0], i, 16, 4);
					applog(LOG_DEBUG, "DEBUG: Hash - %08X%08X%08X%08X  Tgt - %08X%08X%08X%08X", hash32[0], hash32[1], hash32[2], hash32[3], work.pow_target[0], work.pow_target[1], work.pow_target[2], work.pow_target[3]);
				}

				applog(LOG_NOTICE, "%s - %d: Submitting POW Solution", mythr->name, i);

				// Update Multiplicator To Include OpenCL Thread ID That Found The Bounty
				mult32[1] = i;

				wc = (struct workio_cmd *) calloc(1, sizeof(*wc));
				if (!wc) {
					applog(LOG_ERR, "ERROR: Unable to allocate workio_cmd.  Shutting down thread for %s", mythr->name);
					goto out;
				}

				wc->cmd = SUBMIT_POW;
				wc->thr = mythr;
				memcpy(&wc->work, &work, sizeof(struct work));

				// Add Solution To Queue
				if (!tq_push(thr_info[work_thr_id].q, wc)) {
					applog(LOG_ERR, "ERROR: Unable to add solution to queue.  Shutting down thread for %s", mythr->name);
					free(wc);
					goto out;
				}
			}
		}

		hashes_done += gpu[thr_id].threads;

		// Record Elapsed Time
		gettimeofday(&tv_end, NULL);
		timeval_subtract(&diff, &tv_end, &tv_start);
		if (diff.tv_sec >= 5) {

			mult32[6] = genrand_int32();
			mult32[7] = genrand_int32();

			if (!opt_quiet) {
				eval_rate = (double)((hashes_done / (diff.tv_sec + (diff.tv_usec / 1000000.0))) / 1000.0);
				sprintf(str, eval_rate >= 1000.0 ? "%0.2f mEval/s" : "%0.2f kEval/s", (eval_rate >= 1000.0) ? eval_rate / 1000 : eval_rate);
				applog(LOG_INFO, "%s: %s", mythr->name, str);
			}
			gettimeofday((struct timeval *) &tv_start, NULL);
			hashes_done = 0;
		}
	}

out:
	/*
	clReleaseMemObject(base_data);
	clReleaseMemObject(input);
	clReleaseMemObject(output);
	clReleaseKernel(kernel_execute);
	clReleaseKernel(kernel_initialize);
	clReleaseCommandQueue(queue);
	clReleaseContext(context);
	*/
	tq_freeze(mythr->q);

	return NULL;
}
#endif

static void restart_threads(void)
{
	int i;
	for (i = 0; i < opt_n_threads; i++)
		work_restart[i].restart = 1;
}

static void *longpoll_thread(void *userdata)
{
	struct thr_info *mythr = (struct thr_info *) userdata;
	CURL *curl;
	json_t *val = NULL, *obj, *inner_obj;
	int i, err, num_events;
	char *str = NULL;
	bool nosleep = false;

	curl = curl_easy_init();
	if (!curl) {
		applog(LOG_ERR, "CURL initialization failed");
		return NULL;
	}

	pthread_mutex_lock(&longpoll_lock);
	g_new_block = false;
	pthread_mutex_unlock(&longpoll_lock);

	sleep(1);

	while (1) {

		nosleep = false;

		if (val) json_decref(val);
		val = NULL;

		val = json_rpc_call(curl, rpc_url, rpc_userpass, "requestType=longpoll&randomId=1", &err);
		if (err > 0 || !val) {
			applog(LOG_ERR, "ERROR: longpoll failed...retrying in %d seconds", opt_fail_pause);
			sleep(opt_fail_pause);
			continue;
		}

		if (opt_protocol) {
			str = json_dumps(val, JSON_INDENT(3));
			applog(LOG_DEBUG, "DEBUG: JSON Response -\n%s", str);
			free(str);
		}

		obj = json_object_get(val, "event");
		if (!obj) {
			applog(LOG_ERR, "ERROR: longpoll decode failed...retrying in %d seconds", opt_fail_pause);
			sleep(opt_fail_pause);
			continue;
		}

		if (json_is_string(obj)) {
			str = (char *)json_string_value(obj);
			if (strcmp(str, "timeout") == 0) {
				continue;
			}
			else {
				if (strstr(str, "block")) {
					applog(LOG_NOTICE, "Longpoll: detected %s", str);
					pthread_mutex_lock(&longpoll_lock);
					g_new_block = true;
					nosleep = true;
					pthread_mutex_unlock(&longpoll_lock);
				}
			}
		}
		else if (json_is_array(obj)) {
			num_events = json_array_size(obj);
			if (num_events == 0) {
				applog(LOG_ERR, "ERROR: longpoll decode failed...retrying in %d seconds", opt_fail_pause);
				sleep(opt_fail_pause);
				continue;
			}

			for (i = 0; i<num_events; i++) {
				inner_obj = json_array_get(obj, i);
				if (!inner_obj) {
					applog(LOG_ERR, "ERROR: longpoll array parsing failed...retrying in %d seconds", opt_fail_pause);
					sleep(opt_fail_pause);
					continue;
				}

				if (json_is_string(inner_obj)) {
					str = (char *)json_string_value(inner_obj);
					if (strstr(str, "block")) {
						applog(LOG_NOTICE, "Longpoll: detected %s", str);
						pthread_mutex_lock(&longpoll_lock);
						g_new_block = true;
						nosleep = true;
						pthread_mutex_unlock(&longpoll_lock);
					}
				}
			}
		}

		if (!nosleep)
			sleep(1);
	}

	tq_freeze(mythr->q);
	curl_easy_cleanup(curl);

	return NULL;
}

static void *workio_thread(void *userdata)
{
	struct thr_info *mythr = (struct thr_info *) userdata;
	CURL *curl;
	struct workio_cmd *wc;
	int i, failures;

	curl = curl_easy_init();
	if (!curl) {
		applog(LOG_ERR, "CURL initialization failed");
		return NULL;
	}

	failures = 0;

	while (1) {

		// Get Work (New Block or Every 'Scantime' To Check For Difficulty Change)
		if (g_new_block || (time(NULL) - g_work_time) >= opt_scantime) {

			if (!get_work(curl)) {
				if ((opt_retries >= 0) && (++failures > opt_retries)) {
					applog(LOG_ERR, "ERROR: 'json_rpc_call' failed...terminating workio thread");
					break;
				}

				/* pause, then restart work-request loop */
				applog(LOG_ERR, "ERROR: 'json_rpc_call' failed...retrying in %d seconds", opt_fail_pause);
				sleep(opt_fail_pause);
				continue;
			}

			failures = 0;
			pthread_mutex_lock(&longpoll_lock);
			g_new_block = false;
			pthread_mutex_unlock(&longpoll_lock);
		}

		// Check For New Solutions On Queue
		wc = (struct workio_cmd *) tq_pop_nowait(mythr->q);
		while (wc) {
			add_submit_req(&wc->work, wc->cmd);
			free(wc);
			wc = (struct workio_cmd *) tq_pop_nowait(mythr->q);
		}

		// Submit POW / Bounty
		for (i = 0; i < g_submit_req_cnt; i++) {

			// Skip Completed Requests
			if (g_submit_req[i].req_type == SUBMIT_COMPLETE)
				continue;

			// Skip Requests That Are On Hold
			if (g_submit_req[i].delay_tm >= time(NULL))
				continue;

			// Submit Request
			if (!submit_work(curl, &g_submit_req[i]))
				applog(LOG_ERR, "ERROR: Submit bounty request failed");
		}

		// Remove Completed Solutions
		for (i = 0; i < g_submit_req_cnt; i++) {

			if (g_submit_req[i].req_type == SUBMIT_COMPLETE) {
				applog(LOG_DEBUG, "DEBUG: Submit complete...deleting request");
				delete_submit_req(i);
				continue;
			}

			// Remove Stale Requests After 15min
			if (time(NULL) - g_submit_req[i].start_tm >= 900) {
				applog(LOG_DEBUG, "DEBUG: Submit request timed out after 15min");
				delete_submit_req(i);
				g_bounty_timeout_cnt++;
			}
		}

		// Sleep For 15 Sec If No Work
		if (!g_work.work_id) {
			for (i = 0; i < 15; i++) {
				sleep(1);
				if (g_new_block || g_submit_req_cnt)
					break;
			}
		}
	}

	tq_freeze(mythr->q);
	curl_easy_cleanup(curl);

	return NULL;
}

static bool add_submit_req(struct work *work, enum submit_commands req_type) {
	uint32_t *hash32 = (uint32_t *)work->announcement_hash;
	uint32_t *mult32 = (uint32_t *)work->multiplicator;

	pthread_mutex_lock(&submit_lock);

	g_submit_req = realloc(g_submit_req, (g_submit_req_cnt + 1) * sizeof(struct submit_req));
	if (!g_submit_req) {
		g_submit_req_cnt = 0;
		applog(LOG_ERR, "ERROR: Bounty request allocation failed");
		return false;
	}
	g_submit_req[g_submit_req_cnt].thr_id = work->thr_id;
	g_submit_req[g_submit_req_cnt].bounty = false;
	g_submit_req[g_submit_req_cnt].req_type = req_type;
	g_submit_req[g_submit_req_cnt].start_tm = time(NULL);
	g_submit_req[g_submit_req_cnt].delay_tm = 0;
	g_submit_req[g_submit_req_cnt].retries = 0;
	g_submit_req[g_submit_req_cnt].work_id = work->work_id;
	strncpy(g_submit_req[g_submit_req_cnt].work_str, work->work_str, 21);
	sprintf(g_submit_req[g_submit_req_cnt].hash, "%08X%08X%08X%08X%08X%08X%08X%08X", swap32(hash32[0]), swap32(hash32[1]), swap32(hash32[2]), swap32(hash32[3]), swap32(hash32[4]), swap32(hash32[5]), swap32(hash32[6]), swap32(hash32[7]));
	sprintf(g_submit_req[g_submit_req_cnt].mult, "%08X%08X%08X%08X%08X%08X%08X%08X", swap32(mult32[0]), swap32(mult32[1]), swap32(mult32[2]), swap32(mult32[3]), swap32(mult32[4]), swap32(mult32[5]), swap32(mult32[6]), swap32(mult32[7]));
	if (req_type != SUBMIT_POW) {
		g_submit_req[g_submit_req_cnt].bounty = true;
		update_pending_cnt(work->work_id, true);
	}
	g_submit_req_cnt++;

	pthread_mutex_unlock(&submit_lock);
	return true;
}

static bool delete_submit_req(int idx) {
	struct submit_req *req = NULL;
	int i;

	pthread_mutex_lock(&submit_lock);

	if (g_submit_req[idx].bounty)
		update_pending_cnt(g_submit_req[idx].work_id, false);

	if (g_submit_req_cnt > 1) {
		req = malloc((g_submit_req_cnt - 1) * sizeof(struct submit_req));
		if (!req) {
			applog(LOG_ERR, "ERROR: Bounty request allocation failed");
			pthread_mutex_unlock(&submit_lock);
			return false;
		}
	}
	else {
		if (g_submit_req) free(g_submit_req);
		g_submit_req = 0;
		g_submit_req_cnt = 0;
		pthread_mutex_unlock(&submit_lock);
		return true;
	}

	for (i = 0; i < idx; i++)
		memcpy(&req[i], &g_submit_req[i], sizeof(struct submit_req));

	for (i = idx + 1; i < g_submit_req_cnt; i++)
		memcpy(&req[i - 1], &g_submit_req[i], sizeof(struct submit_req));

	free(g_submit_req);
	g_submit_req = req;
	g_submit_req_cnt--;
	pthread_mutex_unlock(&submit_lock);
	return true;
}

static void *key_monitor_thread(void *userdata)
{
	int i, ch, day, hour, min, sec, total_sec, pending_bty;
	struct timeval now;

	while (true)
	{
		sleep(1);
		ch = getchar();
		ch = toupper(ch);
		if (ch == '\n')
			continue;

		switch (ch)
		{
		case 'S':
		{
			gettimeofday(&now, NULL);
			total_sec = now.tv_sec - g_miner_start_time.tv_sec;
			day = total_sec / 3600 / 24;
			hour = total_sec / 3600 - day * 24;
			min = total_sec / 60 - (day * 24 + hour) * 60;
			sec = total_sec % 60;

			pending_bty = 0;
			for (i = 0; i < g_work_package_cnt; i++) {
				pending_bty += g_work_package[i].pending_bty_cnt;
			}

			applog(LOG_WARNING, "************************** Mining Summary **************************");
			applog(LOG_WARNING, "Run Time: %02d Days %02d:%02d:%02d\t\tWork Name: %s", day, hour, min, sec, g_work_nm ? g_work_nm : "");
			applog(LOG_WARNING, "Bounty Pending:\t%3d\t\tWork ID:   %s", pending_bty, g_work_id ? g_work_id : "");
			applog(LOG_WARNING, "Bounty Accept:\t%3d\t\tTarget:    %s", g_bounty_accepted_cnt, g_pow_target_str);
			applog(LOG_WARNING, "Bounty Reject:\t%3d", g_bounty_rejected_cnt);
			applog(LOG_WARNING, "Bounty Deprct:\t%3d\t\tPOW Accept:  %3d", g_bounty_deprecated_cnt, g_pow_accepted_cnt);
			applog(LOG_WARNING, "Bounty Timeout:\t%3d\t\tPOW Reject:  %3d", g_bounty_timeout_cnt, g_pow_rejected_cnt);
			applog(LOG_WARNING, "Bounty Error:\t%3d\t\tPOW Discard: %3d", g_bounty_error_cnt, g_pow_discarded_cnt);
			applog(LOG_WARNING, "********************************************************************");

		}
		break;
		case 'D':
			opt_debug = !opt_debug;
			applog(LOG_WARNING, "Debug Mode: %s", opt_debug ? "On" : "Off");
			break;
		case 'E':
			opt_debug_epl = !opt_debug_epl;
			applog(LOG_WARNING, "Debug ElasticPL Mode: %s", opt_debug_epl ? "On" : "Off");
			break;
		case 'P':
			opt_protocol = !opt_protocol;
			applog(LOG_WARNING, "Protocol Mode: %s", opt_protocol ? "On" : "Off");
			break;
		case 'Q':
			opt_quiet = !opt_quiet;
			applog(LOG_WARNING, "Quiet Mode: %s", opt_quiet ? "On" : "Off");
			break;
		case 'V':
			opt_debug_vm = !opt_debug_vm;
			applog(LOG_WARNING, "Debug VM Mode: %s", opt_debug_vm ? "On" : "Off");
			break;
		}
	}
	return 0;
}

#ifndef WIN32
static void signal_handler(int sig)
{
	switch (sig) {
	case SIGHUP:
		applog(LOG_INFO, "SIGHUP received");
		break;
	case SIGINT:
		applog(LOG_INFO, "SIGINT received, exiting");
		exit(0);
		break;
	case SIGTERM:
		applog(LOG_INFO, "SIGTERM received, exiting");
		exit(0);
		break;
	}
}
#else
BOOL WINAPI ConsoleHandler(DWORD dwType)
{
	switch (dwType) {
	case CTRL_C_EVENT:
		applog(LOG_INFO, "CTRL_C_EVENT received, exiting");
		exit(0);
		break;
	case CTRL_BREAK_EVENT:
		applog(LOG_INFO, "CTRL_BREAK_EVENT received, exiting");
		exit(0);
		break;
	default:
		return false;
	}
	return true;
}
#endif

static int thread_create(struct thr_info *thr, void* func)
{
	int err = 0;
	pthread_attr_init(&thr->attr);
	err = pthread_create(&thr->pth, &thr->attr, func, thr);
	pthread_attr_destroy(&thr->attr);
	return err;
}

int main(int argc, char **argv) {
	struct thr_info *thr;
	int i, err, thr_idx, num_gpus = 0;

	fprintf(stdout, "** " PACKAGE_NAME " " PACKAGE_VERSION " **\n");

	pthread_mutex_init(&applog_lock, NULL);

#if defined(WIN32)
	SYSTEM_INFO sysinfo;
	GetSystemInfo(&sysinfo);
	num_cpus = sysinfo.dwNumberOfProcessors;
#elif defined(_SC_NPROCESSORS_CONF)
	num_cpus = sysconf(_SC_NPROCESSORS_CONF);
#elif defined(CTL_HW) && defined(HW_NCPU)
	int req[] = { CTL_HW, HW_NCPU };
	size_t len = sizeof(num_cpus);
	sysctl(req, 2, &num_cpus, &len, NULL, 0);
#else
	num_cpus = 1;
#endif
	if (num_cpus < 1)
		num_cpus = 1;

	// Process Command Line Before Starting Any Threads
	parse_cmdline(argc, argv);

#ifdef USE_OPENCL
	// Initialize GPU Devices
	if (opt_opencl) {
		num_gpus = init_opencl_devices();
		if (num_gpus == 0) {
			applog(LOG_ERR, "ERROR: No OpenCL Devices that support 64bit Floating Point math found");
			return 1;
		}
	}
#else
	if (opt_opencl) {
		applog(LOG_ERR, "ERROR: OpenCL not found on this system.  Running miner with compiled C code instead");
		opt_opencl = false;
		opt_compile = true;
	}
#endif


	if (!opt_n_threads) {
		if (!opt_opencl)
			opt_n_threads = num_cpus;
		else
			opt_n_threads = num_gpus;
	}

	if (!rpc_url)
		rpc_url = strdup("http://127.0.0.1:6876/nxt");

	if (rpc_user && rpc_pass) {
		rpc_userpass = (char*)malloc(strlen(rpc_user) + strlen(rpc_pass) + 2);
		if (!rpc_userpass)
			return 1;
		sprintf(rpc_userpass, "%s:%s", rpc_user, rpc_pass);
	}

	if (!opt_test_vm && !passphrase) {
		applog(LOG_ERR, "ERROR: Passphrase (option -P) is required");
		return 1;
	}

	// Seed Random Number Generator
	init_genrand((unsigned long)time(NULL));
//	RAND_poll();

#ifndef WIN32
	/* Always catch Ctrl+C */
	signal(SIGINT, signal_handler);
#else
	SetConsoleCtrlHandler((PHANDLER_ROUTINE)ConsoleHandler, TRUE);
#endif

	pthread_mutex_init(&work_lock, NULL);
	pthread_mutex_init(&submit_lock, NULL);
	pthread_mutex_init(&longpoll_lock, NULL);

	work_restart = (struct work_restart*) calloc(opt_n_threads, sizeof(*work_restart));
	if (!work_restart)
		return 1;

	thr_info = (struct thr_info*) calloc(opt_n_threads + 3, sizeof(*thr));
	if (!thr_info)
		return 1;

	// In Test Compiler Mode, Run Parser / Complier Using Source Code From Test File
	if (opt_test_vm) {
		thr = &thr_info[0];
		thr->id = 0;
		thr->q = tq_new();
		if (!thr->q)
			return 1;
		if (thread_create(thr, test_vm_thread)) {
			applog(LOG_ERR, "Test VM thread create failed!");
			return 1;
		}

		pthread_join(thr_info[0].pth, NULL);
		free(test_filename);
		return 0;
	}

	// Init workio Thread Info
	work_thr_id = opt_n_threads;
	thr = &thr_info[work_thr_id];
	thr->id = work_thr_id;
	thr->q = tq_new();
	if (!thr->q)
		return 1;

	// Start workio Thread
	if (thread_create(thr, workio_thread)) {
		applog(LOG_ERR, "work thread create failed");
		return 1;
	}

	applog(LOG_INFO, "Attempting to start %d miner threads", opt_n_threads);

	thr_idx = 0;

	// Start Mining Threads
	for (i = 0; i < opt_n_threads; i++) {
		thr = &thr_info[thr_idx];

		thr->id = thr_idx++;
		thr->q = tq_new();
		if (!thr->q)
			return 1;
		if (opt_opencl) {
#ifdef USE_OPENCL
			err = thread_create(thr, gpu_miner_thread);
#endif
			sprintf(thr->name, "GPU%d", i);
		}
		else {
			err = thread_create(thr, cpu_miner_thread);
			sprintf(thr->name, "CPU%d", i);
		}
		if (err) {
			applog(LOG_ERR, "%s mining thread create failed!", thr->name);
			return 1;
		}
	}

	applog(LOG_INFO, "%d mining threads started", opt_n_threads);

	gettimeofday(&g_miner_start_time, NULL);

	// Start Longpoll Thread
	thr = &thr_info[opt_n_threads + 1];
	thr->id = opt_n_threads + 1;
	thr->q = tq_new();
	if (!thr->q)
		return 1;
	if (thread_create(thr, longpoll_thread)) {
		applog(LOG_ERR, "Longpoll thread create failed");
		return 1;
	}

	// Start Key Monitor Thread
	thr = &thr_info[opt_n_threads + 2];
	thr->id = opt_n_threads + 2;
	thr->q = tq_new();
	if (!thr->q)
		return 1;
	if (thread_create(thr, key_monitor_thread)) {
		applog(LOG_ERR, "Key monitor thread create failed");
		return 1;
	}

	// Main Loop - Wait for workio thread to exit
	pthread_join(thr_info[work_thr_id].pth, NULL);

	applog(LOG_WARNING, "Exiting " PACKAGE_NAME);

	free(test_filename);
	return 0;
}