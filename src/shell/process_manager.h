// src/shell/process_manager.h
#ifndef PROCESS_MANAGER_H
#define PROCESS_MANAGER_H

#include <sys/types.h>
#include <time.h>

#define MAX_BG_JOBS 100
#define MAX_COMMAND_LEN 512

// Process states
typedef enum {
    PROC_RUNNING,
    PROC_STOPPED,
    PROC_DONE
} ProcessState;

// Information about a single process/job
typedef struct {
    pid_t pid;                      // Process ID
    pid_t pgid;                     // Process Group ID
    char command[MAX_COMMAND_LEN];  // Command string
    ProcessState state;             // Current state
    int job_id;                     // Job number (for background jobs)
    time_t start_time;              // When the job was started
} ProcessInfo;

// Manager for all processes in a tab
typedef struct {
    ProcessInfo *fg_process;         // Current foreground process (NULL if none)
    ProcessInfo bg_jobs[MAX_BG_JOBS]; // Array of background jobs
    int num_bg_jobs;                  // Number of background jobs
    int next_job_id;                  // Next job ID to assign
} ProcessManager;

/**
 * @brief Initialize a new process manager
 * @return Pointer to new ProcessManager, or NULL on failure
 */
ProcessManager* process_manager_init(void);

/**
 * @brief Clean up process manager and terminate all child processes
 * @param pm Process manager to clean up
 */
void process_manager_cleanup(ProcessManager *pm);

/**
 * @brief Set the current foreground process
 * @param pm Process manager
 * @param pid Process ID
 * @param pgid Process Group ID
 * @param command Command string
 */
void process_manager_set_foreground(ProcessManager *pm, pid_t pid, pid_t pgid, const char *command);

/**
 * @brief Clear the foreground process slot
 * @param pm Process manager
 */
void process_manager_clear_foreground(ProcessManager *pm);

/**
 * @brief Get the current foreground process
 * @param pm Process manager
 * @return Pointer to foreground ProcessInfo, or NULL if none
 */
ProcessInfo* process_manager_get_foreground(ProcessManager *pm);

/**
 * @brief Move foreground process to background (stopped state)
 * @param pm Process manager
 * @return Job ID assigned, or -1 on failure
 */
int process_manager_move_to_background(ProcessManager *pm);

/**
 * @brief Add a background job
 * @param pm Process manager
 * @param pid Process ID
 * @param pgid Process Group ID
 * @param command Command string
 * @param state Initial state
 * @return Job ID assigned, or -1 on failure
 */
int process_manager_add_background(ProcessManager *pm, pid_t pid, pid_t pgid, 
                                    const char *command, ProcessState state);

/**
 * @brief Remove a background job by PID
 * @param pm Process manager
 * @param pid Process ID to remove
 */
void process_manager_remove_background(ProcessManager *pm, pid_t pid);

/**
 * @brief Update the state of a background job
 * @param pm Process manager
 * @param pid Process ID
 * @param new_state New state
 */
void process_manager_update_state(ProcessManager *pm, pid_t pid, ProcessState state);

/**
 * @brief Find a background job by PID
 * @param pm Process manager
 * @param pid Process ID to find
 * @return Pointer to ProcessInfo, or NULL if not found
 */
ProcessInfo* process_manager_find_by_pid(ProcessManager *pm, pid_t pid);

/**
 * @brief Check for completed background jobs and print notifications
 * @param pm Process manager
 * @param output_callback Callback function to output notifications
 */
void process_manager_check_background_jobs(ProcessManager *pm, 
                                           void (*output_callback)(const char *));

/**
 * @brief Get a string representation of the process state
 * @param state Process state
 * @return String representation
 */
const char* process_state_to_string(ProcessState state);

#endif // PROCESS_MANAGER_H