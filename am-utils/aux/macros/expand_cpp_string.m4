dnl ######################################################################
dnl Expand the value of a CPP macro into a printable string.
dnl Takes: header, macro, [action-if-found, [action-if-not-found]]
dnl It runs the header through CPP looking for a match between the macro
dnl and a string pattern, and if sucessful, it prints the string value out.
AC_DEFUN(AMU_EXPAND_CPP_STRING,
[
# we are looking for a regexp of a string
AC_EGREP_CPP(\".*\",
[$1]
$2,
value="notfound"
AC_TRY_RUN(
[
[$1]
main(argc)
int argc;
{
#ifdef $2
if (argc > 1)
  printf("%s", $2);
exit(0);
#else
# error no such option $2
#endif
exit(1);
}], value=`./conftest dummy 2>>config.log`, value="notfound", value="notfound")
,
value="notfound"
)
if test "$value" = notfound
then
  :
  $4
else
  :
  $3
fi
])
dnl ======================================================================
