AC_DEFUN([LNAV_ADDTO], [
  if test "x$$1" = "x"; then
    test "x$verbose" = "xyes" && echo "  setting $1 to \"$2\""
    $1="$2"
  else
    ats_addto_bugger="$2"
    for i in $ats_addto_bugger; do
      ats_addto_duplicate="0"
      for j in $$1; do
        if test "x$i" = "x$j"; then
          ats_addto_duplicate="1"
          break
        fi
      done
      if test $ats_addto_duplicate = "0"; then
        test "x$verbose" = "xyes" && echo "  adding \"$i\" to $1"
        $1="$$1 $i"
      fi
    done
  fi
])dnl

