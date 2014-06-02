#include <sys/wait.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/syslog.h>
#include <stdio.h>
#include <stdlib.h>
#include <termios.h>
#include <getopt.h>
#include <string.h>
#include <unistd.h>

char *keyringmgr[] = {
    "/usr/bin/python", "keyringmgr.py",
    "--get", 0, "--stdin", 0};

#define SOCKET_PATH     "/tmp/mykeyring"  /* addr to connect */

void rexec(char **argv, FILE **in, FILE **out) {

#define READ_FD  0
#define WRITE_FD 1
#define PARENT_READ_FD  (pr[READ_FD])
#define PARENT_WRITE_FD (pw[WRITE_FD])
#define CHILD_READ_FD   (pw[READ_FD])
#define CHILD_WRITE_FD  (pr[WRITE_FD])

    int pr[2], pw[2];
    pipe(pr);
    pipe(pw);
    if(!fork()) {

        dup2(CHILD_READ_FD, STDIN_FILENO);
        dup2(CHILD_WRITE_FD, STDOUT_FILENO);
        close(CHILD_READ_FD);
        close(CHILD_WRITE_FD);
        close(PARENT_READ_FD);
        close(PARENT_WRITE_FD);
        execv(argv[0], argv);
    }
    close(CHILD_READ_FD);
    close(CHILD_WRITE_FD);
    *in = fdopen(PARENT_READ_FD, "r");
    *out = fdopen(PARENT_WRITE_FD, "w");
}

void get_passwd(char *username, char *lpass, char *pass) {
    FILE *in, *out;
    __WAIT_STATUS status;
    keyringmgr[3] = username;
    rexec(keyringmgr, &in, &out);
    fprintf(out, "%s\n", lpass);
    fflush(out);
    fscanf(in, "%s", pass);
    wait(status);
}

char lpass[1024], username[1024], password[1024];

void open_server_socket(int *s_) {
    socklen_t len;
    struct sockaddr_un saun;
    if ((*s_ = socket(AF_UNIX, SOCK_STREAM, 0)) < 0)
    {
        perror("server: socket");
        exit(1);
    }
    saun.sun_family = AF_UNIX;
    strcpy(saun.sun_path, SOCKET_PATH);
    unlink(SOCKET_PATH);
    len = (socklen_t)(sizeof(saun.sun_family) + strlen(saun.sun_path));
    if (bind(*s_, (struct sockaddr *) &saun, len) < 0)
    {
        perror("server: bind");
        exit(1);
    }
    if (listen(*s_, 5) < 0) {
        perror("server: listen");
        exit(1);
    }
}

void daemonize() {
    pid_t sid = setsid();
    if (sid < 0)
    {
        syslog(LOG_ERR, "Could not create process group\n");
        exit(1);
    }
    umask(0);
    close(STDIN_FILENO);
    close(STDOUT_FILENO);
    close(STDERR_FILENO);
    syslog(LOG_NOTICE, "Successfully started\n");
}

void run_server() {
    FILE *fp;
    int s, sd;
	struct termios sg; 
    struct sockaddr_un fsaun;
    socklen_t fromlen;

	tcgetattr(STDIN_FILENO, &sg); /* get settings */
	sg.c_lflag &= ~(unsigned int)ECHO; /* turn echo off */ 
	tcsetattr(STDIN_FILENO,TCSAFLUSH,&sg); /* set settings */

    fprintf(stderr, "Password: ");
    scanf("%s", lpass);
    fprintf(stderr, "\n");

	tcgetattr(STDIN_FILENO, &sg); /* get settings */
	sg.c_lflag |= ECHO; /* turn echo on */
	tcsetattr(STDIN_FILENO,TCSAFLUSH,&sg); /* set settings */

    daemonize();
    open_server_socket(&s);
    for (;;)
    {
        int op_code;
        char *p;
        if ((sd = accept(s, (struct sockaddr *) &fsaun, &fromlen)) < 0)
        {
            perror("server: accept");
            exit(1);
        }
        fp = fdopen(sd, "r");
        op_code = fgetc(fp);
        if (op_code)
        {
            for (p = username; (*p = (char)fgetc(fp)) > 0; p++);
            if (*p < 0)
            {
                syslog(LOG_ERR, "Wrong string format\n");
                continue;
            }
            else
                syslog(LOG_NOTICE, "Acquiring the password for %s\n", username);
        }
        else
        {
            syslog(LOG_NOTICE, "Shutting down...\n");
            exit(0); /* terminated */
        }
        memset(password, 0, sizeof password);
        get_passwd(username, lpass, password);
        send(sd, password, strlen(password) + 1, 0);
    }
    close(s);
}

void open_client_socket(int *s) {
    socklen_t len;
    struct sockaddr_un saun;
    if ((*s = socket(AF_UNIX, SOCK_STREAM, 0)) < 0)
    {
        perror("client: socket");
        exit(1);
    }
    saun.sun_family = AF_UNIX;
    strcpy(saun.sun_path, SOCKET_PATH);
    len = (socklen_t)(sizeof(saun.sun_family) + strlen(saun.sun_path));

    if (connect(*s, (struct sockaddr *) &saun, len) < 0)
    {
        if (!fork()) run_server();
        while (connect(*s, (struct sockaddr *) &saun, len) < 0)
            sleep(1);
    }
}

void run_client(char *username) {
    FILE *fp;
    int s;
    char *p;
    open_client_socket(&s);
    send(s, "1", 1, 0);
    send(s, username, strlen(username) + 1, 0);
    fp = fdopen(s, "r");
    for (p = password; (*p = (char)fgetc(fp)); p++);
    puts(password);
    close(s);
}

void stop_server() {
    int s;
    open_client_socket(&s);
    send(s, "\0", 1, 0);
    close(s);
}

static struct option lopts[] = {
    { "forget", no_argument, 0, 'f'},
    { "help", no_argument, 0, 'h'},
    {0, 0, 0, 0}
};

void print_help() {
}

int mode = 1;
int main(int argc, char **argv) {
    struct rlimit rlim;
    int option_index = 0;

    getrlimit(RLIMIT_CORE, &rlim);
    rlim.rlim_max = rlim.rlim_cur = 0;
    if(setrlimit(RLIMIT_CORE, &rlim))
        exit(-1);
    mlock(password, sizeof password);
    while (1)
    {
        int c = getopt_long(argc, argv, "ah", lopts, &option_index);
        if (c == -1)
            break;
        switch (c)
        {
            case 0: break;
            case 'f': mode = 0; break;
            case 'h': print_help(); return 0;
            default: print_help(); return 0;
        }
    }
    if (mode == 0)
    {
        stop_server();
        return 0;
    }
    if (optind != argc - 1)
    {
        fprintf(stderr, "One username is expected.\n");
        return 1;
    }
    run_client(strdup(argv[optind]));
    return 0;
}
