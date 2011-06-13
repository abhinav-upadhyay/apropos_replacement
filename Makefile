.include <bsd.own.mk>

MDIST=	${NETBSDSRCDIR}/external/bsd/mdocml/dist

PROG=	makemandb
SRCS=	makemandb.c sqlite3.c

.PATH:	${MDIST}
CPPFLAGS+=-I${MDIST}
CPPFLAGS+=-DSQLITE_ENABLE_FTS3
CPPFLAGS+=-DSQLITE_ENABLE_FTS3_PARENTHESIS

DPADD+= 	/usr/src/external/bsd/mdocml/lib/libmandoc/libmandoc.a
LDADD+= 	-L/usr/src/external/bsd/mdocml/lib/libmandoc -lmandoc

.include <bsd.prog.mk>
