PROG=	cplanet
SRCS=	cplanet.c
CFLAGS+=	-Wall -g -I/usr/local/include -I/usr/local/include/ClearSilver
LDADD+=	-L/usr/local/lib -lmrss -lnxml -lz -lneo_cs -lneo_utl -lneo_cgi -liconv


.include <bsd.prog.mk>
