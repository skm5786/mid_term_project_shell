// in src/shell/redirect_handler.h

#ifndef REDIRECT_HANDLER_H
#define REDIRECT_HANDLER_H

#define MAX_REDIRECTS 4

// The project PDF only requires input redirection for Step 4
typedef enum {
    REDIRECT_NONE,
    REDIRECT_INPUT,     // <
    REDIRECT_OUTPUT     // >
} RedirectType;


typedef struct {
    RedirectType type;
    char *filename;
} Redirect;

typedef struct {
    Redirect redirects[MAX_REDIRECTS];
    int count;
    char* clean_command; // The command string with redirection parts removed
} RedirectInfo;

/**
 * @brief Initializes a RedirectInfo struct.
 */
void init_redirect_info(RedirectInfo *info);

/**
 * @brief Parses a command string for redirection operators.
 * It fills the RedirectInfo struct and sets the clean_command field.
 */
void parse_redirections(char *cmd_str, RedirectInfo *info);

/**
 * @brief Frees memory allocated by the redirection parser.
 */
void cleanup_redirect_info(RedirectInfo *info);

#endif // REDIRECT_HANDLER_H