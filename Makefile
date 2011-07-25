.include <bsd.own.mk>

MDIST=	${NETBSDSRCDIR}/external/bsd/mdocml/dist
MDOCDIR=${NETBSDSRCDIR}/external/bsd/mdocml

PROGS=	makemandb apropos
SRCS.makemandb=		makemandb.c sqlite3.c apropos-utils.c apropos-utils.h stopword_tokenizer.c stopword_tokenizer.h
SRCS.apropos=	apropos.c sqlite3.c apropos-utils.c apropos-utils.h stopword_tokenizer.c stopword_tokenizer.h

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
MKMAN=	no

.include <bsd.prog.mk>
