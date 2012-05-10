#ifndef PHP_QCACHE_H
#define PHP_QCACHE_H

extern zend_module_entry qcache_module_entry;
#define phpext_qcache_ptr &qcache_module_entry

#define PHP_QCACHE_VERSION "1.0"

#ifdef PHP_WIN32
#define PHP_QCACHE_API __declspec(dllexport)
#else
#define PHP_QCACHE_API
#endif

#ifdef ZTS
#include "TSRM.h"
#endif

#if ZEND_MODULE_API_NO > 20060613
#define ZEND_ENGINE_2_3
#endif

#if PHP_MAJOR_VERSION == 5 && PHP_MINOR_VERSION == 3 && PHP_RELEASE_VERSION < 2
#define ZEND_BROKEN_INI_SCANNER 1
#endif

/* b/c for the new macros */
#ifndef Z_REFCOUNT_P
#define Z_REFCOUNT_P(pz)              (pz)->refcount
#define Z_REFCOUNT_PP(ppz)            Z_REFCOUNT_P(*(ppz))
#endif

#ifndef Z_SET_REFCOUNT_P
#define Z_SET_REFCOUNT_P(pz, rc)      (pz)->refcount = rc
#define Z_SET_REFCOUNT_PP(ppz, rc)    Z_SET_REFCOUNT_P(*(ppz), rc)
#endif

#ifndef Z_ADDREF_P
#define Z_ADDREF_P(pz)                (pz)->refcount++
#define Z_ADDREF_PP(ppz)              Z_ADDREF_P(*(ppz))
#endif

#ifndef Z_DELREF_P
#define Z_DELREF_P(pz)                (pz)->refcount--
#define Z_DELREF_PP(ppz)              Z_DELREF_P(*(ppz))
#endif

#ifndef Z_ISREF_P
#define Z_ISREF_P(pz)                 (pz)->is_ref
#define Z_ISREF_PP(ppz)               Z_ISREF_P(*(ppz))
#endif

#ifndef Z_SET_ISREF_P
#define Z_SET_ISREF_P(pz)             (pz)->is_ref = 1
#define Z_SET_ISREF_PP(ppz)           Z_SET_ISREF_P(*(ppz))
#endif

#ifndef Z_UNSET_ISREF_P
#define Z_UNSET_ISREF_P(pz)           (pz)->is_ref = 0
#define Z_UNSET_ISREF_PP(ppz)         Z_UNSET_ISREF_P(*(ppz))
#endif

#ifndef Z_SET_ISREF_TO_P
#define Z_SET_ISREF_TO_P(pz, isref)   (pz)->is_ref = isref
#define Z_SET_ISREF_TO_PP(ppz, isref) Z_SET_ISREF_TO_P(*(ppz), isref)
#endif

PHP_MINIT_FUNCTION(qcache);
PHP_RINIT_FUNCTION(qcache);
PHP_MSHUTDOWN_FUNCTION(qcache);
PHP_RSHUTDOWN_FUNCTION(qcache);
PHP_MINFO_FUNCTION(qcache);

PHP_FUNCTION(qcache_fetch);
PHP_FUNCTION(qcache_fetch_child);
PHP_FUNCTION(qcache_reload);

zval* frozen_array_copy_zval_ptr(zval *dst, zval *src, int persistent);
void* frozen_array_alloc(size_t size, int persistent);
HashTable* frozen_array_copy_hashtable(HashTable* dst, HashTable* src, int persistent);
zval* frozen_array_unserialize(const char* filename);

int qcache_load_data(char *data_file);
int qcache_walk_dir(char *path, char *ext);
int qcache_read_data();


ZEND_BEGIN_MODULE_GLOBALS(qcache)
	char *data_path;	/* 存放.data文件的路径 */
#ifdef ZTS
	THREAD_T p_tid;		/* 父进程的进程id */
#else
	pid_t p_pid;		/* 线程id */
#endif
	char* per_request_ini; /* 每个进程的请求次数统计 */
ZEND_END_MODULE_GLOBALS(qcache)

ZEND_EXTERN_MODULE_GLOBALS(qcache)

#ifdef ZTS
#define QCACHE_G(v) TSRMG(qcache_globals_id, zend_qcache_globals *, v)
#else
#define QCACHE_G(v) (qcache_globals.v)
#endif

#endif

