FLAGS = -Wall -g -std=gnu99 

mancsrv: mancsrv.o
	gcc ${FLAGS} -o $@ $^

%.o: %.c
	gcc ${FLAGS} -c $<

clean:
	rm -f *.o mancsrv