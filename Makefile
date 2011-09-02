.include <bsd.own.mk>

MDIST=	${NETBSDSRCDIR}/external/bsd/mdocml/dist
MDOCDIR=${NETBSDSRCDIR}/external/bsd/mdocml

PROGS=	makemandb apropos apropos_cgi
SRCS.makemandb=		makemandb.c sqlite3.c apropos-utils.c apropos-utils.h stopword_tokenizer.c stopword_tokenizer.h
SRCS.apropos=	apropos.c sqlite3.c apropos-utils.c apropos-utils.h stopword_tokenizer.c stopword_tokenizer.h
SRCS.apropos_cgi=	apropos_cgi.c mongoose.h mongoose.c apropos-utils.c apropos-utils.h sqlite3.c sqlite3.h stopword_tokenizer.h stopword_tokenizer.c
MAN=	makemandb.1 apropos.1 apropos-utils.3 init_db.3 close_db.3 run_query.3 run_query_html.3 run_query_pager.3

.PATH:	${MDIST}
CPPFLAGS+=-I${MDIST}
CPPFLAGS+=-DSQLITE_ENABLE_FTS3
CPPFLAGS+=-DSQLITE_ENABLE_FTS3_PARENTHESIS

MDOCMLOBJDIR!=	cd ${MDOCDIR}/lib/libmandoc && ${PRINTOBJDIR}
MDOCMLLIB=	${MDOCMLOBJDIR}/libmandoc.a

DPADD.makemandb+= 	${MDOCMLLIB}
LDADD.makemandb+= 	-L${MDOCMLOBJDIR} -lmandoc
LDADD.apropos_cgi+=	-lpthread
LDADD+=	-lm
LDADD+=	-lz
LDADD+=	-lutil

.include <bsd.prog.mk>
