CFLAGS+=-fpic

all : holepunch

holepunch : NOTICE.o

NOTICE : ;

NOTICE.o : NOTICE
	$(LD) -r -b binary -o $@ $<

clean :
	rm -f holepunch NOTICE.o
