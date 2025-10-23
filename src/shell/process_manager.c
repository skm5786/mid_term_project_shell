// src/shell/process_manager.c
#include "process_manager.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

ProcessManager* process_manager_init(void) {
    ProcessManager *pm = calloc(1, sizeof(ProcessManager));
    if (!pm) {
        perror("calloc ProcessManager");
        return NULL;
    }
    
    pm->fg_process = NULL;
    pm->num_bg_jobs = 0;
    pm->next_job_id = 1;
    
    return pm;
}

void process_manager_cleanup(ProcessManager *pm) {
    if (!pm) return;
    
    // Terminate and wait for all background jobs
    for (int i = 0; i < pm->num_bg_jobs; i++) {
        if (pm->bg_jobs[i].state == PROC_RUNNING || 
            pm->bg_jobs[i].state == PROC_STOPPED) {
            kill(-pm->bg_jobs[i].pgid, SIGTERM);
            waitpid(pm->bg_jobs[i].pid, NULL, 0);
        }
    }
    
    // Clean up foreground process if any
    if (pm->fg_process) {
        free(pm->fg_process);
    }
    
    free(pm);
}

void process_manager_set_foreground(ProcessManager *pm, pid_t pid, pid_t pgid, 
                                    const char *command) {
    if (!pm) return;
    
    if (pm->fg_process) {
        free(pm->fg_process);
    }
    
    pm->fg_process = malloc(sizeof(ProcessInfo));
    if (!pm->fg_process) {
        perror("malloc ProcessInfo");
        return;
    }
    
    pm->fg_process->pid = pid;
    pm->fg_process->pgid = pgid;
    pm->fg_process->state = PROC_RUNNING;
    pm->fg_process->job_id = 0; // Foreground jobs don't have job IDs
    pm->fg_process->start_time = time(NULL);
    
    strncpy(pm->fg_process->command, command, MAX_COMMAND_LEN - 1);
    pm->fg_process->command[MAX_COMMAND_LEN - 1] = '\0';
}

void process_manager_clear_foreground(ProcessManager *pm) {
    if (!pm) return;
    
    if (pm->fg_process) {
        free(pm->fg_process);
        pm->fg_process = NULL;
    }
}

ProcessInfo* process_manager_get_foreground(ProcessManager *pm) {
    return pm ? pm->fg_process : NULL;
}

int process_manager_move_to_background(ProcessManager *pm) {
    if (!pm || !pm->fg_process) return -1;
    
    if (pm->num_bg_jobs >= MAX_BG_JOBS) {
        fprintf(stderr, "Maximum number of background jobs reached\n");
        return -1;
    }
    
    // Copy foreground process info to background jobs array
    memcpy(&pm->bg_jobs[pm->num_bg_jobs], pm->fg_process, sizeof(ProcessInfo));
    
    // Assign job ID and set state to stopped
    pm->bg_jobs[pm->num_bg_jobs].job_id = pm->next_job_id++;
    pm->bg_jobs[pm->num_bg_jobs].state = PROC_STOPPED;
    
    int job_id = pm->bg_jobs[pm->num_bg_jobs].job_id;
    pm->num_bg_jobs++;
    
    // Clear foreground slot
    free(pm->fg_process);
    pm->fg_process = NULL;
    
    return job_id;
}

int process_manager_add_background(ProcessManager *pm, pid_t pid, pid_t pgid,
                                    const char *command, ProcessState state) {
    if (!pm) return -1;
    
    if (pm->num_bg_jobs >= MAX_BG_JOBS) {
        fprintf(stderr, "Maximum number of background jobs reached\n");
        return -1;
    }
    
    ProcessInfo *job = &pm->bg_jobs[pm->num_bg_jobs];
    job->pid = pid;
    job->pgid = pgid;
    job->state = state;
    job->job_id = pm->next_job_id++;
    job->start_time = time(NULL);
    
    strncpy(job->command, command, MAX_COMMAND_LEN - 1);
    job->command[MAX_COMMAND_LEN - 1] = '\0';
    
    pm->num_bg_jobs++;
    
    return job->job_id;
}

void process_manager_remove_background(ProcessManager *pm, pid_t pid) {
    if (!pm) return;
    
    for (int i = 0; i < pm->num_bg_jobs; i++) {
        if (pm->bg_jobs[i].pid == pid) {
            // Shift remaining jobs down
            memmove(&pm->bg_jobs[i], &pm->bg_jobs[i + 1], 
                    (pm->num_bg_jobs - i - 1) * sizeof(ProcessInfo));
            pm->num_bg_jobs--;
            return;
        }
    }
}

void process_manager_update_state(ProcessManager *pm, pid_t pid, ProcessState state) {
    if (!pm) return;
    
    for (int i = 0; i < pm->num_bg_jobs; i++) {
        if (pm->bg_jobs[i].pid == pid) {
            pm->bg_jobs[i].state = state;
            return;
        }
    }
}

ProcessInfo* process_manager_find_by_pid(ProcessManager *pm, pid_t pid) {
    if (!pm) return NULL;
    
    for (int i = 0; i < pm->num_bg_jobs; i++) {
        if (pm->bg_jobs[i].pid == pid) {
            return &pm->bg_jobs[i];
        }
    }
    
    return NULL;
}

void process_manager_check_background_jobs(ProcessManager *pm, 
                                           void (*output_callback)(const char *)) {
    if (!pm || !output_callback) return;
    
    int status;
    pid_t pid;
    
    // Check all background jobs non-blockingly
    while ((pid = waitpid(-1, &status, WNOHANG | WUNTRACED | WCONTINUED)) > 0) {
        ProcessInfo *job = process_manager_find_by_pid(pm, pid);
        if (!job) continue; // Not a tracked background job
        
        char notification[1024];
        
        if (WIFEXITED(status)) {
            // Process exited normally
            snprintf(notification, sizeof(notification),
                     "[%d]+ Done                    %s\n",
                     job->job_id, job->command);
            output_callback(notification);
            process_manager_remove_background(pm, pid);
        } else if (WIFSIGNALED(status)) {
            // Process terminated by signal
            snprintf(notification, sizeof(notification),
                     "[%d]+ Terminated              %s\n",
                     job->job_id, job->command);
            output_callback(notification);
            process_manager_remove_background(pm, pid);
        } else if (WIFSTOPPED(status)) {
            // Process was stopped
            process_manager_update_state(pm, pid, PROC_STOPPED);
            snprintf(notification, sizeof(notification),
                     "[%d]+ Stopped                 %s\n",
                     job->job_id, job->command);
            output_callback(notification);
        } else if (WIFCONTINUED(status)) {
            // Process was resumed
            process_manager_update_state(pm, pid, PROC_RUNNING);
            snprintf(notification, sizeof(notification),
                     "[%d]+ Running                 %s\n",
                     job->job_id, job->command);
            output_callback(notification);
        }
    }
}

const char* process_state_to_string(ProcessState state) {
    switch (state) {
        case PROC_RUNNING: return "Running";
        case PROC_STOPPED: return "Stopped";
        case PROC_DONE: return "Done";
        default: return "Unknown";
    }
}