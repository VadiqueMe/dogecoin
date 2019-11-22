AC_DEFUN([DOGECOIN_FIND_BDB5],[
  AC_MSG_CHECKING([for Berkeley DB C++ headers])
  BDB_CPPFLAGS=
  BDB_LIBS=
  bdbpath=X
  bdb5path=X
  bdbdirlist=

  for _vn in 5.3 5.1 53 51 5 ''; do
    for _pfx in b lib ''; do
      bdbdirlist="$bdbdirlist ${_pfx}db${_vn}"
    done
  done

  for searchpath in $bdbdirlist ''; do
    test -n "${searchpath}" && searchpath="${searchpath}/"
    AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[
      #include <${searchpath}db_cxx.h>
    ]],[[
      #if !((DB_VERSION_MAJOR == 5 && DB_VERSION_MINOR >= 1) || DB_VERSION_MAJOR > 5)
        #error "failed to find bdb 5.1+"
      #endif
    ]])],[
      if test "x$bdbpath" = "xX"; then
        bdbpath="${searchpath}"
      fi
    ],[
      continue
    ])
    AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[
      #include <${searchpath}db_cxx.h>
    ]],[[
      #if !(DB_VERSION_MAJOR == 5 && (DB_VERSION_MINOR == 1 || DB_VERSION_MINOR == 3))
        #error "failed to find bdb 5.1 or 5.3"
      #endif
    ]])],[
      bdb5path="${searchpath}"
      break
    ],[])
  done

  if test "x$bdbpath" = "xX"; then
    AC_MSG_RESULT([no])
    AC_MSG_ERROR([libdb_cxx headers missing, Dogecoin Core requires this library for wallet (--disable-wallet to disable wallet functionality)])
  elif test "x$bdb5path" = "xX"; then
    SUBDIR_TO_INCLUDE(BDB_CPPFLAGS,[${bdbpath}],db_cxx)
    AC_ARG_WITH([incompatible-bdb],[AS_HELP_STRING([--with-incompatible-bdb], [use a bdb version other than 5.1 or 5.3])],[
      AC_MSG_WARN([Found Berkeley DB other than 5.1 or 5.3; wallets opened by this build may be not portable!])
    ],[
      AC_MSG_ERROR([Found Berkeley DB other than 5.1 or 5.3, needed for portable wallets (--with-incompatible-bdb to ignore or --disable-wallet to disable wallet functionality)])
    ])
  else
    SUBDIR_TO_INCLUDE(BDB_CPPFLAGS,[${bdb5path}],db_cxx)
    bdbpath="${bdb5path}"
  fi
  AC_SUBST(BDB_CPPFLAGS)

  for searchlib in db_cxx-5.1 db_cxx-5.3 db_cxx; do
    AC_CHECK_LIB([$searchlib],[main],[
      BDB_LIBS="-l${searchlib}"
      break
    ])
  done
  if test "x$BDB_LIBS" = "x"; then
      AC_MSG_ERROR([libdb_cxx missing, Dogecoin Core requires this library for wallet (--disable-wallet to disable wallet functionality)])
  fi
  AC_SUBST(BDB_LIBS)
])
