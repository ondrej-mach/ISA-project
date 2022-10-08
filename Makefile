CC=gcc
CFLAGS=-g
LIBS=""


all: sender receiver

sender: dns_sender

receiver: dns_receiver

dns_sender: sender/dns_sender.c sender/dns_sender_events.c
	$(CC) -o dns_sender sender/dns_sender.c sender/dns_sender_events.c $(CFLAGS)

dns_receiver: receiver/dns_receiver.c receiver/dns_receiver_events.c
	$(CC) -o dns_receiver receiver/dns_receiver.c receiver/dns_receiver_events.c $(CFLAGS)
	
clean:
	rm dns_sender dns_receiver

