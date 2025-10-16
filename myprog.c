#include <stdio.h>

int main() {
    char line[256];
    
    printf("Program starting. Reading from stdin:\n");
    
    // Read one line from standard input
    if (fgets(line, sizeof(line), stdin) != NULL) {
        // Print the line that was read
        printf("Line read: %s", line);
    } else {
        printf("No input received.\n");
    }
    
    printf("Program finished.\n");
    return 0;
}