#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include <unistd.h>
#include <errno.h>

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>


#define BUFFER_SIZE 512
#define FILENAME_DELIM "x"
#define LABEL_LENGTH 63


typedef struct {
    char *baseHost;
    char *upstreamIP;
    char *dstFilepath;
    char *srcFilepath;
} Arguments;


void usage() {
    fprintf(stderr, "Usage:\ndns_sender [-b BASE_HOST] [-u UPSTREAM_DNS_IP] {DST_FILEPATH} [SRC_FILEPATH]\n");
}

void parseArguments(int argc, char **argv, Arguments *args) {
	int c;
	while ((c = getopt(argc, argv, "b:u:")) != EOF) {
		switch (c) {
			case 'b':
				args->baseHost = optarg;
				break;
			
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
    
    if (args->baseHost == NULL) {
        usage();
        exit(1);
    }
    
    // parse remaining 1 or 2 positional arguments
    if (argc >= 1 && argc <= 2) {
        // mandatory dst file name
        args->dstFilepath = argv[0];
        // optional src file
        if (argc >= 2) {
            args->srcFilepath = argv[1];
        }
    } else {
        usage();
        exit(1);
    }
    
}

int sendQuery(int sock, char *payload) {
    char buffer[BUFFER_SIZE];
    // LENGTH - used only in DNS over TCP
    uint16_t *dnsLength = &((uint16_t *)buffer)[0];
    // ID
    ((uint16_t *)buffer)[1] = ntohs(0);
    // FLAGS
    ((uint16_t *)buffer)[2] = ntohs(0x120);
    // QDCOUNT
    ((uint16_t *)buffer)[3] = ntohs(1);
    // ANCOUNT
    ((uint16_t *)buffer)[4] = ntohs(0);
    // NSCOUNT
    ((uint16_t *)buffer)[5] = ntohs(0);
    // ARCOUNT
    ((uint16_t *)buffer)[6] = ntohs(0);
    
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
    *(uint16_t *)&buffer[bufferIndex] = ntohs(1);
    bufferIndex += 2;
    // QCLASS
    *(uint16_t *)&buffer[bufferIndex] = ntohs(1);
    bufferIndex += 2;
    
    *dnsLength = ntohs(bufferIndex-2);
    
    if (write(sock, buffer, bufferIndex) < bufferIndex) {
        fprintf(stderr, "Error while sending data.\n");
        exit(1);
    }
}

int sendData(FILE *in, struct sockaddr_in addr, Arguments *args) {
    int sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    
    if (connect(sock, (struct sockaddr *) &addr, sizeof(addr)) < 0) {
        fprintf(stderr, "Connection to server `%s` failed.", inet_ntoa(addr.sin_addr));
        exit(1);
    }
    
    // In first phase, the filename is printed
    // After that, the content is printed
    bool printingFilename = true;
    bool finished = false;
    int inputIndex = 0;
    
    while (!finished) {
        
        char buffer[BUFFER_SIZE];
        int bufferIndex = 0;
        int c;
        int max = LABEL_LENGTH/2;
        
        while (bufferIndex <= max) {
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
                }
            }
            
            sprintf(&buffer[bufferIndex], "%02x", c);
            bufferIndex += 2;
            inputIndex++;
        }
        
        sprintf(&buffer[bufferIndex], ".%s", args->baseHost);
        
        // the payload is crafted, now pack it into DNS format and send it
        sendQuery(sock, buffer);
    }
    
    close(sock);
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
	
	if (args.upstreamIP) {
        int status = inet_aton(args.upstreamIP, &upstreamAddr.sin_addr);
        if (status == 0) {
            fprintf(stderr, "Argument `%s` is not a valid adress.\n", args.upstreamIP);
            exit(1);
        }
    } else {
        inet_aton("127.0.0.53", &upstreamAddr.sin_addr);
    }
    
    sendData(inFile, upstreamAddr, &args);
	
	fclose(inFile);
    return 0;
}
