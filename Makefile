CC=gcc
CFLAGS=-g
LIBS=""


all: sender receiver

sender: dns_sender

receiver: dns_receiver

dns_sender: sender/dns_sender.c
	$(CC) -o dns_sender sender/dns_sender.c $(CFLAGS)

dns_receiver: receiver/dns_receiver.c
	$(CC) -o dns_receiver receiver/dns_receiver.c $(CFLAGS)
	
clean:
	rm dns_sender dns_receiver

