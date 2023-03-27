CC = cc
CFLAGS = -O -DUSG -I/usr/ucbinclude
LDFLAGS =
LIBS = -L/usr/ucblib -lsocket -lnsl -lucb

all: dslipuser mkslipuser slip

dslipuser: dslipuser.o
	$(CC) $(LDFLAGS) -o dslipuser dslipuser.o $(LIBS)

mkslipuser: mkslipuser.o
	$(CC) $(LDFLAGS) -o mkslipuser mkslipuser.o $(LIBS)

slip: slip.o
	$(CC) $(LDFLAGS) -o slip slip.o $(LIBS)

clean:
	rm -f *.o mkslipuser dslipuser slip

install:
	cp dslipuser mkslipuser slip /usr/sbin
	chmod 4755 /usr/sbin/slip
