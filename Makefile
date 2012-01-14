.include <bsd.own.mk>

MDIST=	${NETBSDSRCDIR}/external/bsd/mdocml/dist
MDOCDIR=${NETBSDSRCDIR}/external/bsd/mdocml

PROGS=	makemandb apropos
SRCS.makemandb=		makemandb.c sqlite3.c apropos-utils.c apropos-utils.h
SRCS.apropos=	apropos.c sqlite3.c apropos-utils.c apropos-utils.h
MAN=	makemandb.1 apropos.1 apropos-utils.3 init_db.3 close_db.3 run_query.3 run_query_html.3 run_query_pager.3

.PATH:	${MDIST}
CPPFLAGS+=-I${MDIST}
CPPFLAGS+=-DSQLITE_ENABLE_FTS3
CPPFLAGS+=-DSQLITE_ENABLE_FTS3_PARENTHESIS
CPPFLAGS+=-g
MDOCMLOBJDIR!=	cd ${MDOCDIR}/lib/libmandoc && ${PRINTOBJDIR}
MDOCMLLIB=	${MDOCMLOBJDIR}/libmandoc.a

DPADD.makemandb+= 	${MDOCMLLIB}
LDADD.makemandb+= 	-L${MDOCMLOBJDIR} -lmandoc
LDADD+=	-lm
LDADD+=	-lz
LDADD+=	-lutil
LDADD+= 	-L${MDOCMLOBJDIR} -lmandoc
.include <bsd.prog.mk>
