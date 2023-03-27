#
# Makefile file for SLIP
#

all:
	cd driver/io; make -f slip.mk all install
	cd utils; make all install
	cd dialslip; make all install

clean:
	cd driver/io; make -f slip.mk clean
	cd utils; make clean
	cd dialslip; make clean
