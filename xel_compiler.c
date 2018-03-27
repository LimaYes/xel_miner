#define _GNU_SOURCE
#define __USE_GNU

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include "miner.h"

#ifndef WIN32
#include <dlfcn.h>
#endif

#ifndef LM_ID_BASE
#define LM_ID_BASE              0x00
#endif

bool create_c_source(char *work_str) {
	FILE* f = fopen("./work/work_lib.c", "w");
	if (!f)
		return false;

	fprintf(f, "#include <stdbool.h>\n");
	fprintf(f, "#include <stdio.h>\n");
	fprintf(f, "#include <stdint.h>\n");
	fprintf(f, "#include <stdlib.h>\n");
	fprintf(f, "#include <limits.h>\n");
	fprintf(f, "#include <time.h>\n");
	fprintf(f, "#include <openssl/md5.h>\n");

	if (use_elasticpl_math) {
		fprintf(f, "#include <math.h>\n");
		fprintf(f, "#include \"../ElasticPL/ElasticPLFunctions.h\"\n");
	}
	fprintf(f, "\n");

#ifdef _MSC_VER
	fprintf(f, "__declspec(thread) uint32_t *m = NULL;\n");
	fprintf(f, "__declspec(thread) int32_t *i = NULL;\n");
	fprintf(f, "__declspec(thread) uint32_t *u = NULL;\n");
	fprintf(f, "__declspec(thread) int64_t *l = NULL;\n");
	fprintf(f, "__declspec(thread) uint64_t *ul = NULL;\n");
	fprintf(f, "__declspec(thread) float *f = NULL;\n");
	fprintf(f, "__declspec(thread) double *d = NULL;\n");
	fprintf(f, "__declspec(thread) uint32_t *s = NULL;\n\n");
#else
	fprintf(f, "__thread uint32_t *m = NULL;\n");
	fprintf(f, "__thread int32_t *i = NULL;\n");
	fprintf(f, "__thread uint32_t *u = NULL;\n");
	fprintf(f, "__thread int64_t *l = NULL;\n");
	fprintf(f, "__thread uint64_t *ul = NULL;\n");
	fprintf(f, "__thread float *f = NULL;\n");
	fprintf(f, "__thread double *d = NULL;\n");
	fprintf(f, "__thread uint32_t *s = NULL;\n\n");
#endif

	fprintf(f, "static uint32_t rotl32(uint32_t x, uint32_t n);\n");
	fprintf(f, "static uint32_t rotr32(uint32_t x, uint32_t n);\n");
	fprintf(f, "static uint64_t rotl64(uint64_t x, uint64_t n);\n");
	fprintf(f, "static uint64_t rotr64(uint64_t x, uint64_t n);\n\n");
	fprintf(f, "static uint32_t check_pow(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t *, uint32_t *, uint32_t *);\n\n");

	// Include C Source Code For ElasticPL Jobs
	fprintf(f, "#include \"job_%s.h\"\n\n", work_str);

	fprintf(f, "static const uint32_t mask32 = (CHAR_BIT*sizeof(uint32_t)-1);\n");
	fprintf(f, "static const uint64_t mask64 = (CHAR_BIT*sizeof(uint64_t)-1);\n\n");

	fprintf(f, "static uint32_t rotl32 (uint32_t x, uint32_t n) {\n");
	fprintf(f, "\tn &= mask32;  // avoid undef behaviour with NDEBUG.  0 overhead for most types / compilers\n");
	fprintf(f, "\treturn (x<<n) | (x>>( (-n)&mask32 ));\n");
	fprintf(f, "}\n\n");

	fprintf(f, "static uint32_t rotr32 (uint32_t x, uint32_t n) {\n");
	fprintf(f, "\tn &= mask32;  // avoid undef behaviour with NDEBUG.  0 overhead for most types / compilers\n");
	fprintf(f, "\treturn (x>>n) | (x<<( (-n)&mask32 ));\n");
	fprintf(f, "}\n\n");

	fprintf(f, "static uint64_t rotl64 (uint64_t x, uint64_t n) {\n");
	fprintf(f, "\tn &= mask64;  // avoid undef behaviour with NDEBUG.  0 overhead for most types / compilers\n");
	fprintf(f, "\treturn (x<<n) | (x>>( (-n)&mask64 ));\n");
	fprintf(f, "}\n\n");

	fprintf(f, "static uint64_t rotr64 (uint64_t x, uint64_t n) {\n");
	fprintf(f, "\tn &= mask64;  // avoid undef behaviour with NDEBUG.  0 overhead for most types / compilers\n");
	fprintf(f, "\treturn (x>>n) | (x<<( (-n)&mask64 ));\n");
	fprintf(f, "}\n\n");

	fprintf(f, "static uint32_t check_pow(uint32_t msg_0, uint32_t msg_1, uint32_t msg_2, uint32_t msg_3, uint32_t *m, uint32_t *target, uint32_t *hash) {\n");
	fprintf(f, "\tint i;\n");
	fprintf(f, "\tchar msg[48];\n");
	fprintf(f, "\tuint32_t *msg32 = (uint32_t *)(msg);\n");
	fprintf(f, "\tmsg32[0] = msg_0;\n");
	fprintf(f, "\tmsg32[1] = msg_1;\n");
	fprintf(f, "\tmsg32[2] = msg_2;\n");
	fprintf(f, "\tmsg32[3] = msg_3;\n\n");
	fprintf(f, "\tfor (i = 0; i < 8; i++)\n");
	fprintf(f, "\t\tmsg32[i+4] = m[i];\n\n");
	fprintf(f, "\tMD5(msg, 48, (unsigned char *)hash);\n\n");
	fprintf(f, "\tfor (i = 0; i < 4; i++) {\n");
	fprintf(f, "\t\tif (hash[i] > target[i])\n");
	fprintf(f, "\t\t\treturn 0;\n");
	fprintf(f, "\t\telse if (hash[i] < target[i])\n");
	fprintf(f, "\t\t\treturn 1;    // POW Solution Found\n");
	fprintf(f, "\t}\n");
	fprintf(f, "\treturn 0;\n");
	fprintf(f, "}\n\n");

#ifdef WIN32
	fprintf(f, "__declspec(dllexport) int32_t initialize(uint32_t *vm_m, int32_t *vm_i, uint32_t *vm_u, int64_t *vm_l, uint64_t *vm_ul, float *vm_f, double *vm_d, uint32_t *vm_s) {\n");
#else
	fprintf(f, "int32_t initialize(uint32_t *vm_m, int32_t *vm_i, uint32_t *vm_u, int64_t *vm_l, uint64_t *vm_ul, float *vm_f, double *vm_d, uint32_t *vm_s) {\n");
#endif
	fprintf(f, "\tm = vm_m;\n");
	fprintf(f, "\ti = vm_i;\n");
	fprintf(f, "\tu = vm_u;\n");
	fprintf(f, "\tl = vm_l;\n");
	fprintf(f, "\tul = vm_ul;\n");
	fprintf(f, "\tf = vm_f;\n");
	fprintf(f, "\td = vm_d;\n");
	fprintf(f, "\ts = vm_s;\n\n");
	fprintf(f, "\treturn 0;\n");

	fprintf(f, "}\n\n");

#ifdef WIN32
	fprintf(f, "__declspec(dllexport) int32_t execute( uint64_t work_id, uint32_t *bounty_found, uint32_t verify_pow, uint32_t *pow_found, uint32_t *target, uint32_t *hash ) {\n\n");
#else
	fprintf(f, "int32_t execute( uint64_t work_id, uint32_t *bounty_found, uint32_t verify_pow, uint32_t *pow_found, uint32_t *target, uint32_t *hash ) {\n\n");
#endif

	// Call The Main Function For The Current Job
	fprintf(f, "\tmain_%s(bounty_found, verify_pow, pow_found, target, hash);\n\n", work_str);
	fprintf(f, "\treturn 0;\n");
	fprintf(f, "}\n\n");

#ifdef WIN32
	fprintf(f, "__declspec(dllexport) int32_t verify( uint64_t work_id, uint32_t *bounty_found, uint32_t verify_pow, uint32_t *pow_found, uint32_t *target, uint32_t *hash ) {\n\n");
#else
	fprintf(f, "int32_t verify( uint64_t work_id, uint32_t *bounty_found, uint32_t verify_pow, uint32_t *pow_found, uint32_t *target, uint32_t *hash ) {\n\n");
#endif

	// Call The Verify Function For The Current Job
	fprintf(f, "\tverify_%s(bounty_found, verify_pow, pow_found, target, hash);\n\n", work_str);
	fprintf(f, "\treturn 0;\n");
	fprintf(f, "}\n\n");

	fflush(f);
	fclose(f);
	return true;
}

bool compile_library(char *work_str) {
	char lib_name[50], str[256];
	int ret = 0;

	applog(LOG_DEBUG, "DEBUG: Converting ElasticPL to C");

	if (!create_c_source(work_str)) {
		applog(LOG_ERR, "Unable to convert ElasticPL to %s code", opt_opencl ? "OpenCL" : "C");
		return false;
	}

	sprintf(lib_name, "job_%s", work_str);
	applog(LOG_DEBUG, "DEBUG: Compiling C Library: %s", lib_name);

#ifdef _MSC_VER
	sprintf(str, "compile_dll.bat ./work/%s.dll", lib_name);
	system(str);
#else
#ifdef __MINGW32__
	ret = system("gcc -I./crypto -c -march=native -Ofast -msse -msse2 -msse3 -mmmx -m3dnow -DBUILDING_EXAMPLE_DLL ./work/work_lib.c -o ./work/work_lib.o");
	sprintf(str, "gcc -shared -o ./work/%s.dll ./work/work_lib.o -L./ElasticPL -L./crypto -lElasticPLFunctions -lcrypto", lib_name);
	ret = system(str);
#else
#ifdef __arm__
	ret = system("gcc -I./crypto -c -std=c99 -Ofast -fPIC ./work/work_lib.c -o ./work/work_lib.o");
	sprintf(str, "gcc -std=c99 -shared -Wl,-soname,./work/%s.so.1 -o ./work/%s.so ./work/work_lib.o -L./ElasticPL -L./crypto -lElasticPLFunctions -lcrypto", lib_name, lib_name);
	ret = system(str);
#else
	ret = system("gcc -I./crypto -c -g -march=native -Ofast -fPIC ./work/work_lib.c -o ./work/work_lib.o");
	sprintf(str, "gcc -shared -g -W -o ./work/%s.so ./work/work_lib.o -L./ElasticPL -L./crypto -lElasticPLFunctions -lcrypto", lib_name);
	ret = system(str);
#endif
#endif
#endif

	return true;
}

void create_instance(struct instance* inst, char *work_str) {
	char lib_name[50], file_name[100];

	sprintf(lib_name, "job_%s", work_str);

#ifdef WIN32
	sprintf(file_name, "./work/%s.dll", lib_name);
	inst->hndl = LoadLibrary(file_name);
	if (!inst->hndl) {
		fprintf(stderr, "Unable to load library: '%s' (Error - %d)", file_name, GetLastError());
		exit(EXIT_FAILURE);
	}
	inst->initialize = (int32_t(__cdecl *)(uint32_t *, int32_t *, uint32_t *, int64_t *, uint64_t *, float *, double *, uint32_t *))GetProcAddress((HMODULE)inst->hndl, "initialize");
	inst->execute = (int32_t(__cdecl *)(uint64_t, uint32_t *, uint32_t, uint32_t *, uint32_t *, uint32_t *))GetProcAddress((HMODULE)inst->hndl, "execute");
	inst->verify = (int32_t(__cdecl *)(uint64_t, uint32_t *, uint32_t, uint32_t *, uint32_t *, uint32_t *))GetProcAddress((HMODULE)inst->hndl, "verify");
	if (!inst->initialize || !inst->execute || !inst->verify) {
			fprintf(stderr, "Unable to find library functions");
		FreeLibrary((HMODULE)inst->hndl);
		exit(EXIT_FAILURE);
	}
#else
	sprintf(file_name, "./work/%s.so", lib_name);
	inst->hndl = dlopen(file_name, RTLD_GLOBAL | RTLD_NOW);
	if (!inst->hndl) {
		fprintf(stderr, "%sn", dlerror());
		exit(EXIT_FAILURE);
	}
	inst->initialize = dlsym(inst->hndl, "initialize");
	inst->execute = dlsym(inst->hndl, "execute");
	inst->verify = dlsym(inst->hndl, "verify");
	if (!inst->initialize || !inst->execute || !inst->verify) {
		fprintf(stderr, "Unable to find library functions");
		dlclose(inst->hndl);
		exit(EXIT_FAILURE);
	}
#endif
	applog(LOG_DEBUG, "DEBUG: Library '%s' Loaded", lib_name);
}

void free_library(struct instance* inst) {
	if (inst->hndl != 0) {
#ifdef WIN32
		FreeLibrary((HMODULE)inst->hndl);
#else
		dlclose(inst->hndl);
#endif
		inst->hndl = 0;
		inst->initialize = 0;
		inst->execute = 0;
	}
}

/*
* The md5 kernel was heavily inspired by the md5 kernel in john the ripper
* community enhanced version. See https://github.com/magnumripper/JohnTheRipper
*
* Original software copyright (c) 2010, Dhiru Kholia
* <dhiru.kholia at gmail.com>,
* and it is hereby released to the general public under the following terms:
* Redistribution and use in source and binary forms, with or without modification,
* are permitted.
*
* Useful References:
* 1. CUDA MD5 Hashing Experiments, http://majuric.org/software/cudamd5/
* 2. oclcrack, http://sghctoma.extra.hu/index.php?p=entry&id=11
* 3. http://people.eku.edu/styere/Encrypt/JS-MD5.html
* 4. http://en.wikipedia.org/wiki/MD5#Algorithm
*/

extern bool create_opencl_source(char *work_str) {
	char *code = NULL, filename[50];
	FILE* f;

	sprintf(filename, "./work/job_%s.cl", work_str);

	f = fopen(filename, "w");
	if (!f)
		return false;

	fprintf(f, "#pragma OPENCL EXTENSION cl_khr_byte_addressable_store : enable\n");
	fprintf(f, "#pragma OPENCL EXTENSION cl_khr_fp64 : enable\n\n");

	fprintf(f, "#ifndef int32_t\n#define int32_t int\n#endif\n");
	fprintf(f, "#ifndef uint32_t\n#define uint32_t uint\n#endif\n");
	fprintf(f, "#ifndef int64_t\n#define int64_t long\n#endif\n");
	fprintf(f, "#ifndef uint64_t\n#define uint64_t ulong\n#endif\n");
	fprintf(f, "#ifndef NULL\n#define NULL 0\n#endif\n\n");

	fprintf(f, "/* The basic MD5 functions */\n");
	fprintf(f, "#define F(x, y, z)          ((z) ^ ((x) & ((y) ^ (z))))\n");
	fprintf(f, "#define G(x, y, z)          ((y) ^ ((z) & ((x) ^ (y))))\n");
	fprintf(f, "#define H(x, y, z)          ((x) ^ (y) ^ (z))\n");
	fprintf(f, "#define I(x, y, z)          ((y) ^ ((x) | ~(z)))\n\n");
	fprintf(f, "/* The MD5 transformation for all four rounds. */\n");
	fprintf(f, "#define STEP(f, a, b, c, d, x, t, s) \\\n");
	fprintf(f, "\t(a) += f((b), (c), (d)) + (x) + (t); \\\n");
	fprintf(f, "\t(a) = (((a) << (s)) | (((a) & 0xffffffff) >> (32 - (s)))); \\\n");
	fprintf(f, "\t(a) += (b);\n\n");
	fprintf(f, "#define GET(i) (key[(i)])\n\n");
	fprintf(f, "// void md5_round(uint* internal_state, const uint* key);\n");
	fprintf(f, "static void md5_round(uint* internal_state, const uint* key) {\n");
	fprintf(f, "\tuint a, b, c, d;\n");
	fprintf(f, "\ta = internal_state[0];\n");
	fprintf(f, "\tb = internal_state[1];\n");
	fprintf(f, "\tc = internal_state[2];\n");
	fprintf(f, "\td = internal_state[3];\n\n");

	fprintf(f, "\t/* Round 1 */\n");
	fprintf(f, "\tSTEP(F, a, b, c, d, GET(0), 0xd76aa478, 7)\n");
	fprintf(f, "\tSTEP(F, d, a, b, c, GET(1), 0xe8c7b756, 12)\n");
	fprintf(f, "\tSTEP(F, c, d, a, b, GET(2), 0x242070db, 17)\n");
	fprintf(f, "\tSTEP(F, b, c, d, a, GET(3), 0xc1bdceee, 22)\n");
	fprintf(f, "\tSTEP(F, a, b, c, d, GET(4), 0xf57c0faf, 7)\n");
	fprintf(f, "\tSTEP(F, d, a, b, c, GET(5), 0x4787c62a, 12)\n");
	fprintf(f, "\tSTEP(F, c, d, a, b, GET(6), 0xa8304613, 17)\n");
	fprintf(f, "\tSTEP(F, b, c, d, a, GET(7), 0xfd469501, 22)\n");
	fprintf(f, "\tSTEP(F, a, b, c, d, GET(8), 0x698098d8, 7)\n");
	fprintf(f, "\tSTEP(F, d, a, b, c, GET(9), 0x8b44f7af, 12)\n");
	fprintf(f, "\tSTEP(F, c, d, a, b, GET(10), 0xffff5bb1, 17)\n");
	fprintf(f, "\tSTEP(F, b, c, d, a, GET(11), 0x895cd7be, 22)\n");
	fprintf(f, "\tSTEP(F, a, b, c, d, GET(12), 0x6b901122, 7)\n");
	fprintf(f, "\tSTEP(F, d, a, b, c, GET(13), 0xfd987193, 12)\n");
	fprintf(f, "\tSTEP(F, c, d, a, b, GET(14), 0xa679438e, 17)\n");
	fprintf(f, "\tSTEP(F, b, c, d, a, GET(15), 0x49b40821, 22)\n\n");

	fprintf(f, "\t/* Round 2 */\n");
	fprintf(f, "\tSTEP(G, a, b, c, d, GET(1), 0xf61e2562, 5)\n");
	fprintf(f, "\tSTEP(G, d, a, b, c, GET(6), 0xc040b340, 9)\n");
	fprintf(f, "\tSTEP(G, c, d, a, b, GET(11), 0x265e5a51, 14)\n");
	fprintf(f, "\tSTEP(G, b, c, d, a, GET(0), 0xe9b6c7aa, 20)\n");
	fprintf(f, "\tSTEP(G, a, b, c, d, GET(5), 0xd62f105d, 5)\n");
	fprintf(f, "\tSTEP(G, d, a, b, c, GET(10), 0x02441453, 9)\n");
	fprintf(f, "\tSTEP(G, c, d, a, b, GET(15), 0xd8a1e681, 14)\n");
	fprintf(f, "\tSTEP(G, b, c, d, a, GET(4), 0xe7d3fbc8, 20)\n");
	fprintf(f, "\tSTEP(G, a, b, c, d, GET(9), 0x21e1cde6, 5)\n");
	fprintf(f, "\tSTEP(G, d, a, b, c, GET(14), 0xc33707d6, 9)\n");
	fprintf(f, "\tSTEP(G, c, d, a, b, GET(3), 0xf4d50d87, 14)\n");
	fprintf(f, "\tSTEP(G, b, c, d, a, GET(8), 0x455a14ed, 20)\n");
	fprintf(f, "\tSTEP(G, a, b, c, d, GET(13), 0xa9e3e905, 5)\n");
	fprintf(f, "\tSTEP(G, d, a, b, c, GET(2), 0xfcefa3f8, 9)\n");
	fprintf(f, "\tSTEP(G, c, d, a, b, GET(7), 0x676f02d9, 14)\n");
	fprintf(f, "\tSTEP(G, b, c, d, a, GET(12), 0x8d2a4c8a, 20)\n\n");

	fprintf(f, "\t/* Round 3 */\n");
	fprintf(f, "\tSTEP(H, a, b, c, d, GET(5), 0xfffa3942, 4)\n");
	fprintf(f, "\tSTEP(H, d, a, b, c, GET(8), 0x8771f681, 11)\n");
	fprintf(f, "\tSTEP(H, c, d, a, b, GET(11), 0x6d9d6122, 16)\n");
	fprintf(f, "\tSTEP(H, b, c, d, a, GET(14), 0xfde5380c, 23)\n");
	fprintf(f, "\tSTEP(H, a, b, c, d, GET(1), 0xa4beea44, 4)\n");
	fprintf(f, "\tSTEP(H, d, a, b, c, GET(4), 0x4bdecfa9, 11)\n");
	fprintf(f, "\tSTEP(H, c, d, a, b, GET(7), 0xf6bb4b60, 16)\n");
	fprintf(f, "\tSTEP(H, b, c, d, a, GET(10), 0xbebfbc70, 23)\n");
	fprintf(f, "\tSTEP(H, a, b, c, d, GET(13), 0x289b7ec6, 4)\n");
	fprintf(f, "\tSTEP(H, d, a, b, c, GET(0), 0xeaa127fa, 11)\n");
	fprintf(f, "\tSTEP(H, c, d, a, b, GET(3), 0xd4ef3085, 16)\n");
	fprintf(f, "\tSTEP(H, b, c, d, a, GET(6), 0x04881d05, 23)\n");
	fprintf(f, "\tSTEP(H, a, b, c, d, GET(9), 0xd9d4d039, 4)\n");
	fprintf(f, "\tSTEP(H, d, a, b, c, GET(12), 0xe6db99e5, 11)\n");
	fprintf(f, "\tSTEP(H, c, d, a, b, GET(15), 0x1fa27cf8, 16)\n");
	fprintf(f, "\tSTEP(H, b, c, d, a, GET(2), 0xc4ac5665, 23)\n\n");

	fprintf(f, "\t/* Round 4 */\n");
	fprintf(f, "\tSTEP(I, a, b, c, d, GET(0), 0xf4292244, 6)\n");
	fprintf(f, "\tSTEP(I, d, a, b, c, GET(7), 0x432aff97, 10)\n");
	fprintf(f, "\tSTEP(I, c, d, a, b, GET(14), 0xab9423a7, 15)\n");
	fprintf(f, "\tSTEP(I, b, c, d, a, GET(5), 0xfc93a039, 21)\n");
	fprintf(f, "\tSTEP(I, a, b, c, d, GET(12), 0x655b59c3, 6)\n");
	fprintf(f, "\tSTEP(I, d, a, b, c, GET(3), 0x8f0ccc92, 10)\n");
	fprintf(f, "\tSTEP(I, c, d, a, b, GET(10), 0xffeff47d, 15)\n");
	fprintf(f, "\tSTEP(I, b, c, d, a, GET(1), 0x85845dd1, 21)\n");
	fprintf(f, "\tSTEP(I, a, b, c, d, GET(8), 0x6fa87e4f, 6)\n");
	fprintf(f, "\tSTEP(I, d, a, b, c, GET(15), 0xfe2ce6e0, 10)\n");
	fprintf(f, "\tSTEP(I, c, d, a, b, GET(6), 0xa3014314, 15)\n");
	fprintf(f, "\tSTEP(I, b, c, d, a, GET(13), 0x4e0811a1, 21)\n");
	fprintf(f, "\tSTEP(I, a, b, c, d, GET(4), 0xf7537e82, 6)\n");
	fprintf(f, "\tSTEP(I, d, a, b, c, GET(11), 0xbd3af235, 10)\n");
	fprintf(f, "\tSTEP(I, c, d, a, b, GET(2), 0x2ad7d2bb, 15)\n");
	fprintf(f, "\tSTEP(I, b, c, d, a, GET(9), 0xeb86d391, 21)\n\n");

	fprintf(f, "\tinternal_state[0] = a + internal_state[0];\n");
	fprintf(f, "\tinternal_state[1] = b + internal_state[1];\n");
	fprintf(f, "\tinternal_state[2] = c + internal_state[2];\n");
	fprintf(f, "\tinternal_state[3] = d + internal_state[3];\n");
	fprintf(f, "}\n\n");

	fprintf(f, "void md5(const char* restrict msg, uint length_bytes, uint* restrict out) {\n");
	fprintf(f, "\tuint i;\n");
	fprintf(f, "\tuint bytes_left;\n");
	fprintf(f, "\tchar key[64];\n\n");

	fprintf(f, "\tout[0] = 0x67452301;\n");
	fprintf(f, "\tout[1] = 0xefcdab89;\n");
	fprintf(f, "\tout[2] = 0x98badcfe;\n");
	fprintf(f, "\tout[3] = 0x10325476;\n\n");

	fprintf(f, "\tfor (bytes_left = length_bytes;  bytes_left >= 64;\n");
	fprintf(f, "\t\tbytes_left -= 64, msg = &msg[64]) {\n");
	fprintf(f, "\t\tmd5_round(out, (const uint*)msg);\n");
	fprintf(f, "\t}\n\n");

	fprintf(f, "\tfor (i = 0; i < bytes_left; i++) {\n");
	fprintf(f, "\t\tkey[i] = msg[i];\n");
	fprintf(f, "\t}\n");
	fprintf(f, "\tkey[bytes_left++] = 0x80;\n\n");

	fprintf(f, "\tif (bytes_left <= 56) {\n");
	fprintf(f, "\t\tfor (i = bytes_left; i < 56; key[i++] = 0);\n");
	fprintf(f, "\t}\n");
	fprintf(f, "\telse {\n");
	fprintf(f, "\t\t// If we have to pad enough to roll past this round.\n");
	fprintf(f, "\t\tfor (i = bytes_left; i < 64; key[i++] = 0);\n");
	fprintf(f, "\t\tmd5_round(out, (uint*)key);\n");
	fprintf(f, "\t\tfor (i = 0; i < 56; key[i++] = 0);\n");
	fprintf(f, "\t}\n\n");

	fprintf(f, "\tulong* len_ptr = (ulong*)&key[56];\n");
	fprintf(f, "\t*len_ptr = length_bytes * 8;\n");
	fprintf(f, "\tmd5_round(out, (uint*)key);\n");
	fprintf(f, "}\n\n");

	fprintf(f, "int gcd(int a, int b) {\n");
	fprintf(f, "\tif (a < 0) a = -a;\n");
	fprintf(f, "\tif (b < 0) b = -b;\n");
	fprintf(f, "\twhile (b != 0) {\n");
	fprintf(f, "\t\ta %%= b;\n");
	fprintf(f, "\t\tif (a == 0) return b;\n");
	fprintf(f, "\t\tb %%= a;\n");
	fprintf(f, "\t}\n");
	fprintf(f, "\treturn a;\n");
	fprintf(f, "}\n\n");

	fprintf(f, "uint swap32(uint a) {\n");
	fprintf(f, "\treturn ((a << 24) | ((a << 8) & 0x00FF0000) | ((a >> 8) & 0x0000FF00) | ((a >> 24) & 0x000000FF));\n");
	fprintf(f, "}\n\n");

	fprintf(f, "static uint rotl32(uint x, uint n) {\n");
	fprintf(f, "\tn &= 0x0000001f;  // avoid undef behaviour with NDEBUG.  0 overhead for most types / compilers\n");
	fprintf(f, "\treturn (x<<n) | (x>>( (-n) & 0x0000001f ));\n");
	fprintf(f, "}\n\n");

	fprintf(f, "static uint rotr32(uint x, uint n) {\n");
	fprintf(f, "\tn &= 0x0000001f;  // avoid undef behaviour with NDEBUG.  0 overhead for most types / compilers\n");
	fprintf(f, "\treturn (x>>n) | (x<<( (-n) & 0x0000001f ));\n");
	fprintf(f, "}\n\n");

	fprintf(f, "static uint check_pow(uint msg_0, uint msg_1, uint msg_2, uint msg_3, uint *m, uint *target, uint *hash) {\n");
	fprintf(f, "\tint i;\n");
	fprintf(f, "\tchar msg[48];\n");
	fprintf(f, "\tuint32_t *msg32 = (uint32_t *)(msg);\n\n");
	fprintf(f, "\tmsg32[0] = msg_0;\n");
	fprintf(f, "\tmsg32[1] = msg_1;\n");
	fprintf(f, "\tmsg32[2] = msg_2;\n");
	fprintf(f, "\tmsg32[3] = msg_3;\n\n");
	fprintf(f, "\tfor (i = 0; i < 8; i++)\n");
	fprintf(f, "\t\tmsg32[i+4] = m[i];\n\n");
	fprintf(f, "\tmd5((char*)&msg[0], 48, &hash[0]);\n\n");
	fprintf(f, "\tfor (i = 0; i < 4; i++) {\n");
	fprintf(f, "\t\tif (hash[i] > target[i])\n");
	fprintf(f, "\t\t\treturn 0;\n");
	fprintf(f, "\t\telse if (hash[i] < target[i])\n");
	fprintf(f, "\t\t\treturn 1;    // POW Solution Found\n");
	fprintf(f, "\t}\n");
	fprintf(f, "\treturn 1;\n");
	fprintf(f, "}\n\n");
	fflush(f);

	if (!convert_ast_to_opencl(f))
		return false;

	fflush(f);
	fclose(f);
	if (code != NULL)
		free(code);
	return true;
}
