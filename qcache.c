/*
  +----------------------------------------------------------------------+
  | qcache                                                                |
  +----------------------------------------------------------------------+
  | Copyright (c) 2007 The PHP Group                                     |
  +----------------------------------------------------------------------+
  | This source file is subject to version 3.01 of the PHP license,      |
  | that is bundled with this package in the file LICENSE, and is        |
  | available through the world-wide-web at the following url:           |
  | http://www.php.net/license/3_01.txt                                  |
  | If you did not receive a copy of the PHP license and are unable to   |
  | obtain it through the world-wide-web, please send a note to          |
  | license@php.net so we can mail you a copy immediately.               |
  +----------------------------------------------------------------------+
  | Authors: qxc <qinxiuchen@gmail.com>                       |
  +----------------------------------------------------------------------+
*/


#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "php.h"
#include "php_ini.h"
#include "php_scandir.h"
#include "zend_globals.h"
#include "zend_ini_scanner.h"
#include "zend_hash.h"
#include "ext/standard/info.h"
#include "SAPI.h"
#include "malloc.h"
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <time.h>
#include <sys/stat.h>

#include "php_qcache.h"
#include "frozenarray.h"

typedef int (*qcache_walk_dir_cb)(const char* filename, void* ctxt TSRMLS_DC);

/* {{{ stupid stringifcation */
#if DEFAULT_SLASH == '/'
	#define DEFAULT_SLASH_STRING "/"
#elif DEFAULT_SLASH == '\\'
	#define DEFAULT_SLASH_STRING "\\"
#else
	#error "Unknown value for DEFAULT_SLASH"
#endif
/* }}} */


/* {{{ qcache globals 
 *
 * true globals, no need for thread safety here 
 */
//static HashTable *qcache_data_hash = NULL;
static HashTable *qcache_data_hash_temp = NULL;
//文件夹上次被修改的最新时间
static time_t lmt = NULL;
//static HashTable *qcache_data_hash_temp1 = NULL;
//static int qcache_data_shmid;
/* }}} */

/* {{{ PHP_FUNCTION declarations */
PHP_FUNCTION(qcache_fetch);
PHP_FUNCTION(qcache_fetch_child);
PHP_FUNCTION(qcache_reload);
/* }}} */

/* {{{ ZEND_DECLARE_MODULE_GLOBALS(qcache) */
ZEND_DECLARE_MODULE_GLOBALS(qcache)
/* }}} */

/* {{{ php_qcache_init_globals */
static void php_qcache_init_globals(zend_qcache_globals* qcache_globals TSRMLS_DC)
{
	qcache_globals->data_path = NULL;
}
/* }}} */

/* {{{ php_qcache_shutdown_globals */
static void php_qcache_shutdown_globals(zend_qcache_globals* qcache_globals TSRMLS_DC)
{
	/* nothing ? */
}
/* }}} */

/* {{{ ini entries */
PHP_INI_BEGIN()
STD_PHP_INI_ENTRY("qcache.data_path", (char*)NULL,              PHP_INI_SYSTEM, OnUpdateString,       data_path,  zend_qcache_globals, qcache_globals)

PHP_INI_END()
/* }}} */

/* {{{ arginfo static macro */
#if PHP_MAJOR_VERSION > 5 || PHP_MAJOR_VERSION == 5 && PHP_MINOR_VERSION >= 3
#define QCACHE_ARGINFO_STATIC
#else
#define QCACHE_ARGINFO_STATIC static
#endif
/* }}} */

/* {{{ arginfo */
QCACHE_ARGINFO_STATIC
ZEND_BEGIN_ARG_INFO(php_qcache_fetch_arginfo, 0)
    ZEND_ARG_INFO(0, key)
    ZEND_ARG_INFO(0, thaw)
ZEND_END_ARG_INFO()

QCACHE_ARGINFO_STATIC
ZEND_BEGIN_ARG_INFO(php_qcache_fetch_child_arginfo, 0)
    ZEND_ARG_INFO(0, parent_name)
    ZEND_ARG_INFO(0, child_name)
    ZEND_ARG_INFO(0, thaw)
ZEND_END_ARG_INFO()

QCACHE_ARGINFO_STATIC
ZEND_BEGIN_ARG_INFO(php_qcache_reload_arginfo, 0)
ZEND_END_ARG_INFO()
/* }}} */


/* {{{ qcache_functions[]
 *
 * Every user visible function must have an entry in qcache_functions[].
 */
zend_function_entry qcache_functions[] = {
	PHP_FE(qcache_fetch,               php_qcache_fetch_arginfo)
	PHP_FE(qcache_fetch_child,         php_qcache_fetch_child_arginfo)
	PHP_FE(qcache_reload,              php_qcache_reload_arginfo)
	{NULL, NULL, NULL}	/* Must be the last line in qcache_functions[] */
};
/* }}} */

/* {{{ qcache_module_entry
 */
zend_module_entry qcache_module_entry = {
	STANDARD_MODULE_HEADER,
	"qcache",
	qcache_functions,
	PHP_MINIT(qcache),
	PHP_MSHUTDOWN(qcache),
	PHP_RINIT(qcache),
	PHP_RSHUTDOWN(qcache),
	PHP_MINFO(qcache),
#if ZEND_MODULE_API_NO >= 20010901
	PHP_QCACHE_VERSION,
#endif
	STANDARD_MODULE_PROPERTIES
};
/* }}} */

#ifdef COMPILE_DL_QCACHE
ZEND_GET_MODULE(qcache)
#endif

/* {{{ struct qcache_parser_ctxt */
typedef struct _qcache_parser_ctxt 
{
	int module_number;
	const char * filename;
	int type;
	int flags;
} qcache_parser_ctxt;
/* }}} */

/* {{{ qcache_zval_pfree */
static void qcache_zval_pfree(void *p) 
{
	TSRMLS_FETCH();
	frozen_array_free_zval_ptr((zval**)p, 1 TSRMLS_CC);
}
/* }}} */


/* {{{ qcache_walk_dir */
static int qcache_walk_dir(const char *path, const char *ext, qcache_walk_dir_cb cb, void *ctxt TSRMLS_DC)
{
	char file[MAXPATHLEN]={0,};
	int ndir, i, k;
	char *p = NULL;
	struct dirent **namelist = NULL;

	if ((ndir = php_scandir(path, &namelist, 0, php_alphasort)) > 0)
	{
		for (i = 0; i < ndir; i++) 
		{
			/* check for extension */
			if (!(p = strrchr(namelist[i]->d_name, '.')) 
					|| (p && strcmp(p, ext))) 
			{
				free(namelist[i]);
				continue;
			}
			snprintf(file, MAXPATHLEN, "%s%c%s", 
					path, DEFAULT_SLASH, namelist[i]->d_name);
			if(!cb(file, ctxt TSRMLS_CC))
			{
				goto cleanup;
			}
			free(namelist[i]);
		}
		free(namelist);
	}

	return 1;

cleanup:
	for(k = i; k < ndir; k++)
	{
		free(namelist[k]);
	}
	free(namelist);

	return 0;
}
/* }}} */

/* }}} */

/* {{{ qcache_load_data */
static int qcache_load_data(const char *data_file, void* pctxt TSRMLS_DC)
{
	char *p;
	char key[MAXPATHLEN] = {0,};
	unsigned int key_len;
	zval *data;

	if(access(data_file, R_OK) != 0) 
	{
		/* maybe a broken symlink (skip and continue) */
		zend_error(E_WARNING, "qcache cannot read %s", data_file);
		return 1;
	}

	p = strrchr(data_file, DEFAULT_SLASH);

	if(p && p[1])
	{
		strlcpy(key, p+1, sizeof(key));
		p = strrchr(key, '.');

		if(p)
		{
			p[0] = '\0';
			key_len = strlen(key);

			zend_try 
			{
				data = frozen_array_unserialize(data_file TSRMLS_CC);
			} zend_catch
			{
				zend_error(E_ERROR, "Data corruption in %s, bailing out", data_file);
				zend_bailout();
			} zend_end_try();
			//if((data == NULL) || zend_hash_update(qcache_data_hash, key, key_len+1, &data, sizeof(void*), NULL) == FAILURE)
			//{
			//	zend_error(E_ERROR, "Unable to add %s to the qcache data hash", data_file);
			//	return 0;
			//}
			//qcache_data_hash_temp = (HashTable *)shmat(qcache_data_shmid,NULL,0 );

			//	zend_error(E_ERROR, "some!%d", qcache_data_shmid);
			if((data == NULL) || zend_hash_update(qcache_data_hash_temp, key, key_len+1, &data, sizeof(void*), NULL) == FAILURE)
			{
				zend_error(E_ERROR, "Unable to add %s to the qcache data hash", data_file);
				return 0;
			}
			//修改时间
			const char *data_path = NULL;

			if(QCACHE_G(data_path)) 
			{
				data_path = QCACHE_G(data_path);
			}
			//获取该文件夹的状态信息，主要是最近修改时间
			struct stat statBuf;
			if (stat(data_path, &statBuf))
			{
				zend_error(E_ERROR, "can't stat file %s", data_path);
				return NULL;
			}
			lmt = statBuf.st_mtime;
			
			return 1;
		}
	}

	return 0;
}
/* }}} */

/* {{{ qcache_read_data */
static int qcache_read_data(qcache_parser_ctxt *ctxt TSRMLS_DC)
{
	const char *data_path = NULL;

	if(QCACHE_G(data_path)) 
	{
		data_path = QCACHE_G(data_path);
	}
	else
	{
		return 0;
	}

	return qcache_walk_dir(data_path, ".data", qcache_load_data, ctxt TSRMLS_CC);
}
/* }}} */

/* {{{ PHP_MINIT_FUNCTION
 */
PHP_MINIT_FUNCTION(qcache)
{
	qcache_parser_ctxt ctxt = {0,};
	long memory_limit = 0;

	ZEND_INIT_MODULE_GLOBALS(qcache, php_qcache_init_globals, php_qcache_shutdown_globals);

	REGISTER_INI_ENTRIES();
	
	ctxt.flags = CONST_CS | CONST_PERSISTENT;
#ifdef CONST_CT_SUBST
	ctxt.flags |= CONST_CT_SUBST;
#endif

	ctxt.module_number = module_number;

//	qcache_data_shmid = pemalloc(sizeof(int),1);

	//分配共享内存得到一个全局标识
//	qcache_data_shmid = shmget(IPC_PRIVATE,sizeof(HashTable),IPC_CREAT|IPC_EXCL|0660);
//	memcpy(qcache_data_shmid, &shmid, sizeof(int));
	
	qcache_data_hash_temp = pemalloc(sizeof(HashTable),1);
//	qcache_data_hash = (HashTable *)shmat(qcache_data_shmid,NULL,0 );
//	zend_hash_init(qcache_data_hash, 	  32,  NULL, NULL, 1);
//	qcache_data_hash_temp = (HashTable *)shmat(qcache_data_shmid,NULL,0 );
	zend_hash_init(qcache_data_hash_temp, 	  32,  NULL, NULL, 1);
//	zend_error(E_WARNING, "qcache cannot read %d", qcache_data_shmid);
//	qcache_data_shmid = shmget(IPC_PRIVATE,sizeof(HashTable),IPC_CREATE|IPC_EXCL|0660);
//	if(*qcache_data_shmid == -1){
//		zend_error("shmget error!");
//		return FAILURE;
//	}
	memory_limit = PG(memory_limit);

	zend_set_memory_limit((size_t) (256 * 1024 * 1024)); /* 256 MB sounds sane right now */

	qcache_read_data(&ctxt TSRMLS_CC);

	zend_set_memory_limit((size_t)memory_limit); /* reset the memory limit */
	
	frozen_array_init(TSRMLS_C);

#ifdef ZTS
	QCACHE_G(p_tid) = tsrm_thread_id();
#else
	QCACHE_G(p_pid) = getpid();
#endif

#ifdef HAVE_MALLOC_TRIM
	malloc_trim(0); /* cleanup pages */
#endif
	//shmdt(qcache_data_hash_temp) ;
	return SUCCESS;
}
/* }}} */

/* {{{ PHP_MSHUTDOWN_FUNCTION
 *  */
PHP_MSHUTDOWN_FUNCTION(qcache)
{
#ifdef ZTS
	THREAD_T tid = tsrm_thread_id();
	THREAD_T p_tid = QCACHE_G(p_tid);
	#define IS_ALLOC_THREAD() (memcmp(&(p_tid), &tid, sizeof(THREAD_T))==0)
#else
	pid_t pid = getpid();
	pid_t p_pid = QCACHE_G(p_pid);
	#define IS_ALLOC_THREAD() (p_pid == pid) 
#endif

	if(IS_ALLOC_THREAD())
	{

	}

#ifdef ZTS
	ts_free_id(qcache_globals_id);
#else
	php_qcache_shutdown_globals(&qcache_globals);
#endif

	UNREGISTER_INI_ENTRIES();
	return SUCCESS;
}
/* }}} */

/* {{{ PHP_RINIT_FUNCTION(qcache) */
PHP_RINIT_FUNCTION(qcache)
{
	//首先获取数据文件夹的相关目录
	const char *data_path = NULL;

	if(QCACHE_G(data_path)) 
	{
		data_path = QCACHE_G(data_path);
	}
	//获取该文件夹的状态信息，主要是最近修改时间
	struct stat statBuf;
	if (stat(data_path, &statBuf))
	{
		zend_error(E_ERROR, "can't stat file %s", data_path);
		return NULL;
	}
	time_t cur_lmt = statBuf.st_mtime;
	double dif = cur_lmt - lmt;
	if(dif > 0){
		qcache_parser_ctxt ctxt = {0,};
		//zend_hash_destroy(qcache_data_hash);
		//重新加载数据到qcache_data_hash
		qcache_read_data(&ctxt TSRMLS_CC);
	}
	return SUCCESS;
}
/* }}} */

/* {{{ PHP_RSHUTDOWN_FUNCTION(qcache) */
PHP_RSHUTDOWN_FUNCTION(qcache)
{
	#ifdef HAVE_MALLOC_TRIM
		malloc_trim(0);
	#endif
	return SUCCESS;
}
/* }}} */


/* {{{ PHP_MINFO_FUNCTION
 */
PHP_MINFO_FUNCTION(qcache)
{
	const char * data_path = "disabled";
	HashPosition pos;
	zend_constant *val;
	int module_number = zend_module->module_number;
	
	php_info_print_table_start();

	if(QCACHE_G(data_path)) 
	{
		data_path = QCACHE_G(data_path);	
	}
	
	
	php_info_print_table_header(2, "qcache support", "enabled");
	php_info_print_table_row(2, "version", PHP_QCACHE_VERSION);
	php_info_print_table_row(2, "data search path", data_path);
#ifdef CONST_CT_SUBST
	php_info_print_table_row(2, "substitution mode", "compile time");
#else
	php_info_print_table_row(2, "substitution mode", "runtime");
#endif

	php_info_print_table_end();
	php_info_print_table_start();
	php_info_print_table_header(2, "Constant", "Value");

	
	zend_hash_internal_pointer_reset_ex(EG(zend_constants), &pos);
	while (zend_hash_get_current_data_ex(EG(zend_constants), (void **) &val, &pos) != FAILURE) 
	{
		if (val->module_number == module_number) 
		{
			zval const_val = {{0,},};
			const_val = val->value;
			zval_copy_ctor(&const_val);
			convert_to_string(&const_val);
			
			php_info_print_table_row(2, val->name, Z_STRVAL_P(&const_val));
			zval_dtor(&const_val);
		}
		zend_hash_move_forward_ex(EG(zend_constants), &pos);
	}

	php_info_print_table_end();
}
/* }}} */

/* {{{ proto mixed qcache_fetch(string key [, bool thaw])
 */
PHP_FUNCTION(qcache_fetch) 
{
	zval **hentry;
	zval *wrapped;
	char *strkey;
	int strkey_len;
	zend_bool thaw = 0;

	if(zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "s|b", &strkey, &strkey_len, &thaw) == FAILURE)
	{
		return;
	}

	if(zend_hash_find(qcache_data_hash_temp, strkey, strkey_len+1, (void**)&hentry) == FAILURE)
	{
		return;
	}

	if(thaw)
	{
		wrapped = frozen_array_copy_zval_ptr(NULL, hentry[0], 0, NULL TSRMLS_CC);
	}
	else
	{
		wrapped = frozen_array_wrap_zval(hentry[0] TSRMLS_CC);
	}

	RETURN_ZVAL(wrapped, 0, 1);
}

/* {{{ proto mixed qcache_fetch_child(string parent_name, string child_name [, bool thaw])
 */
PHP_FUNCTION(qcache_fetch_child) 
{
	zval **hentry;
	zval *wrapped;
	zval *src;
	zval **zvalue;
	char *parent_name;
	int parent_name_len;
	char *child_name;
	int child_name_len;
	zend_bool thaw = 0;

	if(zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "s|s|b", &parent_name, &parent_name_len, &child_name, &child_name_len, &thaw) == FAILURE)
	{
		RETURN_NULL();
	}

	if(zend_hash_find(qcache_data_hash_temp, parent_name, parent_name_len+1, (void**)&hentry) == FAILURE)
	{
		RETURN_NULL();
	}

	src = hentry[0];

	if (child_name && zend_hash_find(Z_ARRVAL_P(src), child_name, child_name_len + 1, (void**)&zvalue) == FAILURE) {
		RETURN_NULL();
  }
	*return_value = **zvalue;
  zval_copy_ctor(return_value);
}

/* }}} */

/* {{{ proto FrozenArray qcache_reload()
 * 重新加载数据到qcache_data_hash中
 */
PHP_FUNCTION(qcache_reload) 
{
	qcache_parser_ctxt ctxt = {0,};
	//zend_hash_destroy(qcache_data_hash);
	//重新加载数据到qcache_data_hash
	qcache_read_data(&ctxt TSRMLS_CC);
	return SUCCESS;
}
/* }}} */

/*
 * Local variables:
 * tab-width: 4
 * c-basic-offset: 4
 * End:
 * vim>600: noet sw=4 ts=4 fdm=marker
 * vim<600: noet sw=4 ts=4
 */
