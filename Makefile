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
	
doc: dokumentace.pdf

dokumentace.pdf: doc/doc.md
	cd doc && pandoc -s -o ../manual.pdf -V geometry:margin=25mm -V lang:cs -V papersize:a4 doc.md
	
pack: 
	tar -czvf xmacho12.tar.gz --overwrite receiver/ sender/ Makefile dokumentace.pdf
	
clean:
	rm -f dns_sender dns_receiver doc.pdf

