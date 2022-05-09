CC=gcc

all: yash yashd 

yash: yash.c
	$(CC) -g -o yash yash.c -lpthread

yashd: yashd.c
	$(CC) -g -o yashd yashd.c -lpthread

clean:
	rm yashd yash
