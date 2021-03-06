/*
* Copyright (C) 2015  Arjun Sreedharan
* License: GPL version 2 or higher http://www.gnu.org/licenses/gpl.html
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <pthread.h>

#define BUFF_SIZE 256
#define USERNAME_MAX_SIZE 20

static unsigned short port = 55555;
static char username[USERNAME_MAX_SIZE];

/*
* Conditional variables (@console_cv) permit us to wait until
* another thread completes an arbitrary activity.
* A mutex (@console_cv_lock) is required to protect the condition
* variable itself from race condition.
*/
pthread_cond_t console_cv;
pthread_mutex_t console_cv_lock;

void error(void)
{
	fprintf(stderr, "%s\n", "bad command\n"
		"syntax: [command] [optional recipient] [optional msg]");
}

void console(int sockfd)
{
	char buffer[BUFF_SIZE];
	char *recipient, *msg, *tmp;

	memset(buffer, 0, sizeof buffer);
	printf("%s\n%s\n", "Welcome to chat client console. Please enter commands",
		"syntax: [command] [optional recipient] [optional msg]");

	/*
	* Issue the prompt and wait for command,
	* process the command and
	* repeat forever
	*/
	while(1) {
		/* console prompt */
		printf("[%s]$ ", username);
		fgets(buffer, sizeof buffer, stdin);
		/* fgets also reads the \n from stdin, strip it */
		buffer[strlen(buffer) - 1] = '\0';

		if(strcmp(buffer, "") == 0)
			continue;

		if(strncmp(buffer, "exit", 4) == 0) {
			/* tell server to clean up structures for the client */
			write(sockfd, "exit", 6);
			/* clean up self and exit */
			pthread_mutex_destroy(&console_cv_lock);
			pthread_cond_destroy(&console_cv);
			_exit(EXIT_SUCCESS);
		}

		/*
		* `ls` is sent to server to get list of connected users.
		* It is written to server's socket, then using conditional wait,
		* we `wait` until the reply arrives in the receiver thread, where
		* `signal` is done immediately when the reply is read
		*/
		if(strncmp(buffer, "ls", 2) == 0) {
			/*
			* The mutex the protects the conditional has to
			* be locked before a conditional wait.
			*/
			pthread_mutex_lock(&console_cv_lock);
			write(sockfd, "ls", 2);
			/* not protected from spurious wakeups */
			/*
			* This operation unlocks the given mutex and waits until a 
			* pthread_cond_signal() happens on the same conditonal variable.
			* Then the given mutex is again unlocked
			*/
			pthread_cond_wait(&console_cv, &console_cv_lock);
			/* release the mutex */
			pthread_mutex_unlock(&console_cv_lock);
			continue;
		}

		/* `send <recipient> <msg>` sends <msg> to the given <username> */
		if(strncmp(buffer, "send ", 5) == 0) {
			/* the following is to validate the syntax */
			tmp = strchr(buffer, ' ');
			if(tmp == NULL) {
				error();
				continue;
			}
			recipient = tmp + 1;

			tmp = strchr(recipient, ' ');
			if(tmp == NULL) {
				error();
				continue;
			}
			msg = tmp + 1;

			/* issue the `send` command to server */
			write(sockfd, buffer, 5 + strlen(recipient) + 1 + strlen(msg) + 1);
			continue;
		}

		error();
	}
}

/*
* write username to server
* in the syntax: register username <username>
*/
void register_username(int sockfd)
{
	char *regstring = malloc(USERNAME_MAX_SIZE + 18);
	sprintf(regstring, "register username %s", username);
	write(sockfd, regstring, strlen(regstring));
	free(regstring);
}

/*
* the stupid receiver thread
* It continuously waits for messages from the server,
* and prints it whatever it is.
*/
void *receiver(void *sfd)
{
	char buffer[BUFF_SIZE] = {0};
	int sockfd = *(int*)sfd;
	int readlen;
	while(1) {
		memset(buffer, 0, sizeof buffer);
		readlen = read(sockfd, buffer, sizeof buffer);
		if(readlen < 1)
			continue;
		pthread_mutex_lock(&console_cv_lock);
		printf("%s\n", buffer);
		/* let the other thread stop waiting, if it is */
		pthread_cond_signal(&console_cv);
		pthread_mutex_unlock(&console_cv_lock);
	}
}

int main(void)
{
	int sockfd;
	/*
	* this is the container for socket's address that contains
	* address family, ip address, port
	*/
	struct sockaddr serv_addr;
	char filler[16] = {0};
	/* just to dump the handle for the spawned thread - no use */
	pthread_t receiver_thread;

	pthread_cond_init(&console_cv, NULL);
	pthread_mutex_init(&console_cv_lock, NULL);

	/*
	* creates a socket of family Internet sockets (AF_INET) and
	* of type stream. 0 indicates to system to choose appropriate
	* protocol (eg: TCP)
	*/
	sockfd = socket(AF_INET, SOCK_STREAM, 0);

	/*
	* Address represented by struct sockaddr:
	* first 2 bytes: Address Family,
	* next 2 bytes: port,
	* next 4 bytes: ipaddr,
	* next 8 bytes: zeroes
	*/
	/*
	* htons() and htonl() change endianness to
	* network order which is the standard for network
	* communication.
	*/
	filler[0] = AF_INET & 0xFF;
	filler[1] = AF_INET >> 8 & 0xFF;
	filler[2] = htons(port) & 0xFF;
	filler[3] = htons(port) >> 8 & 0xFF;
	filler[4] = htonl(INADDR_ANY) & 0xFF;
	filler[5] = htonl(INADDR_ANY) >> 8 & 0xFF;
	filler[6] = htonl(INADDR_ANY) >> 16 & 0xFF;
	filler[7] = htonl(INADDR_ANY) >> 24 & 0xFF;

	/*
	* The following method of memcpy-ing is a little risky.
	* It's best done using a structure sockaddr_in like:
	* struct sockaddr_in serv_addr;
	* serv_addr.sin_family = AF_INET;
	* serv_addr.sin_port = htons(port);
	* serv_addr.sin_addr.s_addr = INADDR_ANY;
	* Why didn't I use sockaddr_in?
	* * sockaddr_in is just a wrapper around sockaddr
	* * Functions like connect() do not know of any type sockaddr_in
	* This is just to demonstrate how socket address is read
	*/
	/* copy data in the filler buffer to the socket address */
	memcpy(&serv_addr, filler, sizeof serv_addr);

	/* binds a socket to an address */
	bind(sockfd, &serv_addr, sizeof serv_addr);

	/* makes connection per the socket address */
	connect(sockfd, &serv_addr, sizeof serv_addr);

	printf("%s\n", "Enter a username (max 20 characters, no spaces):");
	fgets(username, sizeof username, stdin);
	/* fgets also reads the \n from stdin, strip it */
	username[strlen(username) - 1] = '\0';

	register_username(sockfd);
	/* spawn a new thread that continuously listens for any msgs from server */
	pthread_create(&receiver_thread, NULL, receiver, (void*)&sockfd);
	/* get our console in action, let the user enter commands */
	console(sockfd);

	return 0;
}
