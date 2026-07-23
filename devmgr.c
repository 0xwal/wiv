#ifdef __FreeBSD__
#define __BSD_VISIBLE 1
#endif
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "devmgr.h"

int devmgr_start(int *fd, pid_t *pid, const char *devpath) {
	(void)devpath;
	const char *testpath = "/dev/input/event0";
	int testfd = open(testpath, O_RDONLY|O_CLOEXEC|O_NOCTTY|O_NONBLOCK);
	if (testfd < 0) {
		fprintf(stderr, "wshowkeys: can't read /dev/input/event0 — "
			"try 'sudo usermod -aG input $USER' then log out and back in\n");
		return 1;
	}
	close(testfd);
	*fd = -1;
	*pid = 0;
	return 0;
}

int devmgr_open(int sockfd, const char *path) {
	(void)sockfd;
	return open(path, O_RDONLY|O_CLOEXEC|O_NOCTTY|O_NONBLOCK);
}

void devmgr_finish(int sock, pid_t pid) {
	(void)sock;
	(void)pid;
}