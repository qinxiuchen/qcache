dnl $Id: config.m4 318410 2012-05-09 09:30:00Z gopalv $
dnl config.m4 for extension qcache

PHP_ARG_ENABLE(qcache, whether to enable qcache support,
[  --enable-qcache           Enable qcache support])

AC_CHECK_FUNCS(malloc_trim)

if test "$PHP_QCACHE" != "no"; then
  PHP_NEW_EXTENSION(qcache, qcache.c, $ext_shared)
  PHP_ADD_EXTENSION_DEP(qcache, spl, true)
fi
