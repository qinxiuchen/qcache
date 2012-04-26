
#ifndef PHP_FROZENARRAY_H
#define PHP_FROZENARRAY_H

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "php_qcache.h"
#include "zend.h"

typedef struct {
	zend_object zo;
	zval *data;
	zval *thawed;
	zval *pinned;
} frozen_array_object;

void frozen_array_init(TSRMLS_D);
void frozen_array_shutdown(TSRMLS_D);

zval* frozen_array_unserialize(const char* filename TSRMLS_DC);
zval* frozen_array_wrap_zval(zval* src TSRMLS_DC);
zval* frozen_array_pin_zval(zval* src TSRMLS_DC);

zval* frozen_array_copy_zval_ptr(zval *dst, zval *src, int persistent, size_t *allocated TSRMLS_DC);
void frozen_array_free_zval_ptr(zval** val, int persistent TSRMLS_DC);

#define FROZEN_METHOD(func) PHP_METHOD(FrozenArray, func)
#define PHP_QCACHE_OBJECT_NAME "FrozenArray"

#endif /* PHP_FROZENARRAY_H */
/*
 * Local variables:
 * tab-width: 4
 * c-basic-offset: 4
 * End:
 * vim>600: noet sw=4 ts=4 fdm=marker
 * vim<600: noet sw=4 ts=4
 */
