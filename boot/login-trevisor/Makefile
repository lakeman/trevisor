INC    = ../../include/core/config.h

.PHONY : all clean 

all : module1.bin module2.bin

clean :
	rm -f module1.bin module2.bin config  

config.o: config.c tresor_key.c $(INC)
	gcc -c -idirafter ../../include -Wall config.c configparser.c loaddata.c 

module1.bin : asm.s main.c $(INC)
	gcc -m32 -Wall -s -Wl,-Ttext,0x8000 -nostdlib -nostdinc \
		-idirafter ../../include -o module1.bin asm.s main.c tresor_key.c

module2.bin : config.o configparser.c configparser.h $(INC) \
              trevisor.conf tresor_key.o
	gcc -Wall -idirafter ../../include -o config config.o loaddata.c \
		configparser.c tresor_key.o
	./config  trevisor.conf  module2.bin key


