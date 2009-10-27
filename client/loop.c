#include <stdio.h>
#include <sys/select.h>

/* pselect loop test */
int main(int argc, char *argv[])
{
	struct timespec timeout;
	fd_set readfds;
	int fdcount;
	char buf[1024];

	while(1){
		FD_ZERO(&readfds);
		FD_SET(0, &readfds);
		timeout.tv_sec = 1;
		timeout.tv_nsec = 0;

		fdcount = pselect(1, &readfds, NULL, NULL, &timeout, NULL);
		printf("loop %d\n", fdcount);

		if(FD_ISSET(0, &readfds)){
			read(0, buf, 1024);
			printf("buf: %s\n", buf);
		}
	}
	return 0;
}

