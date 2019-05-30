CC=gcc

launcher: launcher.c
	$(CC) -o $@ $^ -lnuma

