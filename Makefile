PROG=	cplanet
SRCS=	cplanet.c cplanet.h
CFLAGS+=	-Wall -g -I/usr/local/include -I/usr/local/include/ClearSilver
LDADD+=	-L/usr/local/lib -lmrss -lnxml -lz -lneo_cs -lneo_utl -lneo_cgi -liconv -lexpat -lcurl


.include <bsd.prog.mk>
