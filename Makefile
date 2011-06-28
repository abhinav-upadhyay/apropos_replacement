.include <bsd.own.mk>

MDIST=	${NETBSDSRCDIR}/external/bsd/mdocml/dist

PROGS=	makemandb apropos
SRCS.makemandb=		makemandb.c sqlite3.c
SRCS.apropos=	apropos.c sqlite3.c

.PATH:	${MDIST}
CPPFLAGS+=-I${MDIST}
CPPFLAGS+=-DSQLITE_ENABLE_FTS3
CPPFLAGS+=-DSQLITE_ENABLE_FTS3_PARENTHESIS

DPADD.makemandb+= 	/usr/src/external/bsd/mdocml/lib/libmandoc/libmandoc.a
LDADD.makemandb+= 	-L/usr/src/external/bsd/mdocml/lib/libmandoc -lmandoc
LDADD+=	-lm
.include <bsd.prog.mk>
