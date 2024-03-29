#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <ctype.h>

#include <unistd.h>
#include <errno.h>

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "dns_sender_events.h"

#define BUFFER_SIZE 512
#define FILENAME_DELIM "x"
#define LABEL_LENGTH 63
#define MAX_IP_LEN 16
#define RESOLV_CONF "/etc/resolv.conf"


typedef struct {
    char *baseHost;
    char *upstreamIP;
    char *dstFilepath;
    char *srcFilepath;
} Arguments;


void usage() {
    fprintf(stderr, "Usage:\ndns_sender [-u UPSTREAM_DNS_IP] {BASE_HOST} {DST_FILEPATH} [SRC_FILEPATH]\n");
}

void parseArguments(int argc, char **argv, Arguments *args) {
	int c;
	while ((c = getopt(argc, argv, "u:")) != EOF) {
		switch (c) {
			case 'u':
				args->upstreamIP = optarg;
				break;
				
			case '?':
			default:
				usage();
                exit(0);
		}
	}
	argc -= optind;
	argv += optind;
    
    // parse remaining 2 or 3 positional arguments
    if (argc >= 2 && argc <= 3) {
        // mandatory base host
        args->baseHost = argv[0];
        // mandatory dst file name
        args->dstFilepath = argv[1];
        // optional src file
        if (argc >= 3) {
            args->srcFilepath = argv[2];
        }
    } else {
        usage();
        exit(1);
    }
    
}

// return 0 if successful
int getDefaultIP(char *buffer, char *confFilename) {
    FILE *f = fopen(confFilename, "r");
    if (f == NULL) {
        fprintf(stderr, "Could not open `%s`\n", confFilename);
        return 1;
    }
    
    char *keyword = "nameserver";
    int index = 0;
    
    int c = '\0';
    while (c != EOF) {
        c = fgetc(f);
        // read until the keyword matches
        while (c == keyword[index]) {
            c = fgetc(f);
            index++;
            
            if (keyword[index] == '\0') {
                if (!isspace(c)) {
                    break;
                }
                while (isspace(c)) {
                    c = fgetc(f);
                }
                index = 0;
                while (!isspace(c) && (c != EOF) && (index < MAX_IP_LEN)) {
                    buffer[index++] = c;
                    c = fgetc(f);
                }
                return !isspace(c);
            }
        }
        // Waste the rest of the line
        while ((c != '\n') && (c != EOF)) { c = fgetc(f); }
    }
    
    return 1;
}

int sendQuery(int sock, char *payload) {
    uint16_t static transactionID = 0;
    
    char buffer[BUFFER_SIZE];
    // LENGTH - used only in DNS over TCP
    uint16_t *dnsLength = &((uint16_t *)buffer)[0];
    // TRANSACTION_ID
    ((uint16_t *)buffer)[1] = htons(transactionID++);
    // FLAGS
    ((uint16_t *)buffer)[2] = htons(0x120);
    // QDCOUNT
    ((uint16_t *)buffer)[3] = htons(1);
    // ANCOUNT
    ((uint16_t *)buffer)[4] = htons(0);
    // NSCOUNT
    ((uint16_t *)buffer)[5] = htons(0);
    // ARCOUNT
    ((uint16_t *)buffer)[6] = htons(0);
    
    int payloadLen = strlen(payload);
    
    // set the index to the end of DNS header
    int bufferIndex = 14;
    int payloadIndex = 0;
    
    int c = 1;
    while (c != '\0') {
        // reserve 1 byte for length octet
        char *lenOctet = &buffer[bufferIndex];
        bufferIndex++;
        
        int labelLen = 0;
        while (true) {
            c = payload[payloadIndex];
            payloadIndex++;
            
            if ((c == '.') || (c == '\0')) {
                *lenOctet = labelLen;
                break;
            } else {
                buffer[bufferIndex] = c;
                bufferIndex++;
                labelLen++;
            }
        }
    }
    
    buffer[bufferIndex++] = 0x00;
    // QTYPE
    *(uint16_t *)&buffer[bufferIndex] = htons(1);
    bufferIndex += 2;
    // QCLASS
    *(uint16_t *)&buffer[bufferIndex] = htons(1);
    bufferIndex += 2;
    
    *dnsLength = htons(bufferIndex-2);
    
    if (write(sock, buffer, bufferIndex) < bufferIndex) {
        fprintf(stderr, "Error while sending data.\n");
        exit(1);
    }
}

int sendData(FILE *in, struct sockaddr_in addr, Arguments *args) {
    int sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    
    if (connect(sock, (struct sockaddr *) &addr, sizeof(addr)) < 0) {
        fprintf(stderr, "Connection to server `%s` failed.\n", inet_ntoa(addr.sin_addr));
        exit(1);
    }
    
    dns_sender__on_transfer_init(&addr.sin_addr);
    
    // In first phase, the filename is printed
    // After that, the content is printed
    bool printingFilename = true;
    bool finished = false;
    int inputIndex = 0;
    int fileSize = 0; // only for printing
    
    while (!finished) {
        
        char buffer[BUFFER_SIZE];
        int bufferIndex = 0;
        int c;
        int chunkSize = 0; // only for printing
        
        while (bufferIndex + 2 <= LABEL_LENGTH) {
            if (printingFilename) {
                c = args->dstFilepath[inputIndex];
                if (c == '\0') {
                    printingFilename = false;
                    sprintf(&buffer[bufferIndex], "%s", FILENAME_DELIM);
                    bufferIndex += strlen(FILENAME_DELIM);
                    continue;
                }
                
            } else {
                c = fgetc(in);
                if (c == EOF) {
                    sprintf(&buffer[bufferIndex], "%s", FILENAME_DELIM);
                    bufferIndex += strlen(FILENAME_DELIM);
                    finished = true;
                    break;
                } else {
                    chunkSize++;
                }
            }
            
            sprintf(&buffer[bufferIndex], "%02x", c);
            bufferIndex += 2;
            inputIndex++;
        }
        
        sprintf(&buffer[bufferIndex], ".%s", args->baseHost);
        
        // the payload is crafted, now pack it into DNS format and send it
        static int chunkId = 0;
        dns_sender__on_chunk_encoded(args->dstFilepath, chunkId, buffer);
        sendQuery(sock, buffer);
        dns_sender__on_chunk_sent(&addr.sin_addr, args->dstFilepath, chunkId, chunkSize);
        fileSize += chunkSize;
        chunkId++;
    }
    
    close(sock);
    
    dns_sender__on_transfer_completed(args->dstFilepath, fileSize);
}
     
int main(int argc, char **argv) {
	Arguments args = { NULL };
    parseArguments(argc, argv, &args);
	
	FILE *inFile = stdin;
	if (args.srcFilepath) {
		FILE *f = fopen(args.srcFilepath, "r");
		if (f == NULL) {
			fprintf(stderr, "Could not open file `%s`\n", args.srcFilepath);
			exit(1);
		}
		inFile = f;
	}
	
	struct sockaddr_in upstreamAddr = {
        .sin_family = AF_INET,
        .sin_port = htons(53),
    };
	

    int status;
	if (args.upstreamIP) {
        status = inet_aton(args.upstreamIP, &upstreamAddr.sin_addr);
    } else {
        char defaultIP[MAX_IP_LEN];
        if (getDefaultIP(defaultIP, RESOLV_CONF)) {
            fprintf(stderr, "Could not get default nameserver from file `%s`.\n", RESOLV_CONF);
            exit(1);
        }
        status = inet_aton(defaultIP, &upstreamAddr.sin_addr);
    }
    if (status == 0) {
        fprintf(stderr, "Argument `%s` is not a valid adress.\n", args.upstreamIP);
        exit(1);
    }

    sendData(inFile, upstreamAddr, &args);
	
	fclose(inFile);
    return 0;
}

