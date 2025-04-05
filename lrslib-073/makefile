#makefile for lrslib-073    2024.2.2 

# default make requires a compiler with  __int128 and openMP support (eg. gcc v.4.2 or later) 
# and installation of the GMP library. Otherwise make the alternative choices below.
# if all else fails use %make lrs64 to compile
# mplrs requires OpenMPI to be installed (including mpicc)

#try uncommenting next line if cc is the default C compiler
#CC = gcc7    
CC ?= gcc      # or gcc7

#GMP library installed - try this first!
GMP=-DGMP -lgmp
MINI=

#GMP library is not installed - try second 
#GMP=-DGMP -DMGMP
#MINI=${ARITH}mini-gmp.c

#compilers supporting openMP  (eg gcc 4.2 and later)
PLRSFLAGS=-fopenmp  -DPLRS 

#compilers without openMP support - try second
#PLRSFLAGS= 

#compilers supporting _int128 (eg gcc 4.2 and later)
BITS=-DB128
MPLRSOBJ2=lrslib2-mplrs.o lrslong2-mplrs.o
LRSOBJ2=lrslib2.o lrslong2.o

#compilers without _int128 support (equivalent to make lrs64 in v.7.2)
#BITS=
#MPLRSOBJ2=
#LRSOBJ2=

#legacy lrs and mplrs
#CFLAGS     = -g -Wall -I ${ARITH} 
CFLAGS     = -O3 -Wall -I ${ARITH} 

#use this if you want only output file contain data between begin/end lines
#CFLAGS     = -O3 -Wall -I ${ARITH} -DLRS_QUIET

#ARITH=                   
ARITH=lrsarith-011/

#Apple clang doesn't support OpenMP - change PLRSFLAGS so default builds
#on macOS
#comment this out if you have OpenMP support and are using an Apple compiler
$(eval APPLE_CHECK := $(shell $(CC) --version | grep Apple | wc -l))
ifeq ($(APPLE_CHECK),1)
 $(info OpenMP support not found, disabling OpenMP parallel build)
 PLRSFLAGS=
endif

default: lrs lrsgmp
#default: lrs mplrs lrsgmp mplrsgmp 


# add -DTIMES if your compiler does *not* support ptimes() and get_time()
# add -DSIGNALS if your compiler does *not* support <signal.h> <unistd.h>

#make lrs               lrs,lrsgmp       hybrid and gmp versions 
#make mplrs             mplrs,mplrsgmp hybrid and gmp versions,  make sure mpicc and OpenMPI library is installed
#make mplrs64           mplrs,mplrsgmp for compilers without 128 bit support

#make flint             lrs and mplrs with FLINT arithmetic 
#make single            makes lrs with various arithmetic packages (depending on compiler),lrsnash 
#make singlemplrs        makes mplrs with various arithmetic packages (depending on compiler)
#make allmp             uses native mp and long arithmetic
#make demo              various demo programs for lrslib     
#make lrsnash           Nash equilibria for 2-person games: lrsnash (gmp), lrsnash1 (64bit), lrsnash2 (128bit)
#make clean             removes binaries                                      

#INCLUDEDIR = /usr/include
#LIBDIR     = /usr/lib

#Kyoto machines usage
INCLUDEDIR = /usr/local/include
LIBDIR     = /usr/local/lib


SHLIB_CFLAGS = -fPIC
mpicxx=mpicc

LRSOBJ=lrs.o lrslong1.o lrslib1.o lrslibgmp.o lrsgmp.o lrsdriver.o
LRSOBJMP=lrs.o lrslong1.o lrslong2.o lrslib1.o lrslib2.o lrslibmp.o lrsmp.o lrsdriver.o
MPLRSOBJ=lrslong1-mplrs.o lrslib1-mplrs.o lrslibgmp-mplrs.o lrsgmp-mplrs.o lrsdriver-mplrs.o mplrs.o

MPLRSOBJ64=lrslong1-mplrs.o lrslib1-mplrs.o lrslibgmp-mplrs.o lrsgmp-mplrs.o lrsdriver-mplrs.o mplrs64.o

lrs: ${LRSOBJ} ${LRSOBJ2}
	$(CC) ${CFLAGS} ${PLRSFLAGS} -DMA ${BITS} -L${LIBDIR} -o lrs ${LRSOBJ} ${LRSOBJ2} ${MINI} ${GMP}
	ln -s -f lrs redund
	ln -s -f lrs minrep
	ln -s -f lrs fel

lrsMP: ${LRSOBJMP}
	$(CC) ${CFLAGS} ${PLRSFLAGS} -DMA ${BITS} -o lrsMP ${LRSOBJMP} 
	$(CC) -O3 hvref.c -o hvref
	ln -s -f lrs redund

lrs.o: lrs.c
	$(CC) ${CFLAGS} -DMA ${BITS} -c -o lrs.o lrs.c

lrs64.o: lrs.c
	$(CC) ${CFLAGS} -DMA -c -o lrs64.o lrs.c

lrslong1.o: ${ARITH}lrslong.c ${ARITH}lrslong.h
	$(CC) ${CFLAGS} -DMA -DSAFE -DLRSLONG -c -o lrslong1.o ${ARITH}lrslong.c

lrslib1.o: lrslib.c lrslib.h
	$(CC) ${CFLAGS} ${PLRSFLAGS} -DMA -DSAFE -DLRSLONG -c -o lrslib1.o lrslib.c
lrslong2.o: ${ARITH}lrslong.c ${ARITH}lrslong.h
	$(CC) ${CFLAGS} -DMA -DSAFE ${BITS} -DLRSLONG -c -o lrslong2.o ${ARITH}lrslong.c
lrslib2.o: lrslib.c lrslib.h
	$(CC) ${CFLAGS} ${PLRSFLAGS} -DMA -DSAFE ${BITS} -DLRSLONG -c -o lrslib2.o lrslib.c
lrslibgmp.o: lrslib.c lrslib.h
	$(CC) ${CFLAGS} ${PLRSFLAGS}  -DMA -I${INCLUDEDIR} -c -o lrslibgmp.o lrslib.c ${GMP}
lrslibmp.o: lrslib.c lrslib.h
	$(CC) ${CFLAGS} ${PLRSFLAGS} -DMA -DMP -c -o lrslibmp.o lrslib.c

lrsgmp.o: ${ARITH}lrsgmp.c ${ARITH}lrsgmp.h
	$(CC) ${CFLAGS} -DMA -I${INCLUDEDIR} -c -o lrsgmp.o ${ARITH}lrsgmp.c ${GMP}

lrsmp.o: ${ARITH}lrsmp.c ${ARITH}lrsmp.h
	$(CC) ${CFLAGS} -DMA -DMP -c -o lrsmp.o ${ARITH}lrsmp.c

inedel: inedel.c ${ARITH}lrsgmp.h ${ARITH}lrsgmp.c
	$(CC) ${CFLAGS} -I${INCLUDEDIR} -L${LIBDIR} -DGMP -o inedel inedel.c ${ARITH}lrsgmp.c -lgmp

polyv: polyv.c ${ARITH}lrsgmp.h ${ARITH}lrsgmp.c
	$(CC) $(CFLAGS) -I${INCLUDEDIR} -L${LIBDIR} -DGMP -o polyv polyv.c ${ARITH}lrsgmp.c -lgmp

lrslong1-mplrs.o: ${ARITH}lrslong.c ${ARITH}lrslong.h
	$(mpicxx) ${CFLAGS} -DTIMES -DSIGNALS -DMA -DSAFE -DLRSLONG -DMPLRS -c -o lrslong1-mplrs.o ${ARITH}lrslong.c

lrslong2-mplrs.o: ${ARITH}lrslong.c ${ARITH}lrslong.h
	$(mpicxx) ${CFLAGS} -DTIMES -DSIGNALS -DMA -DSAFE ${BITS} -DLRSLONG -DMPLRS -c -o lrslong2-mplrs.o ${ARITH}lrslong.c

lrslib1-mplrs.o: lrslib.c lrslib.h
	$(mpicxx) ${CFLAGS} -DTIMES -DSIGNALS -DMA -DSAFE -DLRSLONG -DMPLRS -c -o lrslib1-mplrs.o lrslib.c

lrslib2-mplrs.o: lrslib.c lrslib.h
	$(mpicxx) ${CFLAGS} -DTIMES -DSIGNALS -DMA -DSAFE ${BITS} -DLRSLONG -DMPLRS -c -o lrslib2-mplrs.o lrslib.c

lrslibgmp-mplrs.o: lrslib.c lrslib.h
	$(mpicxx) ${CFLAGS} -DMA -DTIMES -DSIGNALS ${GMP} -DMPLRS -I${INCLUDEDIR} -c -o lrslibgmp-mplrs.o lrslib.c

lrsgmp-mplrs.o: ${ARITH}lrsgmp.c ${ARITH}lrsgmp.h
	$(mpicxx) ${CFLAGS} -DMA -DTIMES -DSIGNALS ${GMP} -DMPLRS -I${INCLUDEDIR} -c -o lrsgmp-mplrs.o ${ARITH}lrsgmp.c

lrsdriver-mplrs.o: lrsdriver.c lrsdriver.h lrslib.h
	$(mpicxx) $(CFLAGS) -c -DMPLRS -o lrsdriver-mplrs.o lrsdriver.c

mplrs.o: mplrs.c mplrs.h lrslib.h ${ARITH}lrsgmp.h
	$(mpicxx) ${CFLAGS} -I${INCLUDEDIR} ${GMP} -DMA -DMPLRS -DTIMES ${BITS} -DSIGNALS -D_WITH_GETLINE -c -o mplrs.o mplrs.c

mplrs64.o: mplrs.c mplrs.h lrslib.h ${ARITH}lrsgmp.h
	$(mpicxx) ${CFLAGS} -I${INCLUDEDIR} ${GMP} -DMA -DMPLRS -DTIMES -DSIGNALS -D_WITH_GETLINE -c -o mplrs64.o mplrs.c

mplrs: ${MPLRSOBJ} ${MPLRSOBJ2}
	$(mpicxx) ${CFLAGS} -DTIMES -DSIGNALS -D_WITH_GETLINE -DMPLRS -DMA ${BITS} -L${LIBDIR} -o mplrs ${MPLRSOBJ} ${MPLRSOBJ2} ${MINI} ${GMP}

mplrs64: ${MPLRSOBJ64} mplrsgmp
	$(mpicxx) ${CFLAGS} -DTIMES -DSIGNALS -D_WITH_GETLINE -DMPLRS -DMA -L${LIBDIR} -o mplrs ${MPLRSOBJ64} -lgmp

mplrsgmp: mplrs.c mplrs.h lrslib.c lrslib.h ${ARITH}lrsgmp.c ${ARITH}lrsgmp.h lrsdriver.h lrsdriver.c
	$(mpicxx) ${CFLAGS} -DTIMES -DSIGNALS -D_WITH_GETLINE -DMPLRS -DGMP -I${INCLUDEDIR} mplrs.c lrslib.c ${ARITH}lrsgmp.c lrsdriver.c -L${LIBDIR} -o mplrsgmp -lgmp

mplrs1: mplrs.c mplrs.h lrslib.c lrslib.h ${ARITH}lrslong.c ${ARITH}lrslong.h lrsdriver.h lrsdriver.c
	$(mpicxx) ${CFLAGS} -DTIMES -DSIGNALS -D_WITH_GETLINE -DMPLRS -DSAFE -DLRSLONG mplrs.c lrslib.c ${ARITH}lrslong.c lrsdriver.c -o mplrs1

mplrs2: mplrs.c mplrs.h lrslib.c lrslib.h ${ARITH}lrslong.c ${ARITH}lrslong.h lrsdriver.h lrsdriver.c
	$(mpicxx) ${CFLAGS} -DTIMES -DSIGNALS -D_WITH_GETLINE -DMPLRS -DSAFE -DLRSLONG ${BITS} mplrs.c lrslib.c ${ARITH}lrslong.c lrsdriver.c -o mplrs2

mplrsmp: mplrs.c mplrs.h lrslib.c lrslib.h ${ARITH}lrsmp.c ${ARITH}lrsmp.h lrsdriver.h lrsdriver.c
	$(mpicxx) ${CFLAGS} -DMP -DTIMES -DSIGNALS -D_WITH_GETLINE -DMPLRS mplrs.c lrslib.c ${ARITH}lrsmp.c lrsdriver.c -o mplrsmp

singlemplrs:  mplrs1 mplrs2

flint:	 	lrs.c lrslib.c lrslib.h ${ARITH}lrsgmp.c ${ARITH}lrsgmp.h
		@test -d  ${INCLUDEDIR}/flint || { echo ${INCLUDEDIR}/flint not found; exit 1; }
		$(CC) -O3 -DFLINT ${PLRSFLAGS} -I/usr/local/include/flint lrs.c lrslib.c ${ARITH}lrsgmp.c lrsdriver.c -L/usr/local/lib -Wl,-rpath=/usr/local/lib -lflint -o lrsflint -lgmp

mplrsflint:	mplrs.c mplrs.h lrslib.c lrslib.h ${ARITH}lrsgmp.c ${ARITH}lrsgmp.h lrsdriver.c lrsdriver.h
	${mpicxx} ${CFLAGS} -DTIMES -DSIGNALS -D_WITH_GETLINE -DFLINT -I${INCLUDEDIR}/flint -DMPLRS -o mplrsflint mplrs.c lrsdriver.c lrslib.c ${ARITH}lrsgmp.c -L${LIBDIR} -lflint -lgmp

lrsgmp:		lrs.c lrslib.c lrslib.h ${ARITH}lrsgmp.c ${ARITH}lrsgmp.h lrsdriver.h lrsdriver.c 
		$(CC)  ${CFLAGS} ${PLRSFLAGS} -I${INCLUDEDIR} -o lrsgmp lrs.c lrslib.c ${ARITH}lrsgmp.c lrsdriver.c -L${LIBDIR} ${MINI} ${GMP} 

single:		lrs.c ${ARITH}lrslong.c ${ARITH}lrslong.h lrslib.c lrslib.h ${ARITH}lrsgmp.c ${ARITH}lrsgmp.h lrsdriver.h lrsdriver.c
		$(CC)  ${CFLAGS} ${PLRSFLAGS}  -DSAFE  -DLRSLONG -o lrs1 lrs.c lrslib.c ${ARITH}lrslong.c lrsdriver.c
		$(CC)  ${CFLAGS} ${PLRSFLAGS} ${BITS} -DSAFE  -DLRSLONG -o lrs2 lrs.c lrslib.c ${ARITH}lrslong.c lrsdriver.c
		$(CC)  ${CFLAGS} ${PLRSFLAGS} -DMP -o lrsmp lrs.c lrslib.c lrsdriver.c ${ARITH}lrsmp.c
		$(CC)  ${CFLAGS} ${PLRSFLAGS} -DGMP -I${INCLUDEDIR} -o lrsgmp lrs.c lrslib.c ${ARITH}lrsgmp.c lrsdriver.c -L${LIBDIR}  -lgmp
		$(CC)  ${CFLAGS} ${PLRSFLAGS} -DMGMP -DGMP -I${INCLUDEDIR} -o lrsmgmp lrs.c lrslib.c ${ARITH}lrsgmp.c lrsdriver.c ${ARITH}mini-gmp.c

allmp:		lrs.c lrslib.c lrslib.h ${ARITH}lrsmp.c ${ARITH}lrsmp.h lrsdriver.h lrsdriver.c
		$(CC)  ${CFLAGS} ${PLRSFLAGS} -DMP  -o lrsmp lrs.c lrslib.c lrsdriver.c ${ARITH}lrsmp.c
		$(CC)  ${CFLAGS} ${PLRSFLAGS} -DSAFE -DLRSLONG -o lrs1 lrs.c lrslib.c lrsdriver.c ${ARITH}lrslong.c
		$(CC) ${CFLAGS} ${PLRSFLAGS}  -DSAFE -DLRSLONG ${BITS} -o lrs2 lrs.c lrslib.c lrsdriver.c ${ARITH}lrslong.c
		$(CC) ${CFLAGS} -DMP -DLRS_QUIET   -o lrsnash lrsnash.c lrsnashlib.c lrslib.c lrsdriver.c ${ARITH}lrsmp.c
		$(CC) ${CFLAGS} -DMP -o setupnash setupnash.c lrslib.c lrsdriver.c ${ARITH}lrsmp.c
		$(CC) ${CFLAGS} -DMP -o setupnash2 setupnash2.c lrslib.c lrsdriver.c ${ARITH}lrsmp.c
		$(CC) ${CFLAGS}  -o 2nash 2nash.c

demo:	lpdemo1.c lrslib.c lrsdriver.c lrslib.h ${ARITH}lrsgmp.c ${ARITH}lrsgmp.h
	$(CC) ${CFLAGS}   -I${INCLUDEDIR} -L${LIBDIR} -o lpdemo1 lpdemo1.c lrslib.c lrsdriver.c ${ARITH}lrsgmp.c -lgmp -DGMP
	$(CC) ${CFLAGS}   -I${INCLUDEDIR} -L${LIBDIR} -o lpdemo lpdemo.c lrslib.c lrsdriver.c ${ARITH}lrsgmp.c -lgmp -DGMP
	$(CC) ${CFLAGS}   -I${INCLUDEDIR} -L${LIBDIR} -o lpdemo2 lpdemo2.c lrslib.c lrsdriver.c ${ARITH}lrsgmp.c -lgmp -DGMP
	$(CC) ${CFLAGS}   -I${INCLUDEDIR} -L${LIBDIR} -o vedemo  vedemo.c lrslib.c lrsdriver.c ${ARITH}lrsgmp.c -lgmp -DGMP
	$(CC) ${CFLAGS}   -I${INCLUDEDIR} -L${LIBDIR} -o chdemo  chdemo.c lrslib.c lrsdriver.c ${ARITH}lrsgmp.c -lgmp -DGMP

lrsnash:	lrsnash.c nashdemo.c lrsnashlib.c lrslib.c lrsnashlib.h lrslib.h ${ARITH}lrsgmp.c ${ARITH}lrsgmp.h ${ARITH}lrslong.h lrsdriver.h lrsdriver.c
		$(CC) ${CFLAGS}   -I${INCLUDEDIR} -L${LIBDIR} -o lrsnashgmp lrsnash.c lrsnashlib.c lrslib.c ${ARITH}lrsgmp.c lrsdriver.c  ${MINI} ${GMP}
		$(CC) ${CFLAGS} -DNASH  -I${INCLUDEDIR} -L${LIBDIR} -o lrsnash1 lrsnash.c lrsnashlib.c lrslib.c ${ARITH}lrslong.c lrsdriver.c -DLRSLONG -DSAFE

		$(CC) ${CFLAGS} -DNASH  -I${INCLUDEDIR} -L${LIBDIR} -o lrsnash2 lrsnash.c lrsnashlib.c lrslib.c ${ARITH}lrslong.c lrsdriver.c -DLRSLONG -DSAFE ${BITS}
		$(CC) ${CFLAGS}   -I${INCLUDEDIR} -L${LIBDIR} -o nashdemo nashdemo.c lrsnashlib.c lrslib.c ${ARITH}lrsgmp.c lrsdriver.c  ${MINI} ${GMP}
		$(CC) ${CFLAGS} -DMP -o setupnash setupnash.c lrslib.c lrsdriver.c ${ARITH}lrsmp.c
		$(CC) ${CFLAGS} -DMP -o setupnash2 setupnash2.c lrslib.c lrsdriver.c ${ARITH}lrsmp.c
		$(CC) ${CFLAGS}  -I${INCLUDEDIR} -L${LIBDIR} -o 2nash 2nash.c
		cp lrsnashgmp lrsnash

######################################################################
# From here on the author is David Bremner <bremner@unb.ca> to whom you should turn for help             
#
# Shared library variables
SONAME ?=liblrs.so.1
SOMINOR ?=.0.0
SHLIB ?=$(SONAME)$(SOMINOR)
SHLINK ?=liblrs.so

SHLIBOBJ2=lrslib2-shr.o lrslong2-shr.o

# for 32 bit machines

# SHLIBOBJ2=

SHLIBOBJ=lrslong1-shr.o lrslib1-shr.o  \
	lrslibgmp-shr.o lrsgmp-shr.o lrsdriver-shr.o \
	${SHLIBOBJ2}

SHLIBBIN=lrs-shared lrsnash-shared

# Building (linking) the shared library, and relevant symlinks.

${SHLIB}: ${SHLIBOBJ}
	$(CC) -shared -Wl,-soname=$(SONAME) $(SHLIBFLAGS) -o $@ ${SHLIBOBJ} -lgmp

${SONAME}: ${SHLIB}
	ln -sf ${SHLIB} ${SONAME}

${SHLINK}: ${SONAME}
	ln -sf $< $@

# binaries linked against the shared library

all-shared: ${SHLIBBIN}

lrs-shared: ${SHLINK} lrs-shared.o
	$(CC) $^ -o $@ -L . -llrs


lrsnash-shared: ${SHLINK}  lrsnash.c
	$(CC) ${CFLAGS} -DGMP -DMA lrsnash.c  lrsnashlib.c -I${INCLUDEDIR} -o $@ -L . -llrs -lgmp

# driver object files

lrs-shared.o: lrs.c
	$(CC) ${CFLAGS} -DMA ${BITS} -L${LIBDIR} -c -o $@ lrs.c

# build object files for the shared library

lrslib1-shr.o: lrslib.c lrslib.h
	$(CC) ${CFLAGS} ${SHLIB_CFLAGS} -DMA -DSAFE -DLRSLONG -c -o $@ lrslib.c

lrsdriver-shr.o: lrsdriver.c
	$(CC) ${CFLAGS} ${SHLIB_CFLAGS} -c -o $@ $<

lrslong1-shr.o: ${ARITH}lrslong.c ${ARITH}lrslong.h
	$(CC) ${CFLAGS} ${SHLIB_CFLAGS} -DMA -DSAFE -DLRSLONG -c -o $@ ${ARITH}lrslong.c

lrslong2-shr.o: ${ARITH}lrslong.c ${ARITH}lrslong.h
	$(CC) ${CFLAGS} ${SHLIB_CFLAGS} -DMA -DSAFE ${BITS} -DLRSLONG -c -o $@ ${ARITH}lrslong.c

lrslibgmp-shr.o: lrslib.c lrslib.h
	$(CC) ${CFLAGS} ${SHLIB_CFLAGS} -DMA -DGMP -I${INCLUDEDIR} -c -o $@ lrslib.c

lrsgmp-shr.o: ${ARITH}lrsgmp.c ${ARITH}lrsgmp.h
	$(CC) ${CFLAGS} ${SHLIB_CFLAGS} -DMA -DGMP -I${INCLUDEDIR} -c -o $@ ${ARITH}lrsgmp.c

lrslib2-shr.o: lrslib.c lrslib.h
	$(CC) ${CFLAGS} ${SHLIB_CFLAGS} -DMA -DSAFE ${BITS} -DLRSLONG -c -o $@ lrslib.c

######################################################################
# install targets
# where to install binaries, libraries, include files
prefix ?= /usr/local
INSTALL_INCLUDES=lrslib.h lrsdriver.h ${ARITH}lrsgmp.h ${ARITH}lrslong.h ${ARITH}lrsmp.h lrsrestart.h

install: all-shared install-common
	mkdir -p $(DESTDIR)${prefix}/bin
	for file in ${SHLIBBIN}; do cp $${file} $(DESTDIR)${prefix}/bin/$$(basename $$file -shared); done
	mkdir -p $(DESTDIR)${prefix}/lib
	install -t $(DESTDIR)${prefix}/lib $(SHLIB)
	cd $(DESTDIR)${prefix}/lib && ln -sf $(SHLIB) $(SHLINK)
	cd $(DESTDIR)${prefix}/lib && ln -sf $(SHLIB) $(SONAME)

install-common:
	mkdir -p $(DESTDIR)${prefix}/include/lrslib
	install -t $(DESTDIR)${prefix}/include/lrslib ${INSTALL_INCLUDES}

######################################################################
clean:		
	rm -f  lrs lrs1 lrsgmp lrsmgmp lpdemo lpdemo1 lpdemo2 mplrs1 mplrs mplrsmp  mplrsgmp lrs2 mplrs2 lrsflint mplrsflint 
	rm -f *.o *.exe *.so redund fel minrep
	rm -f  lrsmp lrsMP hvref setupnash setupnash2 lrsnashgmp lrsnash lrsnash1 lrsnash2 nashdemo 2nash vedemo polyv inedel
	rm -f ${LRSOBJ} ${LRSOBJ64} ${SHLIBOBJ} ${SHLIB} ${SONAME} ${SHLINK}
	rm -f ${SHLIBBIN}
