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

bool isHex(char c) {
	return ((c >= '0') && (c <= '9')) || ((c >= 'a') && (c <= 'f'));
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

// decoded buffer should be exactly as long as encoded
// limit sets maximum length of encoded buffer
// Set limit to 0 to end at first \0
// returns number of processed characters in encoded array

int hexDecode(char *encoded, char *decoded, int limit) {
	int encodedIndex = 0;
	int decodedIndex = 0;
	
	char firstChar = '\0';
	while ((limit == 0) || (encodedIndex < limit)) {
		char c = encoded[encodedIndex];
		
		if (!isHex(c)) {
			if (firstChar != '\0') {
				encodedIndex--;
			}
			break;
		}
		
	}
	return encodedIndex;
	
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
	
	int bufferIndex = 18;
	int payloadIndex = 0;
	int receivedBytes = 0;
	int labelRemaining = 0;
	bool finished = false;
	
	while (!finished) {
		int n = read(conn, &buffer[bufferIndex], BUFFER_SIZE-receivedBytes);
		if (n == 0) {
// TODO chyba tady
			fprintf(stderr, "[%d]\tConnection timed out.\n", counter);
			return 1;
		}
		receivedBytes += n;
		
		while (bufferIndex < receivedBytes) {
			if (labelRemaining < 1) {
				labelRemaining = buffer[bufferIndex];
				if (labelRemaining == 0) {
					finished = true;
					break;
				} else {
					payload[payloadIndex] = '.';
					payloadIndex++;
				}
			} else {
				payload[payloadIndex] = buffer[bufferIndex];
				labelRemaining--;
				payloadIndex++;
			}
			bufferIndex++;
		}
		
	}
	
}

void handleConnection(int conn, int counter, Arguments *args) {
	char hexEncoded[LABEL_LENGTH];
	char payload[LABEL_LENGTH];
	FILE *f = NULL;
	bool failed = false;
	
	while (true) {
		if (receiveRequest(conn, hexEncoded, counter) != 0) {
			fprintf(stderr, "[%d]\tCommunication with the client failed.\n", counter);
			break;
		}
		
		if (hexDecode(hexEncoded, payload, 0)) {
			fprintf(stderr, "[%d]\tWrong format, could not decode from hex\n", counter);
			break;
		}
		
		// Process this request only if it has the correct domain name
		if (!failed && isSubdomain(payload, args->baseHost)) {
			char *contentPtr = payload;
			
			// If file is not yet open, we need to open it
			if (f == NULL) {
				// Process the payload into filename and content of the file
				int i = 0;
				while (true) {
					// handling wrong format
					if (payload[i] == '\0') {
						fprintf(stderr, "[%d]\tWrong format\n", counter);
						failed = true;
						break;
					}
					// we have found the end of the filename
					if (payload[i] == FILENAME_DELIM) {
						payload[i] = '\0';
						contentPtr = &payload[i+1];
						break;
					}
					// nothing interestning, go to the next character
					i++; 
				}
				
				if (!failed) {
					fprintf(stderr, "[%d]\tOpening file `%s`\n", counter, payload);
					f = fopen(payload, "w");
					if (f == NULL) {
						failed = true;
						fprintf(stderr, "[%d]\tCould not open file `%s` for writing\n", counter, payload);
					}
				}
			
				if (f) {
					fprintf(f, "%s", contentPtr);
				}
			}
		}
	}
	
	fclose(f);
	fprintf(stderr, "[%d]\tFile received, closing connection.\n", counter);
	close(conn);
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
			fprintf(stderr, "[%d]\tConnection started.\n", counter);
			// This function will never return
			handleConnection(conn, counter, args);
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
