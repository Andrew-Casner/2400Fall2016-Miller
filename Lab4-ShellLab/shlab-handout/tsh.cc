//
// tsh - A tiny shell program with job control
//
// Nicholas Erokhin 104189096
// Andrew Casner 104261003
//

using namespace std;

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <errno.h>
#include <string>

#include "globals.h"
#include "jobs.h"
#include "helper-routines.h"

//
// Needed global variable definitions
//

static char prompt[] = "tsh> ";
int verbose = 0;

//
// You need to implement the functions eval, builtin_cmd, do_bgfg,
// waitfg, sigchld_handler, sigstp_handler, sigint_handler
//
// The code below provides the "prototypes" for those functions
// so that earlier code can refer to them. You need to fill in the
// function bodies below.
//

void eval(char *cmdline);
int builtin_cmd(char **argv);
void do_bgfg(char **argv);
void waitfg(pid_t pid);

void sigchld_handler(int sig);
void sigtstp_handler(int sig);
void sigint_handler(int sig);

//
// main - The shell's main routine
//
int main(int argc, char **argv)
{
  int emit_prompt = 1; // emit prompt (default)

  //
  // Redirect stderr to stdout (so that driver will get all output
  // on the pipe connected to stdout)
  //
  dup2(1, 2);

  /* Parse the command line */
  char c;
  while ((c = getopt(argc, argv, "hvp")) != EOF) {
    switch (c) {
    case 'h':             // print help message
      usage();
      break;
    case 'v':             // emit additional diagnostic info
      verbose = 1;
      break;
    case 'p':             // don't print a prompt
      emit_prompt = 0;  // handy for automatic testing
      break;
    default:
      usage();
    }
  }

  //
  // Install the signal handlers
  //

  //
  // These are the ones you will need to implement
  //
  Signal(SIGINT,  sigint_handler);   // ctrl-c
  Signal(SIGTSTP, sigtstp_handler);  // ctrl-z
  Signal(SIGCHLD, sigchld_handler);  // Terminated or stopped child

  //
  // This one provides a clean way to kill the shell
  //
  Signal(SIGQUIT, sigquit_handler);

  //
  // Initialize the job list
  //
  initjobs(jobs);

  //
  // Execute the shell's read/eval loop
  //
  for(;;) {
    //
    // Read command line
    //
    if (emit_prompt) {
      printf("%s", prompt);
      fflush(stdout);
    }

    char cmdline[MAXLINE];

    if ((fgets(cmdline, MAXLINE, stdin) == NULL) && ferror(stdin)) {
      app_error("fgets error");
    }
    //
    // End of file? (did user type ctrl-d?)
    //
    if (feof(stdin)) {
      fflush(stdout);
      exit(0);
    }

    //
    // Evaluate command line
    //
    eval(cmdline);
    fflush(stdout);
    fflush(stdout);
  }

  exit(0); //control never reaches here
}

/////////////////////////////////////////////////////////////////////////////
//
// eval - Evaluate the command line that the user has just typed in
//
// If the user has requested a built-in command (quit, jobs, bg or fg)
// then execute it immediately. Otherwise, fork a child process and
// run the job in the context of the child. If the job is running in
// the foreground, wait for it to terminate and then return.  Note:
// each child process must have a unique process group ID so that our
// background children don't receive SIGINT (SIGTSTP) from the kernel
// when we type ctrl-c (ctrl-z) at the keyboard.
//
void eval(char *cmdline)
{
	/* Parse command line */
	//
	// The 'argv' vector is filled in by the parseline
	// routine below. It provides the arguments needed
	// for the execve() routine, which you'll need to
	// use below to launch a process.
	char *argv[MAXARGS];
	// The 'bg' variable is TRUE if the job should run
	// in background mode or FALSE if it should run in FG
	int bg = parseline(cmdline, argv);
	struct job_t *job;
	// This initializes the signal set
	sigset_t mask; //I
	sigemptyset(&mask); //Initializes signal set "mask" to contain no signals
	sigaddset(&mask, SIGCHLD);

	// New process id
	pid_t pid;
	//No return if the argument is NULL
	if(argv[0] == NULL) return;
	// Filter out all commands that are not built in
	if (builtin_cmd(argv) == 0){
		sigprocmask(SIG_BLOCK, &mask, 0);
		//  Temporarily blocks SIGCHLD before we fork, blocks parent from child
		pid = fork();
		if(pid < 0){
			printf("Fork Failed");
			exit(0);
		}
		if(pid == 0){
			// Unblocks child after forking and before execv
			sigprocmask(SIG_UNBLOCK, &mask, NULL);
			//fork worked
			setpgid(0,0);
			execv(argv[0], argv);
			//coud not execute the command
			printf("%s: Command not found\n", argv[0]);
			exit(0);
		}
		// If the process has a "&" flag, it is set to run in the background
		if (bg){

			addjob(jobs, pid, BG, cmdline); //Adds job with background flag
			sigprocmask(SIG_UNBLOCK, &mask, NULL);
			//Unblock after addjob
			job = getjobpid(jobs, pid);
			printf("[%d] (%d) %s", job->jid, job->pid, cmdline); //Commandline output


		}
		// The process is a foreground process
		else{
			//job = getjobpid(jobs, pid);
			addjob(jobs, pid, FG, cmdline); //Adds job with foreground flag
			sigprocmask(SIG_UNBLOCK, &mask, NULL);
			//Unblock after addjob
		 	//Wait for the foreground process to compleate before going on
			waitfg(pid); //waits for foreground job to finish

		}
	}
	return;
	}


/////////////////////////////////////////////////////////////////////////////
//
// builtin_cmd - If the user has typed a built-in command then execute
// it immediately. The command name would be in argv[0] and
// is a C string. We've cast this to a C++ string type to simplify
// string comparisons; however, the do_bgfg routine will need
// to use the argv array as well to look for a job number.
//
int builtin_cmd(char **argv)
{
  // All the built in commands go here
  if(strcmp(argv[0],"quit") == 0){
    exit(0);
    return 1;
  }
  else if(strcmp(argv[0],"jobs") == 0){
    // print all of the jobs
    listjobs(jobs);
    return 1;
  }
  else if((strcmp(argv[0],"fg") == 0) || (strcmp(argv[0], "bg") == 0)) {
    do_bgfg(argv); //Calls fg/bg handler
    return 1;
}
  return 0;     /* not a builtin command */
}

/////////////////////////////////////////////////////////////////////////////
//
// do_bgfg - Execute the builtin bg and fg commands
//
void do_bgfg(char **argv)
{
  struct job_t *jobp=NULL;

  /* Ignore command if no argument */
  if (argv[1] == NULL) {
    printf("%s command requires PID or %%jobid argument\n", argv[0]);
    return;
  }

  /* Parse the required PID or %JID arg */
  if (isdigit(argv[1][0])) {
    pid_t pid = atoi(argv[1]);
    if (!(jobp = getjobpid(jobs, pid))) {
      printf("(%d): No such process\n", pid);
      return;
    }
  }
  else if (argv[1][0] == '%') {
    int jid = atoi(&argv[1][1]);
    if (!(jobp = getjobjid(jobs, jid))) {
      printf("%s: No such job\n", argv[1]);
      return;
    }
  }
  else {
    printf("%s: argument must be a PID or %%jobid\n", argv[0]);
    return;
  }

  //
  // You need to complete rest. At this point,
  // the variable 'jobp' is the job pointer
  // for the job ID specified as an argument.
  //
  // Your actions will depend on the specified command
  // so we've converted argv[0] to a string (cmd) for
  // your benefit.
  //
	string cmd(argv[0]);
	//If the command to bring the process to the foreground then
	//restart the process
	//set the state to "FG"
	//wait for the process to finish before going on
	if(cmd == "fg"){
		kill(-jobp->pid,SIGCONT); //Continues foreground job
		jobp->state = FG; //Changest state of job to foreground job
		waitfg(jobp->pid);
	}
	//If the command to movce the process to the background then
	//set the state to "BG"
	//print out background process
	if(cmd == "bg"){
		printf("[%d] (%d) %s", jobp->jid, jobp->pid, jobp->cmdline);
		jobp->state = BG; //Changes state to background
		
	}

	return;
}

/////////////////////////////////////////////////////////////////////////////
//
// waitfg - Block until process pid is no longer the foreground process
//
void waitfg(pid_t pid)
{
	while(pid == fgpid(jobs)){
		sleep(1); //sleeps while current job isnt running or foreground
	}
	return;
}

/////////////////////////////////////////////////////////////////////////////
//
// Signal handlers
//


/////////////////////////////////////////////////////////////////////////////
//
// sigchld_handler - The kernel sends a SIGCHLD to the shell whenever
//     a child job terminates (becomes a zombie), or stops because it
//     received a SIGSTOP or SIGTSTP signal. The handler reaps all
//     available zombie children, but doesn't wait for any other
//     currently running children to terminate.
//
void sigchld_handler(int sig)
{
	pid_t pid;
	//int jid = pid2jid(pid);
	int status; //

	/*
	First arg set to -1, because waitpid will then wait for any process to terminate

	" WNOHANG|WUNTRACED - Return immediately with value 0 if none of the child processes in the wait set have terminated.
	OR wait until process in wait set has terminated or stopped, then return pid of stopped/termed process that caused WUNTRACED to return. 
	Page 744-745 in textbook.

	If status (value pointed to by statusp,, then waitpid continues.	*/
	while((pid = waitpid(-1, &status, WNOHANG|WUNTRACED)) > 0){ 
		
		int jid = pid2jid(pid); //Declaring jid inside loop to get jid of stopped/termed process.
		
		if(WIFEXITED(status)){
			deletejob(jobs, pid); //Executes if child terminated normally with exit()
		}
	    else if(WIFSIGNALED(status)){
	        //printf("Debug sigchld term\n"); //True if child was terminated by an unhandled signal.
			printf("Job [%d] (%d) terminated by signal %d\n",jid, pid, WTERMSIG(status));
			deletejob(jobs, pid);
		}
		else if(WIFSTOPPED(status)){
			//printf("Debug sigchld stop"); //True if child was stopped by delivery of a signal. (WUNTRACED)
			printf("Job [%d] (%d) stopped by signal %d\n",jid, pid, WSTOPSIG(status));
			getjobpid(jobs, pid)->state = ST;
		}

	}
//	if(pid<0 && errno != ECHILD){//if pid invalid and error is other thn echild
//		printf("pid error: %s\n", strerror(errno));
//	}

	return;
}

/////////////////////////////////////////////////////////////////////////////
//
// sigint_handler - The kernel sends a SIGINT to the shell whenver the
//    user types ctrl-c at the keyboard.  Catch it and send it along
//    to the foreground job.
//
void sigint_handler(int sig)
{
	pid_t pid = fgpid(jobs);
	int jid = pid2jid(pid);
	//int jid = pid2jid(pid);
	if(pid == 0){
		//There is no foreground Process
		return;
	}
	else{
		//There is a foreground Process
	    //printf("Job [%d] (%d) terminated by signal %d\n",jid, pid, sig);
		//Reap it
		kill(-pid, SIGINT);
		//Remove the job from the joblist
		//deletejob(jobs,pid);
		return;

	}
	return;
}

/////////////////////////////////////////////////////////////////////////////
//
// sigtstp_handler - The kernel sends a SIGTSTP to the shell whenever
//     the user types ctrl-z at the keyboard. Catch it and suspend the
//     foreground job by sending it a SIGTSTP.
//
void sigtstp_handler(int sig)
{
	pid_t pid = fgpid(jobs);
	int jid = pid2jid(pid);
	//int jid = pid2jid(pid);
	if(pid == 0){
		//There is no foreground Process
		return;
	}
	else{
		//There is a foreground Process
		//printf("Job1 [%d] (%d) stopped by signal %d\n",jid, pid, sig);
		//Set the state to stopped
		//getjobpid(jobs,pid)->state=ST;
		//Stop it
		kill(-pid, SIGTSTP);
		return;
	}
	return;
}

/*********************
 * End signal handlers
 *********************/
