CC = gcc
CFLAGS = -lreadline -I. -g

yash: yash.o
	$(CC) $< -o $@ $(CFLAGS)

yash.o: yash.c
	$(CC) -c $(CFLAGS) $< -o $@

clean:
	rm -f yash