/*
  +----------------------------------------------------------------------+
  | qcache                                                                |
  +----------------------------------------------------------------------+
  | Copyright (c) 2012 The PHP Group                                     |
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
#include "zend.h"
#include "php_qcache.h"
#include "php_ini.h"
#include "php_scandir.h"
#include "zend_globals.h"
#include "zend_interfaces.h"
#include "zend_exceptions.h"
#include "php_streams.h"
#include "zend_ini_scanner.h"
#include "zend_hash.h"
#include "ext/standard/info.h"
#include "SAPI.h"
#include "malloc.h"
#include "ext/standard/php_var.h"
#include "ext/standard/php_smart_str.h"
#include "ext/standard/php_incomplete_class.h"
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/stat.h>
#include <stdbool.h>

#if DEFAULT_SLASH == '/'
	#define DEFAULT_SLASH_STRING "/"
#elif DEFAULT_SLASH == '\\'
	#define DEFAULT_SLASH_STRING "\\"
#else
	#error "Unknown value for DEFAULT_SLASH"
#endif

int shmid;
void *shm_start_addr;
void *shm_cur_addr;
static HashTable *qcache_data_hash = NULL;

ZEND_DECLARE_MODULE_GLOBALS(qcache)

static void php_qcache_init_globals(zend_qcache_globals* qcache_globals TSRMLS_DC)
{
	qcache_globals->data_path = NULL;
}

static void php_qcache_shutdown_globals(zend_qcache_globals* qcache_globals TSRMLS_DC)
{
	/* nothing to do */
}

PHP_INI_BEGIN()
STD_PHP_INI_ENTRY("qcache.data_path", (char*)NULL, PHP_INI_SYSTEM, OnUpdateString, data_path, zend_qcache_globals, qcache_globals)
PHP_INI_END()

#if PHP_MAJOR_VERSION > 5 || PHP_MAJOR_VERSION == 5 && PHP_MINOR_VERSION >= 3
#define QCACHE_ARGINFO_STATIC
#else
#define QCACHE_ARGINFO_STATIC static
#endif

QCACHE_ARGINFO_STATIC
ZEND_BEGIN_ARG_INFO(php_qcache_fetch_arginfo, 0)
    ZEND_ARG_INFO(0, key)
ZEND_END_ARG_INFO()

QCACHE_ARGINFO_STATIC
ZEND_BEGIN_ARG_INFO(php_qcache_fetch_child_arginfo, 0)
    ZEND_ARG_INFO(0, parent_name)
    ZEND_ARG_INFO(0, child_name)
ZEND_END_ARG_INFO()

QCACHE_ARGINFO_STATIC
ZEND_BEGIN_ARG_INFO(php_qcache_reload_arginfo, 0)
ZEND_END_ARG_INFO()

zend_function_entry qcache_functions[] = {
	PHP_FE(qcache_fetch,               php_qcache_fetch_arginfo)
	PHP_FE(qcache_fetch_child,         php_qcache_fetch_child_arginfo)
	PHP_FE(qcache_reload,              php_qcache_reload_arginfo)
	{NULL, NULL, NULL}
};

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

#ifdef COMPILE_DL_QCACHE
ZEND_GET_MODULE(qcache)
#endif

void* frozen_array_alloc(size_t size,int persistent){
	if(persistent){
		void* cur_shm = shm_cur_addr;
		shm_cur_addr += size;
		return cur_shm;
	}else{
		return pemalloc(size, persistent);
	}
}

zval* frozen_array_unserialize(const char* filename){
	zval* data;
	zval* retval;
	long len = 0;
	struct stat sb;
	char *contents, *tmp;
	FILE *fp;
	php_unserialize_data_t var_hash;
	HashTable class_table = {0,};
	if(VCWD_STAT(filename, &sb) == -1){
		return NULL;
	}
	fp = fopen(filename, "rb");
	if((!fp) || (sb.st_size == 0)){
		return NULL;
	}
	len = sizeof(char)*sb.st_size;
	tmp = contents = malloc(len);
	len = fread(contents, 1, len, fp);
	MAKE_STD_ZVAL(data);
	PHP_VAR_UNSERIALIZE_INIT(var_hash);
	if(!php_var_unserialize(&data, (const unsigned char**)&tmp, contents+len, &var_hash TSRMLS_CC)){
		zval_ptr_dtor(&data);
		free(contents);
		fclose(fp);
		return NULL;
	}
	PHP_VAR_UNSERIALIZE_DESTROY(var_hash);
	retval = frozen_array_copy_zval_ptr(NULL, data, 1);
	zval_ptr_dtor(&data);
	free(contents);
	fclose(fp);
	return retval;
}
HashTable* frozen_array_copy_hashtable(HashTable* dst, HashTable* src, int persistent){
	Bucket* curr = NULL;
	Bucket* prev = NULL;
	Bucket* newp = NULL;
	int first = 1;
	if (!dst){
		dst = (HashTable*) frozen_array_alloc(sizeof(HashTable),persistent);
	}
	memcpy(dst, src, sizeof(HashTable));
	dst->arBuckets = frozen_array_alloc(sizeof(Bucket*) * dst->nTableSize,persistent);
	if(!persistent){
		dst->persistent = persistent;
		dst->pDestructor = ZVAL_PTR_DTOR;
	}else{
		dst->persistent = persistent;
		dst->pDestructor = NULL;
	}
	memset(dst->arBuckets, 0, dst->nTableSize * sizeof(Bucket*));
	dst->pInternalPointer = NULL;
	dst->pListHead = NULL;
	for (curr = src->pListHead; curr != NULL; curr = curr->pListNext){
		int n = curr->h % dst->nTableSize;
		newp = frozen_array_alloc((sizeof(Bucket) + curr->nKeyLength - 1),persistent);
		memcpy(newp, curr, sizeof(Bucket) + curr->nKeyLength - 1);
		if (dst->arBuckets[n]){
			newp->pNext = dst->arBuckets[n];
			newp->pLast = NULL;
			newp->pNext->pLast = newp;
		}else{
			newp->pNext = newp->pLast = NULL;
		}
		dst->arBuckets[n] = newp;
		newp->pDataPtr = frozen_array_copy_zval_ptr(NULL, curr->pDataPtr,persistent);
		newp->pData = &newp->pDataPtr;
		newp->pListLast = prev;
		newp->pListNext = NULL;
		if (prev) {
			prev->pListNext = newp;
		}
		if (first) {
			dst->pListHead = newp;
			first = 0;
		}
		prev = newp;
	}
	dst->pListTail = newp;
	return dst;
}
zval* frozen_array_copy_zval_ptr(zval* dst, zval* src, int persistent){
	if(!dst){
		if(persistent){
			dst = frozen_array_alloc(sizeof(zval),persistent);
		}else{
			MAKE_STD_ZVAL(dst);
		}
	}
	memcpy(dst, src, sizeof(zval));
	switch (src->type){
		case IS_RESOURCE:
		case IS_BOOL:
		case IS_LONG:
		case IS_DOUBLE:
		case IS_NULL:
			break;
		case IS_CONSTANT:
		case IS_STRING:
		{
			if (Z_STRVAL_P(src)){
				Z_STRVAL_P(dst) = frozen_array_alloc(sizeof(char)*Z_STRLEN_P(src)+1, persistent);
				memcpy(Z_STRVAL_P(dst), Z_STRVAL_P(src), Z_STRLEN_P(src)+1);
			}
		}
		break;
		case IS_ARRAY:
		case IS_CONSTANT_ARRAY:
		{
			if(!Z_ISREF_P(src)){
				Z_ARRVAL_P(dst) = frozen_array_copy_hashtable(NULL, Z_ARRVAL_P(src),persistent);
			}else{
				dst->type = IS_STRING;
				Z_SET_REFCOUNT_P(dst, 1);
				Z_UNSET_ISREF_P(dst);
				Z_STRVAL_P(dst) = pestrdup("**RECURSION**", persistent);
				Z_STRLEN_P(dst) = sizeof("**RECURSION**")-1;
			}
		}
		break;
		case IS_OBJECT:
		{
			dst->type = IS_NULL;
			Z_SET_REFCOUNT_P(dst, 1);
			Z_UNSET_ISREF_P(dst);
			if(persistent){
				zend_class_entry *zce = Z_OBJCE_P(src);
				char *class_name = NULL;
				zend_uint class_name_len;
				if(zce && zce == PHP_IC_ENTRY){
					class_name = php_lookup_class_name(src, &class_name_len);
				}else if(zce && Z_OBJ_HANDLER_P(src, get_class_name)){
					Z_OBJ_HANDLER_P(src, get_class_name)(src, &class_name, &class_name_len, 0 TSRMLS_CC);
				}
				zend_error(E_ERROR, "Unknown object of type '%s' found in serialized hash", class_name ? class_name : "Unknown");
				if(class_name) efree(class_name);
				zend_bailout();
			}
		}
		break;
		default:
			assert(0);
	}
	return dst;
}

bool checkInt(char* p){
    int len = strlen(p);
    bool result = true;
    while(len > 0){
        if(*p < '0' || *p > '9'){
            result = false;
            break;
        }
        p++;
        len--;
    }
    return result;
}
int qcache_load_data(char *data_file){
	char *p;
	char key[MAXPATHLEN] = {0,};
	unsigned int key_len;
	zval *data;
	if(access(data_file, R_OK) != 0) {
		zend_error(E_WARNING, "qcache cannot read %s", data_file);
		return 1;
	}
	p = strrchr(data_file, DEFAULT_SLASH);
	if(p && p[1]){
		strlcpy(key, p+1, sizeof(key));
		p = strrchr(key, '.');
		if(p){
			p[0] = '\0';
			key_len = strlen(key);
			zend_try{
				data = frozen_array_unserialize(data_file);
			}zend_catch{
				zend_error(E_ERROR, "Data corruption in %s, bailing out", data_file);
				zend_bailout();
			}zend_end_try();
			if((data == NULL) || zend_hash_update(qcache_data_hash, key, key_len+1, &data, sizeof(void*), NULL) == FAILURE){
				zend_error(E_ERROR, "Unable to add %s to the qcache data hash", data_file);
				return 0;
			}
			return 1;
		}
	}
	return 0;
}

int qcache_read_data()
{
	char *data_path;
	if(QCACHE_G(data_path)){
		data_path = QCACHE_G(data_path);
	}else{
		data_path = "/home/qxc/htdocs/data/";
	}
	return qcache_walk_dir(data_path, ".data");
}
int qcache_walk_dir(char *path, char *ext){
	char file[MAXPATHLEN]={0,};
	int ndir, i, k;
	char *p = NULL;
	struct dirent **namelist = NULL;
	if ((ndir = php_scandir(path, &namelist, 0, php_alphasort)) > 0){
		for (i = 0; i < ndir; i++) {
			if (!(p = strrchr(namelist[i]->d_name, '.')) || (p && strcmp(p, ext))) {
				free(namelist[i]);
				continue;
			}
			snprintf(file, MAXPATHLEN, "%s%c%s",path, DEFAULT_SLASH, namelist[i]->d_name);
			if(!qcache_load_data(file)){
				goto cleanup;
			}
			free(namelist[i]);
		}
		free(namelist);
	}
	return 1;
cleanup:
	for(k = i; k < ndir; k++){
		free(namelist[k]);
	}
	free(namelist);
	return 0;
}
PHP_MINIT_FUNCTION(qcache)
{
	ZEND_INIT_MODULE_GLOBALS(qcache, php_qcache_init_globals, php_qcache_shutdown_globals);

	REGISTER_INI_ENTRIES();
	key_t shmkey = ftok( "hcqcache" , 'a' );
	shmid = shmget( shmkey , 50*1024*1024, IPC_CREAT | 0666 ) ;
	HashTable *qcache_data_hash_temp = malloc(sizeof(HashTable));
	zend_hash_init(qcache_data_hash_temp, 32,  NULL, NULL, 1);
	qcache_data_hash = shm_start_addr = shm_cur_addr = shmat( shmid , NULL , 0 );
	memcpy(qcache_data_hash, qcache_data_hash_temp, sizeof(HashTable));	
	free(qcache_data_hash_temp);
	shm_cur_addr += sizeof(HashTable);
	qcache_read_data();
	return SUCCESS;
}

PHP_MSHUTDOWN_FUNCTION(qcache)
{
#ifdef ZTS
	ts_free_id(qcache_globals_id);
#else
	php_qcache_shutdown_globals(&qcache_globals);
#endif

	UNREGISTER_INI_ENTRIES();
	return SUCCESS;
}

PHP_RINIT_FUNCTION(qcache)
{
	return SUCCESS;
}

PHP_RSHUTDOWN_FUNCTION(qcache)
{
	#ifdef HAVE_MALLOC_TRIM
		malloc_trim(0);
	#endif
	return SUCCESS;
}

PHP_MINFO_FUNCTION(qcache)
{
}

PHP_FUNCTION(qcache_fetch) 
{
	zval **hentry;
	zval *wrapped;
	char *strkey;
	int strkey_len;
	if(zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "s", &strkey, &strkey_len) == FAILURE){
		return;
	}
	qcache_data_hash = shm_start_addr;
	if(zend_hash_find(qcache_data_hash, strkey, strkey_len+1, (void**)&hentry) == FAILURE){
		RETURN_NULL();
	}
	wrapped = frozen_array_copy_zval_ptr(NULL, hentry[0], 0);

	RETURN_ZVAL(wrapped, 0, 1);
	//RETURN_ZVAL(hentry[0],1, 0);
}

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
	if(zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "s|s", &parent_name, &parent_name_len, &child_name, &child_name_len) == FAILURE){
		RETURN_NULL();
	}
	if(zend_hash_find(qcache_data_hash, parent_name, parent_name_len+1, (void**)&hentry) == FAILURE){
		RETURN_NULL();
	}
	src = hentry[0];
	bool re = checkInt(child_name);
	if(child_name && re == false){
		if (zend_hash_find(Z_ARRVAL_P(src), child_name, child_name_len + 1, (void**)&zvalue) == FAILURE) {
			RETURN_NULL();
		}
		
	}else if(child_name && re == true){
		if (zend_hash_index_find(Z_ARRVAL_P(src), atoi(child_name), (void**)&zvalue) == FAILURE) {
			RETURN_NULL();
		}
	}else{
		RETURN_NULL();
	}
	*return_value = **zvalue;
	zval_copy_ctor(return_value);
}

/* 
 * 重新加载数据到qcache_data_hash中
 */
PHP_FUNCTION(qcache_reload) 
{
	shm_cur_addr = qcache_data_hash;
	shm_cur_addr += sizeof(HashTable);
	qcache_read_data();
}
