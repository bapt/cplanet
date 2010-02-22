CC=		gcc
INCLUDES=	-I/usr/local/include -I/usr/local/include/ClearSilver
LIBDIR=		-L/usr/local/lib
LIBS=		-lmrss -lnxml -lz -lneo_cs -lneo_utl -lneo_cgi -liconv
LDFLAGS+=	${LIBDIR}
CFLAGS+=	-Wall -Werror -pipe -O2

PROG=		cplanet
SRCS=		cplanet.c
OBJS=		${SRCS:.c=.o}

PREFIX?=	/usr/local
BINDIR?=	${PREFIX}/bin
MANDIR?=	${PREFIX}/man/man1

all: ${PROG}

${PROG}: ${OBJS}
	${CC} ${LDFLAGS} ${CFLAGS} ${INCLUDES} ${OBJS} -o $@ ${LIBS}

.c.o:
	${CC} -o $@ -c $< ${CFLAGS} ${INCLUDES}

install:
	install -o root -g wheel -m 755 ${PROG} ${BINDIR}
	install -o root -g wheel -m 755 ${PROG}.1 ${MANDIR}

deinstall:
	rm -f ${BINDIR}/${PROG}
	rm -f ${MANDIR}/${PROG}.1

clean:
	rm -f ${PROG} *.o *.core
