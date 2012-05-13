# This file is part of Autoconf.                       -*- Autoconf -*-


AC_DEFUN([DOC_SUPPORT_INIT], [

PD_DOC_SUPPORT_DEFVAL=true
PD_DOCTOOL_PATH=/usr/share/policy-doc-tools

AC_PATH_TOOL([PD_LYX], lyx)

AS_IF([test x$PD_LYX = x],
      [PD_DOC_SUPPORT_DEFVAL=false
       AC_MSG_WARN([Can't find lyx. doc-support is going to be disabled])]
)

AC_PATH_TOOL([PD_FIG2DEV], fig2dev)

AS_IF([test x$PD_FIG2DEV = x],
      [PD_DOC_SUPPORT_DEFVAL=false
       AC_MSG_WARN([Can't find fig2dev. doc-support is going to be disabled])]
)

AC_PATH_TOOL([PD_DOXYGEN], doxygen)

AS_IF([test x$PD_DOXYGEN = x],
      [PD_DOC_SUPPORT_DEFVAL=false
       AC_MSG_WARN([Can't find doxygen. doc-support is going to be disabled])]
)

AC_PATH_TOOL([PD_XSLTPROC], xsltproc)

AS_IF([test x$PD_XSLTPROC = x],
      [PD_DOC_SUPPORT_DEFVAL=false
       AC_MSG_WARN([Can't find xsltproc. doc-support is going to be disabled])]
)

AC_SUBST([PD_DOXML2DB_STY], [${PD_DOCTOOL_PATH}/xslt/doxml2db.sty])

AC_CHECK_FILE([$PD_DOXML2DB_STY], [PD_DOXML2DB_STY_OK=true])

AS_IF([test x$PD_DOXML2DB_STY_OK != xtrue],
      [PD_DOC_SUPPORT_DEFVAL=false
       AC_MSG_WARN([Can't find doxml2db.sty; doc-support is going to be disabled])]
)

AC_ARG_ENABLE(
[doc-support],
[AS_HELP_STRING(--disable-doc-support, [turns off policy-doc support])],
[
case "$enableval" in #(
y|Y|yes|Yes|YES) doc_support=false ;; #(
n|N|no|No|NO)	 doc_support=true  ;; #(
*) AC_MSG_ERROR([bad value '${enableval}' for --disable-doc-support]) ;;
esac
],
[doc_support=$PD_DOC_SUPPORT_DEFVAL])

AM_CONDITIONAL([PD_SUPPORT], [test x${doc_support} = xtrue])


])
