#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <sys/mount.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/param.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <signal.h>
#include <sched.h>
#include <sys/socket.h>

extern int pivot_root(const char *new_root, const char *put_old);
static int prepare_mntns()
{
	int fd, ret;
	char *root;
	char path[PATH_MAX];

	root = getenv("ZDTM_ROOT");
	if (!root) {
		fprintf(stderr, "ZDTM_ROOT isn't set\n");
		return -1;
	}

	if (mount("none", "/", "none", MS_REC|MS_PRIVATE, NULL)) {
		fprintf(stderr, "Can't remount root with MS_PRIVATE: %m\n");
		return -1;
	}

	if (mount(root, root, NULL, MS_BIND | MS_REC, NULL)) {
		fprintf(stderr, "Can't bind-mount root: %m\n");
		return -1;
	}

	printf("chdir(%s)\n", root);
	if (chdir(root)) {
		fprintf(stderr, "chdir(%s) failed: %m\n", root);
		return -1;
	}
	if (mkdir("old", 0777) && errno != EEXIST) {
		fprintf(stderr, "mkdir(old) failed: %m\n");
		return -1;
	}

	printf("pivot_root(., ./old)\n");
	if (pivot_root(".", "./old")) {
		fprintf(stderr, "pivot_root(., ./old) failed: %m\n");
		return -1;
	}
	chdir("/");
	if (mkdir("proc", 0777) && errno != EEXIST) {
		fprintf(stderr, "mkdir(proc) failed: %m\n");
		return -1;
	}

	printf("Mount /proc\n");
	if (mount("proc", "/proc", "proc", MS_MGC_VAL, NULL)) {
		fprintf(stderr, "mount(/proc) failed: %m\n");
		return -1;
	}
	printf("Open the mount point, where is a target file\n");
	fd = open("/old/tmp/xxx", O_DIRECTORY | O_RDONLY);
	if (fd < 0) {
		fprintf(stderr, "open(/old/mnt/xxx) failed: %m\n");
		return -1;
	}

	printf("Detach mounts from the previous mount namespace\n");
	if (umount2("./old", MNT_DETACH)) {
		fprintf(stderr, "umount(./old) failed: %m\n");
		return -1;
	}

	printf("Try to read the target file\n");
	snprintf(path, sizeof(path), "/proc/self/fd/%d/yyy/zzz", fd);
	fd = open(path, O_RDONLY);
	if (fd < 0)
		return 0;

	printf("We have done this\n");
	ret = read(fd, path, sizeof(path) - 1);
	if (ret < 0) {
		fprintf(stderr, "read() failed: %m\n");
		return -1;
	}
	path[ret] = 0;
	fprintf(stderr, "%s\n", path);

	return -1;
}

#define NS_STACK_SIZE	4096

/* All arguments should be above stack, because it grows down */
struct ns_exec_args {
	char stack[NS_STACK_SIZE];
	char stack_ptr[0];
	int argc;
	char **argv;
	int status_pipe[2];
};

int ns_exec(void *_arg)
{
	struct ns_exec_args *args = (struct ns_exec_args *) _arg;
	char buf[4096];
	int ret;

	close(args->status_pipe[0]);

	setsid();

	read(args->status_pipe[1], buf, sizeof(buf));
	shutdown(args->status_pipe[1], SHUT_RD);
	if (setuid(0) || setgid(0)) {
		fprintf(stderr, "set*id failed: %m\n");
		return -1;
	}

	if (prepare_mntns())
		return -1;

	return 0;
}

#define ID_MAP "0 10000 100000"
void main(int argc, char **argv)
{
	pid_t pid;
	char pname[PATH_MAX];
	int ret, status;
	struct ns_exec_args args;
	int fd, flags;

	args.argc = argc;
	args.argv = argv;

	ret = socketpair(AF_UNIX, SOCK_SEQPACKET, 0, args.status_pipe);
	if (ret) {
		fprintf(stderr, "Pipe() failed %m\n");
		exit(1);
	}

	flags = CLONE_NEWPID | CLONE_NEWNS | CLONE_NEWUTS |
		CLONE_NEWNET | CLONE_NEWIPC | CLONE_NEWUSER | SIGCHLD;

	printf("Unshare namespaces\n");
	pid = clone(ns_exec, args.stack_ptr, flags, &args);
	if (pid < 0) {
		fprintf(stderr, "clone() failed: %m\n");
		exit(1);
	}

	close(args.status_pipe[1]);

	snprintf(pname, sizeof(pname), "/proc/%d/uid_map", pid);
	fd = open(pname, O_WRONLY);
	if (fd < 0) {
		fprintf(stderr, "open(%s): %m\n", pname);
		exit(1);
	}
	if (write(fd, ID_MAP, sizeof(ID_MAP)) < 0) {
		fprintf(stderr, "write(" ID_MAP "): %m\n");
		exit(1);
	}
	close(fd);

	snprintf(pname, sizeof(pname), "/proc/%d/gid_map", pid);
	fd = open(pname, O_WRONLY);
	if (fd < 0) {
		fprintf(stderr, "open(%s): %m\n", pname);
		exit(1);
	}
	if (write(fd, ID_MAP, sizeof(ID_MAP)) < 0) {
		fprintf(stderr, "write(" ID_MAP "): %m\n");
		exit(1);
	}
	close(fd);

	shutdown(args.status_pipe[0], SHUT_WR);

	wait(&status);

	exit(status == 0 ? 0 : 1);
}

