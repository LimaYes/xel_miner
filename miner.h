#ifndef __MINER_H__
#define __MINER_H__

#define PACKAGE_NAME "xel_miner"
#define MINER_VERSION "0.9.6"

#define USER_AGENT PACKAGE_NAME "/" MINER_VERSION
#define MAX_CPUS 16

#define MAX_POW_PER_BLOCK 10

#include <curl/curl.h>
#include <jansson.h>
#include <pthread.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifdef WIN32
#include <windows.h>
#define sleep(secs) Sleep((secs) * 1000)
#else
#include <unistd.h> /* close */
#endif

#include "elist.h"
#include "ElasticPL/ElasticPL.h"
#include "ElasticPL/ElasticPLFunctions.h"
#include "crypto/sha2.h"

#ifdef _MSC_VER
#define strdup(...) _strdup(__VA_ARGS__)
#define strcasecmp(x,y) _stricmp(x,y)
#define strncasecmp(x,y,z) _strnicmp(x,y,z)
#define __thread __declspec(thread)
#define _ALIGN(x) __declspec(align(x))
#else
#define _ALIGN(x) __attribute__ ((aligned(x)))
#endif

#if JANSSON_MAJOR_VERSION >= 2
#define JSON_LOADS(str, err_ptr) json_loads(str, 0, err_ptr)
#define JSON_LOAD_FILE(path, err_ptr) json_load_file(path, 0, err_ptr)
#else
#define JSON_LOADS(str, err_ptr) json_loads(str, err_ptr)
#define JSON_LOAD_FILE(path, err_ptr) json_load_file(path, err_ptr)
#endif

extern __thread _ALIGN(64) uint32_t *vm_m;
extern __thread _ALIGN(64) int32_t *vm_i;
extern __thread _ALIGN(64) uint32_t *vm_u;
extern __thread _ALIGN(64) int64_t *vm_l;
extern __thread _ALIGN(64) uint64_t *vm_ul;
extern __thread _ALIGN(64) float *vm_f;
extern __thread _ALIGN(64) double *vm_d;
extern __thread _ALIGN(64) uint32_t *vm_s;

extern bool use_elasticpl_math;

extern bool opt_debug;
extern bool opt_debug_epl;
extern bool opt_debug_vm;
extern bool opt_protocol;
extern bool opt_quiet;
extern int opt_timeout;
extern int opt_n_threads;
extern bool opt_test_vm;
extern bool opt_opencl;
extern int opt_opencl_gthreads;
extern int opt_opencl_vwidth;

extern struct work_package *g_work_package;
extern int g_work_package_cnt;
extern int g_work_package_idx;

extern struct work_restart *work_restart;

static const int BASE85_POW[] = {
	1,
	85,
	85 * 85,
	85 * 85 * 85,
	85 * 85 * 85 * 85
};

enum submit_commands {
	SUBMIT_BOUNTY,
	SUBMIT_POW,
	SUBMIT_COMPLETE
};

struct work_package {
	uint64_t block_id;
	uint64_t work_id;
	unsigned char work_str[22];
	unsigned char work_nm[50];
	uint32_t WCET;
	uint64_t bounty_limit;
	uint64_t pow_reward;
	uint64_t bty_reward;
	int pending_bty_cnt;
	bool blacklisted;
	bool active;

	// VM Memory
	uint32_t vm_ints;
	uint32_t vm_uints;
	uint32_t vm_longs;
	uint32_t vm_ulongs;
	uint32_t vm_floats;
	uint32_t vm_doubles;

	// Data Submissions
	uint32_t submit_sz;		// Number Of Unsigned Ints To Submit To Node For Validation
	uint32_t submit_idx;	// Index In u[] To Extract Submitted Values From

	// Data Storage
	uint32_t iteration_id;
	uint32_t storage_id;	// Current Storage ID Used By Miner
	uint32_t storage_sz;	// Number Of Unsigned Ints In Storage
	uint32_t storage_idx;	// Index In u[] To Extract Storage From
	uint32_t storage_cnt;	// Number Of Storage Solutions For Iteration
	uint32_t *storage;

};

struct work {
	int thr_id;
	int package_id;
	uint64_t block_id;
	uint64_t work_id;
	uint32_t iteration_id;
	unsigned char work_str[22];
	unsigned char work_nm[50];
	uint32_t pow_target[4];
	uint32_t pow_hash[4];
	uint32_t vm_input[12];
	unsigned char multiplicator[32];
};

struct thr_info {
	int id;
	char name[6];
	pthread_t pth;
	pthread_attr_t attr;
	struct thread_q	*q;
};

struct work_restart {
	volatile uint8_t restart;
	char padding[128 - sizeof(uint8_t)];
};

struct submit_req {
	int thr_id;
	bool bounty;
	enum submit_commands req_type;
	time_t start_tm;	// Time Request Was Submitted
	time_t delay_tm;	// If Populated, Time When Next Request Can Be Sent
	int retries;
	char mult[65];		// Multiplicator In Hex
	char hash[33];		// POW Hash In Hex
	uint64_t work_id;
	unsigned char work_str[22];
	uint32_t iteration_id;
	uint32_t storage_id;
	uint32_t submit_data_sz;
	uint32_t *submit_data;
};

struct workio_cmd {
	enum submit_commands cmd;
	struct thr_info *thr;
	struct work work;
	uint32_t *submit_data;
};

struct header_info {
	char		*lp_path;
	char		*reason;
	char		*stratum_url;
};

struct data_buffer {
	void		*buf;
	size_t		len;
};

struct upload_buffer {
	const void	*buf;
	size_t		len;
	size_t		pos;
};

struct tq_ent {
	void				*data;
	struct list_head	q_node;
};

struct thread_q {
	struct list_head	q;

	bool frozen;

	pthread_mutex_t		mutex;
	pthread_cond_t		cond;
};

struct instance {

#ifdef WIN32
	HINSTANCE hndl;
	int32_t(__cdecl* initialize)(uint32_t *, int32_t *, uint32_t *, int64_t *, uint64_t *, float *, double *, uint32_t *);
	int32_t(__cdecl* execute)(uint64_t, uint32_t *, uint32_t, uint32_t *, uint32_t *, uint32_t *);
	int32_t(__cdecl* verify)(uint64_t, uint32_t *, uint32_t, uint32_t *, uint32_t *, uint32_t *);
#else
	void *hndl;
	int32_t(*initialize)(uint32_t *, int32_t *, uint32_t *, int64_t *, uint64_t *, float *, double *, uint32_t *);
	int32_t(*execute)(uint64_t, uint32_t *, uint32_t, uint32_t *, uint32_t *, uint32_t *);
	int32_t(*verify)(uint64_t, uint32_t *, uint32_t, uint32_t *, uint32_t *, uint32_t *);
#endif

};

extern bool use_colors;
extern struct thr_info *thr_info;
extern pthread_mutex_t applog_lock;
extern pthread_mutex_t response_lock;

enum {
	LOG_ERR,
	LOG_WARNING,
	LOG_NOTICE,
	LOG_INFO,
	LOG_DEBUG,
	LOG_BLUE = 0x10,	// Custom Notices
};

#define JSON_RPC_LONGPOLL	(1 << 0)
#define JSON_RPC_QUIET_404	(1 << 1)
#define JSON_RPC_IGNOREERR  (1 << 2)
#define JSON_BUF_LEN 512

// Colors For Text Output
#define CL_N    "\x1B[0m"
#define CL_RED  "\x1B[31m"
#define CL_GRN  "\x1B[32m"
#define CL_YLW  "\x1B[33m"
#define CL_BLU  "\x1B[34m"
#define CL_MAG  "\x1B[35m"
#define CL_CYN  "\x1B[36m"
#define CL_BLK  "\x1B[22;30m" /* black */
#define CL_RD2  "\x1B[22;31m" /* red */
#define CL_GR2  "\x1B[22;32m" /* green */
#define CL_BRW  "\x1B[22;33m" /* brown */
#define CL_BL2  "\x1B[22;34m" /* blue */
#define CL_MA2  "\x1B[22;35m" /* magenta */
#define CL_CY2  "\x1B[22;36m" /* cyan */
#define CL_SIL  "\x1B[22;37m" /* gray */
#ifdef WIN32
#define CL_GRY  "\x1B[01;30m" /* dark gray */
#else
#define CL_GRY  "\x1B[90m"    /* dark gray selectable in putty */
#endif
#define CL_LRD  "\x1B[01;31m" /* light red */
#define CL_LGR  "\x1B[01;32m" /* light green */
#define CL_YL2  "\x1B[01;33m" /* yellow */
#define CL_LBL  "\x1B[01;34m" /* light blue */
#define CL_LMA  "\x1B[01;35m" /* light magenta */
#define CL_LCY  "\x1B[01;36m" /* light cyan */
#define CL_WHT  "\x1B[01;37m" /* white */


#ifdef USE_OPENCL

#ifdef __APPLE__
#include <OpenCL/cl.h>
#else
#include <CL/cl.h>
#endif

struct opencl_device {
	unsigned char name[100];
	cl_platform_id platform_id;
	cl_device_id device_id;
	cl_context context;
	cl_command_queue queue;
	cl_kernel kernel;
	cl_uint work_dim;
	int threads;
	size_t global_size[3];
	size_t local_size[3];
	cl_mem obj_dat;
	cl_mem obj_rnd;
	cl_mem obj_res;
	cl_mem obj_out;
	cl_mem obj_sub;
	cl_mem obj_storage;
};

extern struct opencl_device *gpu;

extern int init_opencl_devices();
extern int opencl_init_devices();
extern unsigned char* opencl_load_source(char *work_str);
extern bool opencl_create_kernel(struct opencl_device *gpu, char *ocl_source, uint32_t storage_sz);
extern bool opencl_create_buffers(struct opencl_device *gpu);
extern bool opencl_calc_worksize(struct opencl_device *gpu);
extern bool opencl_init_buffer_data(struct opencl_device *gpu, uint32_t *base_data, uint32_t *round, uint32_t *result, uint32_t *storage, uint32_t storage_sz);
extern bool opencl_run_kernel(struct opencl_device *gpu, uint32_t *rnd_num, uint32_t *result, uint32_t *output, uint32_t *submit, uint32_t submit_sz);
static void *gpu_miner_thread(void *userdata);
static bool opencl_err_check(int err, char *err_code);
#endif

struct thread_q;

struct thread_q *tq_new(void);
void tq_free(struct thread_q *tq);
bool tq_push(struct thread_q *tq, void *data);
void *tq_pop(struct thread_q *tq, const struct timespec *abstime);
void *tq_pop_nowait(struct thread_q *tq);
void tq_freeze(struct thread_q *tq);
void tq_thaw(struct thread_q *tq);

static void *key_monitor_thread(void *userdata);
static void *longpoll_thread(void *userdata);
static void *test_vm_thread(void *userdata);
static void *workio_thread(void *userdata);
static void restart_threads(void);
static void *cpu_miner_thread(void *userdata);

extern uint32_t swap32(uint32_t a);
static void parse_cmdline(int argc, char *argv[]);
static void strhide(char *s);
static void parse_arg(int key, char *arg);
static void show_usage_and_exit(int status);
static void show_version_and_exit(void);
static bool load_test_file(char *file_name, char *buf);
static bool get_vm_input(struct work *work);
static int execute_vm(int thr_id, uint32_t *rnd, uint32_t iteration, struct work *work, struct instance *inst, long *hashes_done);
static void dump_vm(int idx);

static bool get_work(CURL *curl);
static int decode_work(CURL *curl, const json_t *val, struct work *work);
static bool get_work_source(CURL *curl, char *work_str, char *elastic_src);
static int get_work_storage(CURL *curl, char *work_str, uint32_t *storage);
static bool validate_work_source(int package_id, struct instance *inst);
static double calc_diff(uint32_t *target);
extern bool add_work_package(struct work_package *work_package);
static void update_pending_cnt(uint64_t work_id, bool add);

static bool submit_work(CURL *curl, struct submit_req *req);
static bool delete_submit_req(int idx);
static bool add_submit_req(struct work *work, uint32_t *data, enum submit_commands req_type);

static bool get_opencl_base_data(struct work *work, uint32_t *vm_input);

// Function Prototypes - util.c
extern void applog(int prio, const char *fmt, ...);
extern int timeval_subtract(struct timeval *result, struct timeval *x, struct timeval *y);
extern bool bin2hex(unsigned char *in, int in_sz, unsigned char *out, int out_sz);
extern bool hex2ints(uint32_t *p, int array_sz, const char *hex, int len);
extern bool ints2hex(uint32_t *in, int num, unsigned char *out, int out_sz);
extern int32_t bin2int(unsigned char *str);
extern bool ascii85dec(unsigned char *str, int strsz, const char *ascii85);
static void databuf_free(struct data_buffer *db);
static size_t all_data_cb(const void *ptr, size_t size, size_t nmemb, void *user_data);
extern json_t* json_rpc_call(CURL *curl, const char *url, const char *userpass, const char *req, int *curl_err);

extern unsigned long genrand_int32(void);
extern void init_genrand(unsigned long s);

static bool create_c_source(char *work_str);
extern bool compile_library(char *work_str);
extern void create_instance(struct instance* inst, char *work_str);
extern void free_library(struct instance* inst);
extern bool create_opencl_source(char *work_str);

int curve25519_donna(uint8_t *mypublic, const uint8_t *secret, const uint8_t *basepoint);

#endif /* __MINER_H__ */