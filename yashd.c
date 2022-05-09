/**
 * @file TCPServer-ex2.c
 * @brief The program creates a TCP socket in
 * the inet domain and listens for connections from TCPClients, accept clients
 * into private sockets, and fork an echo process to ``serve'' the
 * client.  If [port] is not specified, the program uses any available
 * port.
 * Run as:
 *     TCPServer-ex2 <port>
 */
/*
NAME:
SYNOPSIS:    TCPServer [port]

DESCRIPTION:

*/
#include <stdio.h>
/* socket(), bind(), recv, send */
#include <sys/types.h>
#include <sys/socket.h> /* sockaddr_in */
#include <netinet/in.h> /* inet_addr() */
#include <arpa/inet.h>
#include <netdb.h>  /* struct hostent */
#include <string.h> /* memset() */
#include <unistd.h> /* close() */
#include <stdlib.h> /* exit() */
#include <stdio.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <pthread.h>
#include <time.h>
#include <semaphore.h>
#include <errno.h> 

#define MAXHOSTNAME 80
#define HARDCODEDPORT 3826
#define BUFSIZE 1024
void reusePort(int sock);
//void *EchoServe(int psd, struct sockaddr_in from);
void *EchoServe(void *arg);
void getTime(int psd, struct sockaddr_in from);

// DAEMON STUFF
#define PATHMAX 255
static char sg_u_server_path[PATHMAX + 1] = "/tmp"; /* default */
static char sg_u_log_path[PATHMAX + 1];
static char sg_u_std_log_path[PATHMAX + 1];
static char sg_u_pid_path[PATHMAX + 1];

//Thread Setup
#define NUM_THREADS 2

static volatile int count=0;
pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;

typedef struct _thread_data_t {
    int psd;
    struct sockaddr_in from;
} thread_data_t;

struct proc_info{
    pthread_t my_tid;
    int my_socket;
    int shell_pid;
    int pthread_pipe_fd[2];
};

//globalVariables
    char buf[512];
	char tbuf[512];
	    time_t current_time;
    char *c_time_string;
	int pid_ch1 = -1, pid_ch2 = -1, pid = -1;

struct proc_info *proc_info_table;
int table_index_counter = 0;
//Function Declarations 
char **parseLine(char *line);
void cleanup();
void getTime();
/**
 * @brief  If we are waiting reading from a pipe and
 *  the interlocutor dies abruptly (say because
 *  of ^C or kill -9), then we receive a SIGPIPE
 *  signal. Here we handle that.
 */
void sig_pipe(int n)
{
    perror("Broken pipe signal");
}

/**
 * @brief Handler for SIGCHLD signal
 */
void sig_chld(int n)
{
    int status;

    fprintf(stderr, "Child terminated\n");
    wait(&status); /* So no zombies */
}

/**
 * @brief Initializes the current program as a daemon, by changing working
 *  directory, umask, and eliminating control terminal,
 *  setting signal handlers, saving pid, making sure that only
 *  one daemon is running. Modified from R.Stevens.
 * @param[in] path is where the daemon eventually operates
 * @param[in] mask is the umask typically set to 0
 */
void daemon_init(const char *const path, uint mask)
{
    pid_t pid;
    char buff[256];
    static FILE *log; /* for the log */
    int fd;
    int k;

    printf("Starting Daemon Init\n");
    /* put server in background (with init as parent) */
    if ((pid = fork()) < 0)
    {
        perror("daemon_init: cannot fork");
        exit(0);
    }
    else if (pid > 0) /* The parent */
        exit(0);

    /* the child */

    /* Close all file descriptors that are open */
    for (k = getdtablesize() - 1; k > 0; k--)
        if(k != 1 && k != 2) close(k);

    //printf("Closed all Fds\n");

    /* Redirecting stdin and stdout to /dev/null */
    if ((fd = open("/dev/null", O_RDWR)) < 0)
    {
        perror("Open");
        exit(0);
    }
    dup2(fd, STDIN_FILENO); /* detach stdin */
    close(fd);
    /* From this point on printf and scanf have no effect */

    /* Redirecting stderr to sg_u_log_path */
    log = fopen(sg_u_std_log_path, "aw"); /* attach stderr to sg_u_log_path */
    fd = fileno(log);                     /* obtain file descriptor of the log */
    dup2(fd, STDOUT_FILENO);
    close(fd);
    
    /* From this point on printing to stderr will go to /tmp/u-echod.log */

    /* Redirecting stderr to sg_u_log_path */
    log = fopen(sg_u_log_path, "aw"); /* attach stderr to sg_u_log_path */
    fd = fileno(log);                 /* obtain file descriptor of the log */
    dup2(fd, STDERR_FILENO);
    close(fd);
    /* From this point on printing to stderr will go to /tmp/u-echod.log */

    printf("Redirect STDOUT working\n");
    perror("Redirect STDERR working\n");

    /* Establish handlers for signals */
    if (signal(SIGCHLD, sig_chld) < 0)
    {
        perror("Signal SIGCHLD");
        exit(1);
    }
    if (signal(SIGPIPE, sig_pipe) < 0)
    {
        perror("Signal SIGPIPE");
        exit(1);
    }

    /* Change directory to specified directory */
    chdir(path);

    /* Set umask to mask (usually 0) */
    umask(mask);

    /* Detach controlling terminal by becoming sesion leader */
    setsid();

    /* Put self in a new process group */
    pid = getpid();
    setpgrp(); /* GPI: modified for linux */

    /* Make sure only one server is running */
    if ((k = open(sg_u_pid_path, O_RDWR | O_CREAT, 0666)) < 0)
        exit(1);
    if (lockf(k, F_TLOCK, 0) != 0)
        exit(0);

    /* Save server's pid without closing file (so lock remains)*/
    sprintf(buff, "%6d", pid);
    write(k, buff, strlen(buff));

    printf("Finished Daemon Init\n");

    return;
}

int main(int argc, char **argv)
{
    int sd, psd;
    struct sockaddr_in server;
    struct hostent *hp, *gethostbyname();
    struct sockaddr_in from;
    int fromlen;
    int length;
    char ThisHost[80];
    int pn = server.sin_port = htons(HARDCODEDPORT);
    ;
    int childpid;

    strncat(sg_u_server_path, "/", PATHMAX - strlen(sg_u_server_path));
    strncat(sg_u_server_path, argv[0], PATHMAX - strlen(sg_u_server_path));
    strcpy(sg_u_pid_path, sg_u_server_path);
    strncat(sg_u_pid_path, ".pid", PATHMAX - strlen(sg_u_pid_path));
    strcpy(sg_u_std_log_path, sg_u_server_path);
    strncat(sg_u_std_log_path, ".txt", PATHMAX - strlen(sg_u_std_log_path));
    strcpy(sg_u_log_path, sg_u_server_path);
    strncat(sg_u_log_path, ".log", PATHMAX - strlen(sg_u_log_path));
    daemon_init(sg_u_server_path, 0);

    /* get TCPServer1 Host information, NAME and INET ADDRESS */
    gethostname(ThisHost, MAXHOSTNAME);
    /* OR strcpy(ThisHost,"localhost"); */

    fprintf(stdout, "----TCP/Server running at host NAME: %s\n", ThisHost);
    if ((hp = gethostbyname(ThisHost)) == NULL)
    {
        fprintf(stderr, "Can't find host %s\n", argv[1]);
        exit(-1);
    }
    bcopy(hp->h_addr, &(server.sin_addr), hp->h_length);
    fprintf(stdout, "    (TCP/Server INET ADDRESS is: %s )\n", inet_ntoa(server.sin_addr));

    /** Construct name of socket */
    server.sin_family = AF_INET;
    /* OR server.sin_family = hp->h_addrtype; */

    server.sin_addr.s_addr = htonl(INADDR_ANY);
    if (argc == 1)
        server.sin_port = htons(HARDCODEDPORT);
    else
    {
        server.sin_port = pn;
    }

    /** Create socket on which to send  and receive */

    sd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    /* OR sd = socket (hp->h_addrtype,SOCK_STREAM,0); */
    if (sd < 0)
    {
        perror("opening stream socket");
        exit(-1);
    }
    /** this allow the server to re-start quickly instead of waiting
    for TIME_WAIT which can be as large as 2 minutes */
    reusePort(sd);
    if (bind(sd, (struct sockaddr *)&server, sizeof(server)) < 0)
    {
        close(sd);
        perror("binding name to stream socket");
        exit(-1);
    }

    /** get port information and  prints it out */
    length = sizeof(server);
    if (getsockname(sd, (struct sockaddr *)&server, &length))
    {
        perror("getting socket name");
        exit(0);
    }
	
    fprintf(stdout, "Server Port is: %d\n", ntohs(server.sin_port));

//fprintf(stdout,"testing threads");
    /** accept TCP connections from clients and fork a process to serve each */
	 proc_info_table = malloc(sizeof(struct proc_info) * 500);
    listen(sd, 4);
    fromlen = sizeof(from);

    for (;;)// create thread here
    {
		pthread_t thr; //[NUM_THREADS]
		thread_data_t thr_data;
        ssize_t rc;
				
		//cleanup(buf);
				
        psd = accept(sd, (struct sockaddr *)&from, &fromlen);
		
		thr_data.from = from;
        thr_data.psd = psd;
        /*childpid = fork();
        if (childpid == 0) Going away from fork and moving to Thread 
        {
            close(sd);
            EchoServe(psd, from);
        }*/
		proc_info_table[table_index_counter].my_socket = psd;

		 if ((rc = pthread_create(&thr, NULL, EchoServe, &thr_data))) {
            fprintf(stderr, "error: pthread_create, rc: %d\n", (int) rc);
            close(psd);
            return EXIT_FAILURE;
		 }
		 /*
        else//parent
        {
            //fprintf(stdout, "My new child pid is %d\n", rc);
            close(psd);
        }*/
    }
}

//void *EchoServe(int psd, struct sockaddr_in from)
void *EchoServe(void *arg)
{
	printf("thread created");
	thread_data_t *data = (thread_data_t *)arg;
	int psd = data->psd;
    struct sockaddr_in from = data->from;
fprintf(stdout, "Server Port 2 is: %d\n", psd);
	proc_info_table[table_index_counter].my_tid = pthread_self();
    ssize_t rc; //int to ssize
	char **args;
    char *buf_copy;
    struct hostent *hp, *gethostbyname();
	int new_pid, epid;
	char **fixed_args;

    printf("Serving %s:%d\n", inet_ntoa(from.sin_addr),
           ntohs(from.sin_port));
    if ((hp = gethostbyaddr((char *)&from.sin_addr.s_addr,
                            sizeof(from.sin_addr.s_addr), AF_INET)) == NULL)
        fprintf(stderr, "Can't find host %s\n", inet_ntoa(from.sin_addr));
    else
        printf("(Name is : %s)\n", hp->h_name);
	//Child to handle executes
	/*
    new_pid = fork();
    if(new_pid == 0) {
		printf("testing");
	}
	
	else if(new_pid >0){
    /**  get data from  client and send it back */
	
    for (;;)
    {
		bzero(buf, 1024);
		strcpy(buf, "\n#");
		send(psd, buf, sizeof(buf), 0);
        printf("\n...server is waiting...\n");
        if ((rc = recv(psd, buf, sizeof(buf), 0)) < 0)
        {
            perror("receiving stream  message");
            exit(-1);
        }
        if (rc > 0)
        {
            buf[rc] = '\0';
			buf_copy = strdup(buf);
			args = parseLine(buf_copy);
			 if (strcmp(args[0], "CMD") == 0) {
            fixed_args = &args[1];
			}
			
			if (strcmp(args[0], "CTL") == 0) {
				
				if (strcmp(args[1], "c") == 0) {
                    fprintf(stderr,"sending sig int to pid %d\n", pid_ch1);
                    kill(pid_ch1, SIGINT);
                 }
                 if (strcmp(args[1], "z") == 0) {
                        fprintf(stderr,"sending sig tstp to pid %d\n", pid_ch1);
                        kill(pid_ch1, SIGTSTP);
                 }
				
			}

                if (strcmp(args[0], "CMD") == 0) {
                    //write_to_log(buf, (size_t) rc, inet_ntoa(from.sin_addr), ntohs(from.sin_port));
					printf("testing %s",args[1]);
					//args[2] = NULL;
					getTime(psd, from);
                    printf("%s",buf);
                    fflush(stdout);
					
					epid = fork();
					if (epid == 0){
						//close(psd);
					dup2(psd, STDOUT_FILENO);
					//printf("testing %s",fixed_args[0]);
					 execvp(fixed_args[0], fixed_args);
					 
					//send(psd, tbuf, sizeof(tbuf), 0);
					fflush(stdout);
					}
                }

            printf("Received: %s\n", buf);
            printf("From TCP/Client: %s:%d\n", inet_ntoa(from.sin_addr),
                   ntohs(from.sin_port));
            printf("(Name is : %s)\n", hp->h_name);
            if (send(psd, buf, rc, 0) < 0)
                perror("sending stream message");
        }
        else
        {
            printf("TCP/Client: %s:%d\n", inet_ntoa(from.sin_addr),
                   ntohs(from.sin_port));
            printf("(Name is : %s)\n", hp->h_name);
            printf("Disconnected..\n");
            close(psd);
            exit(0);
        }
    }
	//}
}
void cleanup(char *buf)
{
    int i;
    for(i=0; i<BUFSIZE; i++) buf[i]='\0';
}
void reusePort(int s)
{
    int one = 1;

    if (setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (char *)&one, sizeof(one)) == -1)
    {
        printf("error in setsockopt,SO_REUSEPORT \n");
        exit(-1);
    }
}

//Get time and format 
void getTime(int psd, struct sockaddr_in from){
current_time = time(NULL);
    if (current_time == ((time_t)-1)){
        (void) fprintf(stderr, "Failure to obtain the current time.\n");
        return;
    }

    /* Convert to local time format. */
    c_time_string = ctime(&current_time);
    size_t string_size = strlen(c_time_string);
    c_time_string[string_size-1] = '\0';
    char yashd_str[] = " yashd[";
    size_t yashd_size = strlen(yashd_str);
    size_t host_addr_size = strlen(inet_ntoa(from.sin_addr));//hp
    size_t port_length = sizeof( ntohs(from.sin_port));
    size_t final_string_size = string_size + yashd_size + host_addr_size + 5 + BUFSIZE + port_length;
    char port_string[port_length];
    sprintf(port_string, "%d",  ntohs(from.sin_port));
    char *final_string = calloc(final_string_size, sizeof(char));
    char *colon = ":";
    char *right_brace = "]";
    char *space = " ";

/*inet_ntoa(from.sin_addr),inet_ntoa(from.sin_addr)
                   ntohs(from.sin_port));
				   */

    strcat(final_string, c_time_string);
    strcat(final_string, yashd_str);
    strcat(final_string, inet_ntoa(from.sin_addr)); //hp
    strcat(final_string, colon);
    strcat(final_string, port_string);
    strcat(final_string, right_brace);
    strcat(final_string, colon);
    strcat(final_string, space);
    strcat(final_string, buf);
	printf(final_string);
}

// the following line parser was taken from https://brennan.io/2015/01/16/write-a-shell-in-c/
#define LSH_TOK_BUFSIZE 64
#define LSH_TOK_DELIM " \t\r\n\a"
char **parseLine(char *line)
{
    int bufsize = LSH_TOK_BUFSIZE, position = 0;
    char **tokens = malloc(bufsize * sizeof(char*));
    char *token;

    if (!tokens) {
        fprintf(stderr, "lsh: allocation error\n");
        exit(EXIT_FAILURE);
    }

    token = strtok(line, LSH_TOK_DELIM);
    while (token != NULL) {
        tokens[position] = token;
        position++;

        if (position >= bufsize) {
            bufsize += LSH_TOK_BUFSIZE;
            tokens = realloc(tokens, bufsize * sizeof(char*));
            if (!tokens) {
                fprintf(stderr, "lsh: allocation error\n");
                exit(EXIT_FAILURE);
            }
        }

        token = strtok(NULL, LSH_TOK_DELIM);
    }
    tokens[position] = NULL;
    return tokens;
}
