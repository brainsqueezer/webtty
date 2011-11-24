/***********************************************************************

webtty.c - using fifos to control applications running in a pseudo
terminal.

This file is part of the webtty package. Latest version can be found
at http://testape.com/webtty_sample.html. The webtty package was 
written by Martin Steen Nielsen. 

webtty is free software; you can redistribute it and/or modify it
under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

webtty is distributed in the hope that it will be useful,but
WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with webtty; if not, write to the Free Software Foundation,
Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA

***********************************************************************/

#include <unistd.h>
#include <sys/types.h>
#include <sys/time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pty.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <errno.h>
#include <time.h>
#include <syslog.h>

#define LID LOG_MAKEPRI(LOG_USER, LOG_DEBUG)

#define MAX_CHUNK 1000

//#define DEBUG syslog
#define DEBUG (void)

/***********************************************************************
wait 
***********************************************************************/
static int waitms(int ms)
    {
    struct timespec requested = { 0, ms*1e6  };
    struct timespec remaining;

    nanosleep(&requested, &remaining);

    return ms;
    }
    

/***********************************************************************
signal handler ( from GNU C Library documentation)
***********************************************************************/

static volatile sig_atomic_t fatal_error_in_progress = 0;
static int terminal, pid_terminal, pid_buffer, pid_fifo_out, pid_fifo_in;
static char *name_buffer, *name_fifo_in, *name_fifo_out, 
            *name_pid, *process_id, *name_admin;

static void fatal_error_signal (int sig)
    {
    /* Since this handler is established for more than one kind of signal,
    it might still get invoked recursively by delivery of some other kind
    of signal.  Use a static variable to keep track of that. */
    if (fatal_error_in_progress)
        raise (sig);
    fatal_error_in_progress = 1;

    /* allow client 5 second to fetch whatever 
       is there, before terminating */
    DEBUG(LID,"terminating in 5 sec");
    sleep(5);
    
    /* kill children */
    if (pid_terminal)
       {
       DEBUG(LID,"killing terminal %d",pid_terminal);
       kill(pid_terminal,sig);
       }
       
    if (pid_buffer)
       {
        DEBUG(LID,"killing buffer %d",pid_buffer);
        kill(pid_buffer,sig);
	}

    if (pid_fifo_out)
       {
        DEBUG(LID,"killing out %d",pid_fifo_out);
        kill(pid_fifo_out,sig);
	}

    if (pid_fifo_in)
       {
        DEBUG(LID,"killing in %d",pid_fifo_in);
        kill(pid_fifo_in,sig);
	}

    /* cleanup files */
    DEBUG(LID,"clean up");
    unlink(name_buffer);
    unlink(name_fifo_in);
    unlink(name_fifo_out);
    unlink(name_pid);

    /* cleanup memory */
    free(name_buffer);
    free(name_fifo_in);
    free(name_fifo_out);
    free(name_pid);
    free(name_admin);

    closelog();
    free(process_id);

    /* Now reraise the signal.  We reactivate the signal's
    default handling, which is to terminate the process.
    We could just call exit or abort,
    but reraising the signal sets the return status
    from the process correctly. */
    signal (sig, SIG_DFL);
    raise (sig);
    }

/***********************************************************************
exit error function
***********************************************************************/
static void exit_error(char *error_text)
    {
    fprintf(stderr, "failed - %s\n",error_text);
    DEBUG( LID, "failed - %s",error_text);
    _exit(1);
    }



/***********************************************************************
wait for input function ( from GNU C library documentation )
***********************************************************************/

static int input_timeout (int filedes, unsigned int milli_seconds)
    {
    char buf;
    fd_set set;
    struct timeval timeout;
    int res,r;

   /* Initialize the file descriptor set. */
    FD_ZERO (&set);
    FD_SET (filedes, &set);

    /* Initialize the timeout data structure. */
    timeout.tv_sec = 0;
    timeout.tv_usec = 1000 * milli_seconds;

    /* select returns 0 if timeout, 1 if input available, -1 if error. */
    return select (FD_SETSIZE, &set, NULL, NULL, &timeout);
    }



/***********************************************************************
handle terminal
***********************************************************************/

static int handle_terminal (int *terminal, char **argv)
    {
    int pid = forkpty (terminal, 0, 0, 0);

    if ( -1 == pid )
        /* forkpty failed */
        exit_error("could not fork or more pseudo terminals available");

    /* are we parent ? */
    if (  0 != pid )
        /* yes - return with child pid */
        return pid;

    /* remove cr/lf translation from terminal */
    struct termios settings;
    tcgetattr (STDOUT_FILENO, &settings);
    settings.c_oflag &= ~OPOST;
    settings.c_oflag &= ~ONLCR;
    tcsetattr (STDOUT_FILENO, TCSANOW, &settings);

    /* stdin/stdout/stderr is connected to fd. Launch application */
    execvp(argv[0],&argv[0]);

    DEBUG( LID, "Could not execute\n");
    /* application terminated - die and return */
    close(*terminal);
    _exit (1);
    }



/***********************************************************************
handle output buffer - copies from terminal to file buffer
***********************************************************************/

static int handle_output_buffer (int terminal_out, char *buffer)
    {
    int pid, terminal_buffer = open(buffer,O_WRONLY|O_CREAT,0777);

    if ( -1 == terminal_buffer )
        /* failed */
        exit_error("creating buffer <id>_out failed");

    pid = fork();

    if ( -1 == pid )
        /* failed */
        exit_error("fork failed");

    /* are we parent ? */
    if (  0 != pid )
        /* yes - return with child pid */
        return pid;

    /* copy from pseudo terminal to write buffer.
    Exit when ternminal dies or when killed by parent */
    while (-1 != input_timeout(terminal_out,0))
        {
        char buf;

        if ( 1 == input_timeout(terminal_out,100) )
            {
            int res = read(terminal_out,&buf,1);

            /* Reading from pseudo terminal in loop until closed */
            if ( -1 == res)
                break;
            if ( 0 ==res )
                waitms(100);
            else
	       /* keep writing to pseudo terminal */
               write(terminal_buffer,&buf,1);
            }
        }
    /* close file */
    close(terminal_buffer);
    /* terminate process */
    _exit(1);
    }


/***********************************************************************
handle output fifo
***********************************************************************/

static int handle_output_fifo (char *name_buffer, char *name_fifo_out)
    {
    int data_count, time_count, sres, fifo_out, admin_out, pid, buffer;

    buffer = open(name_buffer,O_RDONLY);
    if ( -1 == buffer )
        /* failed */
        exit_error("open buffer <id>_out failed");

    pid = fork();

    if ( -1 == pid )
        /* failed */
        exit_error("fork failed");

    /* are we parent ? */
    if (  0 != pid )
        /* yes - return with child pid */
        return pid;

    /* no - We're the output handling child. */
    while(1)
        {
        char *outbuf;
	int time=0;

        /* Open output fifo - will block until connected */
        DEBUG( LID, "output: waiting for connection");

        do
	    {
	    /* start with pause, to allow close operation to recover, before
	       reopening */
	    time += waitms(100);

	    /* Open in non - blocking will return -1 if not connected */
	    fifo_out = open(name_fifo_out,O_WRONLY|O_NONBLOCK);

	    /* Have we been here for 10 sec ? */
	    if (time>10000)
                {
                /* Yes - give up and terminate */
                DEBUG( LID, "output: giving up waiting");
                close(buffer);
                _exit(1);
                }
	    /* Keep going until client is connected */  
            } while( -1 == fifo_out);

        DEBUG( LID, "output: connected\n");

        /* zero counters */
        data_count = 0; time_count = 0;

        outbuf = (char*)malloc(MAX_CHUNK);
        while(1)
            {
            DEBUG(LID, "output: time is %d ms, data is %d bytes",time_count, data_count);

            if (1 != read(buffer,&outbuf[data_count],1))
                {
                /* Yes - simply make 50 ms pause */
                /* consider feeding browser 0's to keep connection */
                time_count += waitms(50);
                }
            else
                {
                if (data_count==0) 
		    time_count=0;
		data_count++;
                }

            /* give the browser manageable chunks */
            if (data_count>=MAX_CHUNK)
                {
                DEBUG(LID, "output: max data reached");
                break;
                }
            /* avoid timeout in browser, if no data */
            if (time_count>10000)
                {
                DEBUG(LID, "output: max time reached");
                break;
                }

            /* Transmit data after 200 ms */
            if (data_count && time_count)
                {
                DEBUG(LID, "output: timeout after %d ms (%d bytes)",time_count, data_count);
                break;
                }
            }

        if (data_count != 0 )
	  {
	  /* write data to fifo */
          DEBUG(LID, "output: writing %d bytes",data_count);
          write(fifo_out,outbuf,data_count);

  	  /* make a blocking lock request on admin file */
          admin_out = open(name_admin, O_WRONLY|O_CREAT|O_APPEND, -1);
 	  struct flock fl = { F_WRLCK };
	  fcntl (admin_out, F_SETLKW, &fl);
	
          /* duplicate data to admin file */
          char size_buf[6];
 	  sprintf(size_buf,"%.5d",pid_terminal);
  	  write(admin_out,size_buf,5);
	  write(admin_out,":",1);

 	  sprintf(size_buf,"%.4d",data_count);
	  write(admin_out,size_buf,4);
	  write(admin_out,":",1);
          write(admin_out,outbuf,data_count);

          /* Free and close fifo and adminstrative file */
  	  struct flock fu = { F_UNLCK };
	  fcntl (admin_out, F_SETLKW, &fl);
	  close(admin_out);
	  }
        free(outbuf);
	close (fifo_out);
        }

    close(buffer);
    _exit(1);
    }



/***********************************************************************
handle input fifo
***********************************************************************/

static int handle_input_fifo (char *name_fifo_in, int terminal)
    {
    int pid = fork();

    if ( -1 == pid )
        /* failed */
        exit_error("fork failed");

    /* are we parent ? */
    if (  0 != pid )
        /* yes - return with child pid */
        return pid;

    /* no - we're child. loop on open and read pipe. Exit when killed
    by parent only or when pipe disappears */
    while(1)
        {
        char buf;
        DEBUG(LID, "input: waiting");
        /* Open input pipe */
        int fifo_in = open(name_fifo_in,O_RDONLY,0);
        DEBUG(LID, "input: connected");
        /* error ? */
        if (fifo_in == -1)
            /* Yes - die */
            exit_error("input pipe not found");

        /* No - Reading from pipe in loop until closed */
        while (1 == read(fifo_in,&buf,1))
            /* keep writing to pseudo terminal */
            write(terminal,&buf,1);
        /* close this end of pipe */
        close(fifo_in);
        }
    _exit(1);
    }



/***********************************************************************
   exit help function
 ***********************************************************************/

static void exit_help(void)
    {
    /* Display the help */
    printf("This program will launch a command in a pseudo terminal. Data for the\n");
    printf("pseudo terminal can be controlled using fifo's.\n\n");
            
    printf("Usage:\n");
    printf("  webtty <id> <admin_file> <application> [parameters]\n\n");

    printf("Input to process in pseudo terminal can be sent writing to fifo <id>_in.\n");
    printf("Different controlling processes can");
    printf("share the fifo.\n The application will re-attach to the fifo input file every \n");
    printf("time it is closed.\n\n");
    printf("Output from process in pseudo terminal can be read from fifo <id>_out.\n");
    printf("Output fifo will never deliver more than 1000 bytes at a time. The controlling\n");
    printf("process can reattach several times to the fifo to get more data. If there is\n");
    printf("no data, the process will block for max 5 seconds or until data is available.\n\n");

    printf("Example:\n");
    printf("  webtty key1234 admin_log bash\n");
    printf("  webtty key1234 admin_log traceroute testape.com\n");
    exit(1);
    }



/***********************************************************************
process arguments and control process and pipe connetions
***********************************************************************/

int main(int argc, char **argv)
    {
    printf("webtty - monitor and control tty applications using fifos\n\n");

    /* Is there no args or is '--help' '-?' or '-h' present ? */
    if ((1 == argc) || strstr(argv[1],"--help-?-h"))
        /* Yes - display the help */
        exit_help();
    else
        {
        FILE *f;
	int dying_status, dying_pid;
	
        /* No - run the program */
        name_buffer   = (char *)malloc(5+strlen(argv[1]));
        name_fifo_in  = (char *)malloc(5+strlen(argv[1]));
        name_fifo_out = (char *)malloc(5+strlen(argv[1]));
        name_pid      = (char *)malloc(5+strlen(argv[1]));
        name_admin    = (char *)malloc(1+strlen(argv[2]));
        process_id    = (char *)malloc(9+1+strlen(argv[1]));
	
        strcpy(name_admin, argv[2]);
	
	sprintf(process_id, "webtty[%s]",argv[1]);
        openlog(process_id,LOG_PERROR,0);	

        /* launch application */
        pid_terminal = handle_terminal(&terminal, &argv[3]);
        
        /* handover to main process id to php */
        strcpy(name_pid, argv[1]);
        strcat(name_pid, "_pid");
        f = fopen(name_pid,"w");
        fprintf(f,"%d\n",pid_terminal);
        fclose(f);

        /* launch buffer handling */
        strcpy(name_buffer,argv[1]);
        strcat(name_buffer,"_buf");
        pid_buffer = handle_output_buffer(terminal,name_buffer);

        /* launch fifo output handling */
        strcpy(name_fifo_out, argv[1]);
        strcat(name_fifo_out, "_out");
        mkfifo(name_fifo_out,0777);
        pid_fifo_out = handle_output_fifo(name_buffer, name_fifo_out);

        /* launch fifo input handling */
        strcpy(name_fifo_in, argv[1]);
        strcat(name_fifo_in, "_in");
        mkfifo(name_fifo_in,0777);
        pid_fifo_in = handle_input_fifo(name_fifo_in,terminal);

        DEBUG(LID, "new process t:%d b:%d i:%d o:%d",pid_terminal,pid_buffer,pid_fifo_in,pid_fifo_out);
        /* establish signal handler */
        if (signal (SIGINT, fatal_error_signal) == SIG_IGN)
            signal (SIGINT, SIG_IGN);
        if (signal (SIGHUP, fatal_error_signal) == SIG_IGN)
            signal (SIGHUP, SIG_IGN);
        if (signal (SIGTERM, fatal_error_signal) == SIG_IGN)
            signal (SIGTERM, SIG_IGN);
        /* wait for any children to die */
        int dying_id =  waitpid (-1, 0, 0);
	DEBUG(LID, "dying process %d",dying_id);
	if (pid_fifo_in == dying_id)
	  pid_fifo_in=0;
	if (pid_fifo_out == dying_id)
	  pid_fifo_out=0;
	if (pid_buffer == dying_id)
	  pid_buffer=0;
	if (pid_terminal == dying_id)
	  pid_terminal=0;
	  
        /* cleanup and kill rest of children in signal handler */
	raise (SIGTERM);
	/* wait for all to die, before returning */
	wait (0);
        }
    DEBUG(LID, "ended");
    }
