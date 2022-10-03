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

void handleConnection(int conn, int counter) {
	exit(0);
}

void receiveConnections(int sock) {
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
			fprintf(stderr, "[%d]\tConnection started.", counter);
			// This function will never return
			handleConnection(conn, counter);
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

    

    close(sock);
	
    return 0;
}
