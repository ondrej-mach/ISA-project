#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include <unistd.h>
#include <errno.h>

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "dns_receiver_events.h"

#define BUFFER_SIZE 512
#define FILENAME_DELIM 'x'
#define LABEL_LENGTH 63
#define CONN_TIMEOUT 10


typedef struct {
    char *baseHost;
    char *dstFilepath;
} Arguments;

void usage() {
    fprintf(stderr, "Usage:\ndns_receiver {BASE_HOST} {DST_FILEPATH}\n");
}

void parseArguments(int argc, char **argv, Arguments *args) {
	if (argc != 3) {
		usage();
		exit(1);
	}
	
	args->baseHost = argv[1];
	args->dstFilepath = argv[2];
}

// Basically the same as finding postfix but it checks for the dot before postfix
bool isSubdomain(char *str, char *postfix) {
	int i=0, j=0;
	while (str[i]) {
		i++;
	}
	
	while (postfix[j]) {
		j++;
	}
	
	if (i <= j) {
		return false;
	}
	
	while (str[i] == postfix[j]) {
		if (j == 0) {
			// check for the dot before domain
			return str[--i] == '.';
		} else {
			i--;
			j--;
		}
	}
	
	return false;
}


bool isHex(char c) {
	return ((c >= '0') && (c <= '9')) || ((c >= 'a') && (c <= 'f'));
}

char hexToNum(char c) {
	if ((c >= '0') && (c <= '9')) {
		return c - '0';
	} else {
		return c + 10 - 'a';
	}
}

char hexToByte(char upper, char lower) {
	return hexToNum(upper)*16 + hexToNum(lower);
}

// decoded buffer should be exactly as long as encoded
// limit sets maximum length of encoded buffer
// Set limit to 0 to end at first \0
// returns number of processed characters in encoded array
int hexDecode(char *encoded, char *decoded, int *decodedLen) {
	int encodedIndex = 0;
	
	char firstChar = '\0';
	while (true) {
		char c = encoded[encodedIndex];
		
		if (!isHex(c)) {
			break;
		} 
		
		if (encodedIndex%2 == 0) {
			firstChar = c;
		} else {
			decoded[encodedIndex/2] = hexToByte(firstChar, c);
		}
		
		encodedIndex++;
	}
	decoded[encodedIndex/2] = '\0';
	if (decodedLen) {
		*decodedLen = encodedIndex/2;
	}
	return (encodedIndex/2)*2;
}

// returns 0 on success
// payload is the url as \0 terminated string
int receiveRequest(int conn, char *payload, int counter) {
	struct timeval tv = { .tv_sec = CONN_TIMEOUT, .tv_usec = 0 };
	int status = setsockopt(conn, SOL_SOCKET, SO_RCVTIMEO, (const char *)&tv, sizeof(tv));
	if (status != 0) {
		fprintf(stderr, "[%d]\tCould not set socket options.\n", counter);
	}
	
	char buffer[BUFFER_SIZE];
	// Skip headers, go directly to payload
	int bufferIndex = 12;
	int payloadIndex = 0;
	int labelRemaining = 0;
	int packetSize;
	int n;
	bool firstLabel = true;
	
	n = read(conn, buffer, 2);
	if (n != 2) {
		return 1;
	}
	packetSize = htons(*(uint16_t *)buffer);
	
	n = read(conn, buffer, packetSize);
	if (n != packetSize) {
		return 1;
	}
	
	while (bufferIndex < packetSize) {
		if (labelRemaining == 0) {
			// This is the place where we read the length of next label
			labelRemaining = buffer[bufferIndex];
			// If we read 0, then this was the last label
			if (labelRemaining == 0) {
				payload[payloadIndex] = '\0';
				return 0;
			} else {
				if (firstLabel) {
					firstLabel = false;
				} else {
					// There is next label coming, so we write . as delimiter
					payload[payloadIndex] = '.';
					payloadIndex++;
				}
			}
		} else {
			payload[payloadIndex] = buffer[bufferIndex];
			labelRemaining--;
			payloadIndex++;
		}
		bufferIndex++;
	}
	return 1;
}

void handleConnection(int conn, int counter, Arguments *args, struct in_addr *source) {
	char hexEncoded[BUFFER_SIZE];
	char payload[BUFFER_SIZE];
	FILE *f = NULL;
	bool failed = false;
	
	char filePath[LABEL_LENGTH]; // just for printing
	int fileSize = 0;
	int chunkId = 0;
	
	while (true) {
		if (receiveRequest(conn, hexEncoded, counter) != 0) {
			fprintf(stderr, "[%d]\tCommunication with the client failed.\n", counter);
			break;
		}
		
		int processed = 0;
		// Process this request only if it has the correct domain name
		if (!failed && isSubdomain(hexEncoded, args->baseHost)) {
			// If file is not yet open, we need to open it
			if (f == NULL) {
				// Process the payload into filename
				processed = hexDecode(hexEncoded, payload, NULL);
				if (hexEncoded[processed] != FILENAME_DELIM) {
					failed = true;
					fprintf(stderr, "[%d]\tWrong format\n", counter);
					continue;
				}
				processed++;
				
				strcpy(filePath, payload);
				if (strchr(filePath, '/')) {
					fprintf(stderr, "[%d]\tFilename `%s` is unsafe, exiting.\n", counter, filePath);
					break;
				}
				
				fprintf(stderr, "[%d]\tOpening file `%s`\n", counter, filePath);
				f = fopen(payload, "w");
				if (f == NULL) {
					failed = true;
					fprintf(stderr, "[%d]\tCould not open file `%s` for writing\n", counter, payload);
					continue;
				}
			}
			
			dns_receiver__on_query_parsed(filePath, chunkId, hexEncoded);
			
			// Decode the chunk
			int decodedLen;
			processed += hexDecode(&hexEncoded[processed], payload, &decodedLen);
			// Write it to the destination file
			fwrite(payload, decodedLen, 1, f);
			
			dns_receiver__on_chunk_received(source, filePath, chunkId, decodedLen);
			chunkId++;
			fileSize += decodedLen;
			
			if (hexEncoded[processed] != '.') {
				if (hexEncoded[processed] == FILENAME_DELIM) {
					break;
				} else {
					fprintf(stderr, "[%d]\tBad encoding: %c\n", counter, hexEncoded[processed]);
					failed = true;
					continue;
				}
			}
		}
	}
	
	if (f) {
		fprintf(stderr, "[%d]\tClosing file.\n", counter);
		fclose(f);
	}
	fprintf(stderr, "[%d]\tClosing connection.\n", counter);
	close(conn);
	dns_receiver__on_transfer_completed(filePath, fileSize);
	exit(0);
}

void receiveConnections(int sock, Arguments *args) {
	int counter = 1;
	
	while (true) {
		struct sockaddr_in clientAddr;
		int len = sizeof(struct sockaddr_in);
		int conn = accept(sock, (struct sockaddr *)&clientAddr, &len);
		
		if (conn == -1) {
			fprintf(stderr, "Exiting...\n");
			break;
		}
		
		if (fork() == 0) {
			close(sock);
			dns_receiver__on_transfer_init(&clientAddr.sin_addr);
			fprintf(stderr, "[%d]\tConnection started.\n", counter);
			// This function will never return
			handleConnection(conn, counter, args, &clientAddr.sin_addr);
		} else {
			close(conn);
			counter++;
		}
	}
}


int main(int argc, char **argv) {
	Arguments args = { NULL };
    parseArguments(argc, argv, &args);
	
	// Check if you have write access to destination folder
	if (access(args.dstFilepath, W_OK) != 0) {
		fprintf(stderr, "Cannot write into folder `%s`\n", args.dstFilepath);
		exit(1);
	}
	chdir(args.dstFilepath);
	
	// Create the socket
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == -1) {
        fprintf(stderr, "Could not create socket.\n");
        exit(1);
    }

    // Set correct addresses
    struct sockaddr_in srvAddr = {
		.sin_family = AF_INET,
		.sin_addr.s_addr = htonl(INADDR_ANY),
		.sin_port = htons(53),
	};

	// Bind to the port
    if (bind(sock, (struct sockaddr *)&srvAddr, sizeof(struct sockaddr_in)) != 0) {
		fprintf(stderr, "Could not bind to the socket.\n");
        exit(1);
    }

    if ((listen(sock, 10)) != 0) {
        fprintf(stderr, "Could not listen on the socket.\n");
        exit(1);
    }
    
	fprintf(stderr, "Listening for new connections...\n");
	receiveConnections(sock, &args);

    close(sock);
	
    return 0;
}
