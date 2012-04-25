dnl $Id: config.m4 318410 2011-10-25 16:59:04Z gopalv $
dnl config.m4 for extension qcache

PHP_ARG_ENABLE(qcache, whether to enable qcache support,
[  --enable-qcache           Enable qcache support])

AC_CHECK_FUNCS(malloc_trim)

if test "$PHP_QCACHE" != "no"; then

  qcache_sources="	qcache.c\
					frozenarray.c"

  PHP_NEW_EXTENSION(qcache, $qcache_sources, $ext_shared)
  PHP_ADD_EXTENSION_DEP(qcache, spl, true)
fi
