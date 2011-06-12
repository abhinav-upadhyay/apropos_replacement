.include <bsd.own.mk>

MDIST=	${NETBSDSRCDIR}/external/bsd/mdocml/dist

PROG=	makemandb
SRCS=	makemandb.c 

.PATH:	${MDIST}
CPPFLAGS+=-g
CPPFLAGS+=-I${MDIST}

DPADD+= 	/usr/src/external/bsd/mdocml/lib/libmandoc/libmandoc.a
LDADD+= 	-L/usr/src/external/bsd/mdocml/lib/libmandoc -lmandoc

.include <bsd.prog.mk>
