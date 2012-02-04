.include <bsd.own.mk>

MDIST=	${NETBSDSRCDIR}/external/bsd/mdocml/dist
MDOCDIR=${NETBSDSRCDIR}/external/bsd/mdocml

PROGS=	makemandb apropos
SRCS.makemandb=		makemandb.c sqlite3.c apropos-utils.c
SRCS.apropos=	apropos.c sqlite3.c apropos-utils.c
SRCS.whatis=	whatis.c apropos-utils.c sqlite3.c
MAN=	apropos-utils.3 init_db.3 close_db.3 run_query.3 run_query_html.3 run_query_pager.3
MAN.makemandb=	makemandb.8
MAN.apropos=	apropos.1
MAN.whatis=	whatis.1

.PATH:	${MDIST}
CPPFLAGS+=-I${MDIST}
CPPFLAGS+=-DSQLITE_ENABLE_FTS3
CPPFLAGS+=-DSQLITE_ENABLE_FTS3_PARENTHESIS

MDOCMLOBJDIR!=	cd ${MDOCDIR}/lib/libmandoc && ${PRINTOBJDIR}
MDOCMLLIB=	${MDOCMLOBJDIR}/libmandoc.a

DPADD.makemandb+= 	${MDOCMLLIB}
LDADD.makemandb+= 	-L${MDOCMLOBJDIR} -lmandoc
LDADD+=	-lm
LDADD+=	-lz
LDADD+=	-lutil

stopwords.c: stopwords.txt
	( set -e; ${TOOL_NBPERF} -n stopwords_hash -s -p ${.ALLSRC};	\
	echo 'static const char *stopwords[] = {';			\
	${TOOL_SED} -e 's|^\(.*\)$$|	"\1",|' ${.ALLSRC};		\
	echo '};'							\
	) > ${.TARGET}

DPSRCS+=	stopwords.c
CLEANFILES+=	stopwords.c

.include <bsd.prog.mk>
