#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include <limits.h>

/*
    DATA STRUCTURES AND GLOBAL VARIABLES
*/

// Process status enum
typedef enum process_status {
	RUNNING = 0,
    READY = 1,
    STOPPED = 2,
	TERMINATED = 3,
    UNUSED = 4
} process_status;

// Process record struct
typedef struct process_record {
	pid_t pid;
	process_status status;
    int remaining_runtime;
} process_record;

// Maximum number of processes
enum {
	MAX_PROCESSES = 64
};

// Process records array
process_record process_records[MAX_PROCESSES];
// Index of the running process for easy access
int running_process_index = -1;

// Time variables for tracking elapsed time for running process
time_t start_time;
time_t elapsed_time; // To calculate how much time has passed since the last update 

/*
    UTILITY FUNCTIONS
*/

// Initialize process records
void initialise_process_records(void) {
    for (int i = 0; i < MAX_PROCESSES; i++) {
        process_records[i].status = UNUSED;
    }
}

// Get the index of the first unused process record
int get_unused_process_index(void) {
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (process_records[i].status == UNUSED) {
            return i;
        }
    }
    return -1;
}

// Get the index of the first slot with TERMINATED process (For Replacement Policy)
int get_terminated_process_index(void) {
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (process_records[i].status == TERMINATED) {
            return i;
        }
    }
    return -1;
}

// Find the index of the process record with minimum remaining runtime
int find_min_runtime_process(void) {
    int min_index = -1;
    int min_runtime = INT_MAX;
    for (int i = 0; i < MAX_PROCESSES; ++i) {
        process_record * const p = &process_records[i];
        if (p->status == READY) {
            if (p->remaining_runtime < min_runtime) {
                min_runtime = p->remaining_runtime;
                min_index = i;
            }
        }
    }
    return min_index;
}

// Similar to the shell.c get_input function but slightly modified
char * get_input(char * buffer, char * args[], int args_count_max) {
	for (char* c = buffer; *c != '\0'; ++c) {
		if ((*c == '\r') || (*c == '\n')) {
			*c = '\0';
			break;
		}
	}
	strcat(buffer, " ");
	// Tokenize command's arguments
	char * p = strtok(buffer, " ");
	int arg_cnt = 0;
	while (p != NULL) {
		args[arg_cnt++] = p;
		if (arg_cnt == args_count_max - 1) {
			break;
		}
		p = strtok(NULL, " ");
	}
	args[arg_cnt] = NULL;
	return args[0];
}

/*
    SIGNAL HANDLERS: SIGCHLD (To handle child process termination automatically)
*/

// Signal handler for SIGCHLD (Child process terminated)
void sigchld_handler(int signum) {
    (void) signum;
    pid_t pid;
    int status;
    // Reap all terminated child processes
    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
        for (int i = 0; i < MAX_PROCESSES; ++i) {
            // Find the process record with the given pid
            process_record * const p = &process_records[i];
            if (p->pid == pid) {
                // Update the status of the process record
                p->status = TERMINATED;
                // If the process was running, update the running process index
                if (i == running_process_index) {
                    time_t current_time = time(NULL);
                    elapsed_time = difftime(current_time, start_time);
                    if (elapsed_time > 0) {
                        p->remaining_runtime -= elapsed_time;
                        start_time = current_time;
                    }
                    // Set the running process index to -1 to trigger the scheduler
                    running_process_index = -1;
                }
                break;
            }
        }
    }
}

// Set up the signal handlers for SIGCHLD (Automatically handle child process termination)
void setup_signal_handlers(void) {
    struct sigaction sa;
    sa.sa_handler = sigchld_handler;
    // Block all signals while the handler is running
    sigemptyset(&sa.sa_mask);
    // Restart system calls if interrupted by handler
    sa.sa_flags = SA_RESTART | SA_NOCLDSTOP;
    // Register the handler for SIGCHLD
    if (sigaction(SIGCHLD, &sa, NULL) == -1) {
        perror("Sigaction failed in setup_signal_handlers\n");
        exit(EXIT_FAILURE);
    }
}

/*
    SCHEDULER (Placed here to avoid implicit declaration)
*/

// Scheduler function to start the process with the minimum remaining runtime (SJF)
void scheduler (void) {
    // If there is a running process, stop it and update its remaining runtime
    // Reason to stop the process: To ensure that the process does not consume CPU time while the scheduler is running
    if (running_process_index >= 0) {
        process_record * const p = &process_records[running_process_index];
        int kill_check = kill(p->pid, SIGSTOP);
        // If the kill fails, print an error message
        if (kill_check == -1) {
            perror("First Kill failed in scheduler()\n");
            return;
        }
        time_t current_time = time(NULL);
        elapsed_time = difftime(current_time, start_time);
        if (elapsed_time > 0) {
            p->remaining_runtime -= elapsed_time;
            start_time = current_time;
        }
        p->status = READY;
        // Set the running process index to -1 to indicate that there is no running process now
        running_process_index = -1;
    }

    // Find the process with the minimum remaining runtime and start it (Scheduler Policy: SJF (Shortest Job First))
    int min_index = find_min_runtime_process();
    // If there is no process to start (No READY processes), return
    if (min_index < 0) {
        return;
    }
    // Start the process with the minimum remaining runtime
    process_record * const p = &process_records[min_index];
    p->status = RUNNING;
    running_process_index = min_index;
    int kill_check = kill(p->pid, SIGCONT);
    // If the kill fails, print an error message
    if (kill_check == -1) {
        perror("Second Kill failed in scheduler()\n");
        return;
    }
    // Update the start time for the new running process
    start_time = time(NULL);
}

/*
    CORE FUNCTIONS: RUN, LIST, STOP, RESUME, TERMINATE and EXIT
*/

void perform_run(char* args[]) {
    // Ensure that the arguments are valid
    if (args == NULL) {
        fprintf(stderr, "Invalid arguments for perform_run()\n");
        return;
    }
    // Ensure there are enough arguments
    if (args[1] == NULL || args[2] == NULL || args[3] == NULL) {
        fprintf(stderr, "Invalid arguments for perform_run()\n");
        return;
    }
    // Ensure that the remaining runtime is valid
    if (atoi(args[3]) <= 0) {
        fprintf(stderr, "Invalid remaining runtime for perform_run(), provide a number > 0\n");
        return;
    }
    // Ensure there is space for the new process record:
    int index = get_unused_process_index();
    if (index < 0) {
        // If there is no unused process record, find the first TERMINATED process record to replace
        index = get_terminated_process_index();
    }
    if (index < 0) {
        // If there is no TERMINATED process record, print an error message and return
        fprintf(stderr, "Maximum number of processes reached\n");
        return;
    }

    // Create a new process to run and store its information in the process records array
	pid_t pid = fork();
	if (pid < 0) {
		fprintf(stderr, "Fork failed in perform_run()\n");
		return;
	}
	if (pid == 0) {
        // Child process: Execute the command within the new process
        // Since './' will be present in the input, we can directly execute the command
		execvp(args[1], args + 1);
        // If the exec fails, print an error message and exit the child process
        perror("Execution failed in perform_run()\n");
		exit(EXIT_FAILURE);
	}
    // Parent process: Store the information of the new process in the process records array
	process_record * const p = &process_records[index];
	p->pid = pid;
    // Set the status to READY because the scheduler will decide which process to start
	p->status = READY;
    p->remaining_runtime = atoi(args[3]);
    // Start the process if there is no running process
    if (running_process_index < 0) {
        start_time = time(NULL);
        p->status = RUNNING;
        running_process_index = index;
    } else {
        // If there is a running process already, stop this process and store it as READY
        int kill_check = kill(p->pid, SIGSTOP);
        if (kill_check == -1) {
            perror("Kill failed in perform_run()\n");
            return;
        }
    }
    // We call the scheduler to start the process with the minimum remaining runtime (SJF)
    scheduler();
}

void perform_list(void) {
    // To keep track of whether there are any non-UNUSED processes
    bool found = false;
    for (int i = 0; i < MAX_PROCESSES; ++i) {
        process_record * const p = &process_records[i];
        if (p->status != UNUSED) {
            found = true;
            printf("%d, %d\n", p->pid, p->status);
        }
    }
    if (!found) {
        printf("No processes to list.\n");
    }
}

void perform_stop(pid_t pid) {
    // Ensure that the process ID given is valid
    if (pid <= 0) {
        printf("The process ID must be a positive integer.\n");
        return;
    }
    // Find the process record with the given PID and stop it if it is RUNNING
    for (int i = 0; i < MAX_PROCESSES; ++i) {
        process_record * const p = &process_records[i];
        if (p->pid == pid) {
            if (p->status == RUNNING || p->status == READY) {
                int kill_check = kill(p->pid, SIGSTOP);
                // If the kill fails, print an error message
                if (kill_check == -1) {
                    perror("Kill failed in perform_stop()\n");
                    return;
                }
                p->status = STOPPED;
                // If the process was running, update information about the running process
                if (i == running_process_index) {
                    // Update the remaining runtime of the process to ensure (Scheduler Policy: SJF)
                    time_t current_time = time(NULL);
                    elapsed_time = difftime(current_time, start_time);
                    if (elapsed_time > 0) {
                        p->remaining_runtime -= elapsed_time;
                        start_time = current_time;
                    }
                    // Set the running process index to -1 to trigger the scheduler
                    running_process_index = -1;
                    scheduler();
                }
                return;
            } else {
                // If the process is not running, print an error message
                printf("Process %d is not running.\n", pid);
                return;
            }
        }
    }
    // At this point, the process with the given PID was not found
    printf("Process %d not found.\n", pid);
}

void perform_resume(pid_t pid) {
    // Ensure that the process ID given is valid
    if (pid <= 0) {
        printf("The process ID must be a positive integer.\n");
        return;
    }
    // Find the process record with the given PID and resume it if it is STOPPED
    for (int i = 0; i < MAX_PROCESSES; ++i) {
        process_record * const p = &process_records[i];
        if (p->pid == pid) {
            if (p->status == STOPPED) {
                // We won't directly resume the process here because the scheduler will decide which process to start
                p->status = READY;
                scheduler();
                return;
            } else {
                // If the process wasn't not stopped, print an error message
                printf("Process %d was not in STOPPED status, in order to resume it.\n", pid);
                return;
            }
        }
    }
    // At this point, the process with the given PID was not found
    printf("Process %d not found.\n", pid);
}

void perform_kill(pid_t pid) {
    // Ensure that the process ID given is valid
    if (pid <= 0) {
        printf("The process ID must be a positive integer.\n");
        return;
    }
    // Find the process record with the given PID and terminate it if it is not TERMINATED
    for (int i = 0; i < MAX_PROCESSES; ++i) {
        process_record * const p = &process_records[i];
        if (p->pid == pid) {
            if (p->status != TERMINATED) {
                int kill_check = kill(p->pid, SIGTERM);
                // If the kill fails, print an error message
                if (kill_check == -1) {
                    perror("Kill failed in perform_kill()\n");
                    return;
                }
                p->status = TERMINATED;
                // If the process was running, update the running process index just for accuracy purposes
                time_t current_time = time(NULL);
                elapsed_time = difftime(current_time, start_time);
                if (elapsed_time > 0) {
                    p->remaining_runtime -= elapsed_time;
                    start_time = current_time;
                }
                if (i == running_process_index) {
                    // If the process was running, set the running process index to -1 to trigger the scheduler
                    running_process_index = -1;
                    scheduler();
                }
                return;
            } else {
                printf("Process %d is already terminated.\n", pid);
                return;
            }
        }
    }
    // At this point, the process with the given PID was not found
    printf("Process %d not found.\n", pid);
}

void perform_exit(void) {
    // Loop through process records and terminate all processes
    for (int i = 0; i < MAX_PROCESSES; ++i) {
        process_record * const p = &process_records[i];
        if (p->status != UNUSED && p->status != TERMINATED) {
            int kill_check = kill(p->pid, SIGTERM);
            // If the kill fails, print an error message
            if (kill_check == -1) {
                perror("Kill failed in perform_exit()... continuing exit function\n");
            }
            p->status = TERMINATED;
        }
    }
    // Print a message and exit the program
	printf("Exiting the process manager!\n");
}

/*
    MAIN FUNCTION (Entry point of the program)
*/

int main(void) {
    // First, initialize the process records to UNUSED status
    initialise_process_records();

    // Create a pipe to communicate between the user interface and the process manager
    int pipefd[2];
    if (pipe(pipefd) == -1) {
        perror("Pipe failed in main\n");
        exit(EXIT_FAILURE);
    }

    // Create a fork to run the user interface and the process manager
    pid_t pid = fork();
    if (pid == -1) {
        perror("Fork failed in main\n");
        exit(EXIT_FAILURE);
    }
    // In the child process, we will run the user interface
    if (pid == 0) {
        // This process will only write to the pipe
        close(pipefd[0]);
        char buffer[80];
        while (true) {
            // Display the prompt and get the input from the terminal
            printf("\x1B[34m""cs205""\x1B[0m""$ ");
            if (fgets(buffer, sizeof(buffer), stdin) == NULL) {
                // If the input is NULL, break the loop
                break;
            }
            // Write the input to the pipe with a newline character at the end
            buffer[strcspn(buffer, "\n")] = '\0';
            write(pipefd[1], buffer, sizeof(buffer));
            // If the input is "exit", break the loop
            if (strcmp(buffer, "exit") == 0) {
                break;
            }
            // Sleep here to avoid the process manager from reading the input before the user interface writes it
            sleep(1);
        }
        close(pipefd[1]);
    } 
    else {
        // This process manager will only read from the pipe and perform the required operations
        int flags = fcntl(pipefd[0], F_GETFL, 0);
        // I set the pipe to non-blocking mode to avoid the process manager from blocking while reading
        fcntl(pipefd[0], F_SETFL, flags | O_NONBLOCK);
        close(pipefd[1]);
        // Set up the signal handlers for SIGCHLD (Automatically handle child process termination)
        setup_signal_handlers();
        // Flag to check if the user wants to exit the program
        bool exit = false;

        // Loop to read the input from the pipe and perform the required operations
        while (true) {
            // If the exit flag is true, break the loop
            if (exit) {
                break;
            }
            // Reading from pipe
            char buffer[80];
            int bytes_read = read(pipefd[0], buffer, sizeof(buffer));
            // If there are bytes to read, process the input
            if (bytes_read > 0) {
                buffer[bytes_read] = '\0';
                char * args[10];
                int args_count_max = sizeof(args) / sizeof(args[0]);
                // Get the command and arguments from the input
                char * command = get_input(buffer, args, args_count_max);
                if (strcmp(command, "run") == 0) {
                    perform_run(args);
                } else if (strcmp(command, "stop") == 0) {
                    perform_stop(atoi(args[1]));
                } else if (strcmp(command, "resume") == 0) {
                    perform_resume(atoi(args[1]));
                } else if (strcmp(command, "kill") == 0) {
                    perform_kill(atoi(args[1]));
                } else if (strcmp(command, "list") == 0) {
                    perform_list();
                } else if (strcmp(command, "exit") == 0) {
                    // Set the exit flag to true and call the perform_exit function
                    exit = true;
                    perform_exit();
                    break;
                } else {
                    printf("Unknown command: %s\n", command);
                }
            }
            // If there is a running process, update its runtime periodically
            if (running_process_index >= 0) {
                process_record * const p = &process_records[running_process_index];
                time_t current_time = time(NULL);
                elapsed_time = difftime(current_time, start_time);
                if (elapsed_time > 0) {
                    p->remaining_runtime -= elapsed_time;
                    start_time = current_time;
                }
            } else {
                // If there is no running process, call the scheduler (Trigger: running_process_index = -1)
                scheduler();
            }
            // Sleep for 100ms to avoid busy waiting and to give the user interface time to write to the pipe
            usleep(100000);
        }
        close(pipefd[0]);
    }
	return EXIT_SUCCESS;
}
