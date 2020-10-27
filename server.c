#include "threadpool/threadpool.h"
#include "utils.h"
#include "string_util.h"
#include <assert.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

const char *GET = "GET";

const char *INDEX_PAGE = "/static/index.html";

const int NUM_THREAD = 4;

const int MAX_CWD = 100;

void writeln_to_socket(int sockfd, const char *message) {
	write(sockfd, message, strlen(message));
	write(sockfd, "\r\n", 2);
}

void write_content_to_socket(int sockfd, const char *content) {
	char length_str[100];
	sprintf(length_str, "%d", (int) strlen(content));

	char *content_length_str = concat("Content-Length: ", length_str);
	writeln_to_socket(sockfd, "Content-Type: text/html");
	writeln_to_socket(sockfd, content_length_str);
	writeln_to_socket(sockfd, "");
	writeln_to_socket(sockfd, content);

	free(content_length_str);
}

void http_404_reply(int sockfd) {
	writeln_to_socket(sockfd, "HTTP/1.1 404 Not Found");

	static
	const char *content = "<html><body> <h1>Not found</h1></body></html>\r\n";
	write_content_to_socket(sockfd, content);
}

void http_get_reply(int sockfd, const char *content) {
	writeln_to_socket(sockfd, "HTTP/1.1 200 OK");
	write_content_to_socket(sockfd, content);
}

int is_get(char *text) {
	return starts_with(text, GET);
}

char *get_path(char *text) {
	int beg_pos = strlen(GET) + 1;
	char *end_of_path = strchr(text + beg_pos, ' ');
	int end_pos = end_of_path - text;

	int pathlen = end_pos - beg_pos;
	char *path = malloc(pathlen + 1);
	substr(text, beg_pos, pathlen, path);
	path[pathlen] = '\0';

	return path;
}

char *read_file(FILE *fpipe) {
	int capacity = 10;
	char *buf = malloc(capacity);
	int index = 0;

	int c;
	while ((c = fgetc(fpipe)) != EOF) {
		assert(index < capacity);
		buf[index++] = c;

		if (index == capacity) {
			char *newbuf = malloc(capacity *2);
			memcpy(newbuf, buf, capacity);
			free(buf);
			buf = newbuf;
			capacity *= 2;
		}
	}

	buf[index] = '\0';
	return buf;
}

void output_static_file(int sockfd, const char *curdir, const char *path) {
	char *fullpath = malloc(strlen(curdir) + strlen(path) + 1);
	strcpy(fullpath, curdir);
	strcat(fullpath, path);
	FILE *f = fopen(fullpath, "r");
	if (!f) {
		fprintf(stderr, "resource not found: %s\n", path);
		http_404_reply(sockfd);
	}
	else {
		char *result = read_file(f);
		http_get_reply(sockfd, result);
		free(result);
	}
}

void handle_socket_thread(void *sockfd_arg) {
	int sockfd = *((int*) sockfd_arg);
	char *text = read_text_from_socket(sockfd);
	if (is_get(text)) {
		char curdir[MAX_CWD];

		if (!getcwd(curdir, MAX_CWD)) {
			error("Couldn't read curdir");
		}

		char *path = get_path(text);

		if (strcmp(path, "/") == 0) {
			output_static_file(sockfd, curdir, INDEX_PAGE);
		} else if (strcmp(path, "/hello") == 0) {
			http_get_reply(sockfd, "hello");
		} else {
			output_static_file(sockfd, curdir, path);
		}
		free(path);
	}
	else {
		// We only support GET
		http_404_reply(sockfd);
	}

	free(text);
	close(sockfd);
	free(sockfd_arg);
}

int create_listening_socket(unsigned short port) {
	int sockfd = socket(AF_INET, SOCK_STREAM, 0);
	if (sockfd < 0) {
		error("ERROR opening socket");
	}
	int setopt = 1;

	// Reuse the port. Otherwise, on restart, port 8000 is usually still occupied for a bit
	// and we need to start at another port
	if (-1 == setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, (char*) &setopt, sizeof(setopt))) {
		error("ERROR setting socket options");
	}

	struct sockaddr_in serv_addr;

	while (1) {
		bzero(&serv_addr, sizeof(serv_addr));
		serv_addr.sin_family = AF_INET;
		serv_addr.sin_port = htons(port);
		serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);

		if (bind(sockfd, (struct sockaddr *) &serv_addr, sizeof(serv_addr)) < 0) {
			port++;
		}
		else {
			break;
		}
	}

	if (listen(sockfd, SOMAXCONN) < 0) error("Couldn't listen");
	printf("Running on port: %d\n", port);

	return sockfd;
}

int main(int argc, char** argv) {
	// Parse args
    if (argc != 2) {
        fprintf(stderr, "usage: %s [port]\n", argv[0]);
        exit(1);
    }
	int sockfd = create_listening_socket((unsigned short) atoi(argv[1]));

	struct sockaddr_in client_addr;
	int cli_len = sizeof(client_addr);

	threadpool thpool = thpool_init(NUM_THREAD);

	while (1) {
		int newsockfd = accept(sockfd, (struct sockaddr *) &client_addr, (socklen_t*) &cli_len);
		if (newsockfd < 0) error("Error on accept");
		printf("New socket: %d\n", newsockfd);

		int *arg = malloc(sizeof(int));
		*arg = newsockfd;
		thpool_add_work(thpool, &handle_socket_thread, (void *) (arg));
	}
	close(sockfd);

	return 0;
}