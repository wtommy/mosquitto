#include <errno.h>
#include <stdio.h>
#include <sys/select.h>
#include <unistd.h>

/* pselect loop test */
int main(int argc, char *argv[])
{
	struct timespec timeout;
	fd_set readfds, writefds;
	int fdcount;
	char buf[1024];
	int run = 1;

	while(run){
		FD_ZERO(&readfds);
		FD_SET(0, &readfds);
		FD_ZERO(&writefds);
		//FD_SET(0, &writefds);
		timeout.tv_sec = 1;
		timeout.tv_nsec = 0;

		fdcount = pselect(1, &readfds, &writefds, NULL, &timeout, NULL);
		if(fdcount == -1){
			fprintf(stderr, "Error in pselect: %s\n", strerror(errno));
			run = 0;
		}else if(fdcount == 0){
			printf("loop timeout\n");
		}else{
			printf("fdcount=%d\n", fdcount);

			if(FD_ISSET(0, &readfds)){
				read(0, buf, 1024);
				printf("buf: %s\n", buf);
			}
		}
	}
	return 0;
}

