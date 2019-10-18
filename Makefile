CFLAGS ?= -O3
FILES = main.c vmsim.c vmsim.h Makefile
vmsim: main.o vmsim.o

cachesim.o: vmsim.c vmsim.h
main.o: main.c vmsim.h

clean:
	rm -f *.o *~ \#* vmsim

submit:
	tar -czvf last_first_a3_b.tar.gz $(FILES)
