#define _XOPEN_SOURCE 500 // Enable GNU extensions (required for nftw)
#include <ftw.h>
#include <glob.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define MAX_LINE_LENGTH 1024

#define CYAN "\033[36m"
#define ENDCOLOR "\033[0m"
#define RED "\033[31m"
#define GREEN "\033[32m"

char **paths = NULL;
int num_paths = 0;

/**
 * Highlights all occurrences of a pattern in a given line. Prints the rest of the line without highlighting.
 *
 * @param line The line to search for pattern.
 * @param pattern The pattern to highlight in the line.
 */
void highlight_match(const char *line, const char *pattern) {
    const char *match_start = line;
    const char *match_end = NULL;
    const char *ptr = line;
    const size_t pattern_length = strlen(pattern);

    while ((ptr = strstr(ptr, pattern)) != NULL) {
        match_start = ptr; // Record the start of the match
        match_end = match_start + pattern_length; // Calculate the end of the match

        // Print text before the match
        printf("%.*s", (int) (match_start - line), line);

        // Print the matched pattern in red
        printf(RED "%.*s" ENDCOLOR, (int) (match_end - match_start), match_start);

        // Move the pointer past the current match
        ptr += strlen(pattern);
        line = ptr; // Move the line pointer to the end of the current match
    }

    // Print remaining text after last match
    printf("%s\n", match_start + strlen(pattern));
}

/**
 * Searches for a given pattern in the specified file and highlights all occurrences of the pattern.
 * Prints the file name and the line number where the pattern occurs.
 *
 * @param pattern The pattern to search for in the file.
 * @param filename The name of the file to search within, with its full path.
 * @param matches_before If set to 1, print the file name before matching lines. Otherwise, do not print the name of the file.
 * @return The number of matches found in the file.
 */
int look_for(char *pattern, char *filename, const int matches_before) {
    if (strstr(filename, ".git") != NULL) {
        // Ignore git
        return 0;
    }

    FILE *fptr = fopen(filename, "r");
    if (fptr == NULL) {
        fprintf(stderr, "shitty-grep: Error opening file");
        return 0;
    }
    fseek(fptr, 0, SEEK_END);
    const long fsize = ftell(fptr);
    rewind(fptr);

    // Check if file size is greater than or equal to 4. If it is, check if it is a binary. If it is, skip the file, returning 0 matches.
    if (fsize >= 4) {
        char binary_check[4];
        fread(binary_check, 1, 4, fptr);

        if (*((unsigned int *) binary_check) == 0x464c457f) {
            // Ignore binaries
            fclose(fptr);
            return 0;
        }
    }
    rewind(fptr);

    char line[MAX_LINE_LENGTH];

    int filename_shown = 0;
    int line_number = 1;
    int matches_in_file = 0;

    pattern[strcspn(pattern, "\n")] = '\0';

    while (fgets(line, MAX_LINE_LENGTH, fptr)) {
        line[strcspn(line, "\n")] = '\0';

        if (strstr(line, pattern) != NULL) {
            if (filename_shown == 0) {
                if (matches_before) {
                    printf(GREEN "\n%s\n" ENDCOLOR, filename);
                } else {
                    printf(GREEN "%s\n" ENDCOLOR, filename);
                }
                filename_shown = 1;
            }
            printf(CYAN "%d" ENDCOLOR ":", line_number);
            highlight_match(line, pattern);
            matches_in_file++;
        }

        line_number++;
    }

    fclose(fptr);
    return matches_in_file;
}

int find_paths_callback(const char *fpath, const struct stat *sb, const int typeflag, struct FTW *ftwbuf) {
    if (typeflag == FTW_F) {
        // Regular file
        // Allocate memory for the path
        char *new_path = calloc(1, strlen(fpath) + 1);
        if (new_path == NULL) {
            perror("Error allocating memory");
            return -1;
        }
        strcpy(new_path, fpath);

        // Reallocate memory for the array of pointers
        char **tmp = realloc(paths, (num_paths + 1) * sizeof(char *));
        if (tmp == NULL) {
            perror("Error reallocating memory");
            free(new_path);
            return -1;
        }
        paths = tmp;

        // Store the pointer to the path in the array
        paths[num_paths] = new_path;
        num_paths++;
    }
    return 0; // Continue traversal
}

int find_paths_recursive(const char *pattern) {
    return nftw(pattern, find_paths_callback, 20, FTW_PHYS);
}

int processs_piped_data(FILE *stream, const char *pattern) {
    char line[MAX_LINE_LENGTH];
    int line_number = 1;
    int matches = 0;

    while (fgets(line, MAX_LINE_LENGTH, stream)) {
        line[strcspn(line, "\n")] = '\0';
        if (strstr(line, pattern) != NULL) {
            printf(CYAN "%d" ENDCOLOR ":", line_number);
            highlight_match(line, pattern);
            matches++;
        }
        line_number++;
    }
    return matches;
}

/**
 * Checks if the tool is receiving input from a pipe, and processes the data from the pipe.
 * If argv > 2 this function will be ignored and cli will be used instead.
 *
 * @param arg_count The number of command line arguments passed.
 * @param arg_values An array of command line argument values.
 * @return Returns 1 if the pattern is not provided or no matches are found in the piped data,
 *          otherwise returns 0.
 */
int piped(const int arg_count, char *arg_values[]) {
    if (arg_count == 1) {
        fprintf(stderr, "shitty-grep: No pattern provided\n");
        return 1;
    }
    const char *pattern = arg_values[1];
    const int matches = processs_piped_data(stdin, pattern);
    return matches == 0 ? 1 : 0;
}

/**
 * Command Line Interface (CLI) function for executing search operations using shitty-grep.
 * If a single argument is provided, the function searches for the given pattern recursively in the current directory.
 * If two arguments are provided, the function searches for the given pattern recursively in the specified directory.
 *
 * @param arg_count The number of command line arguments.
 * @param arg_values An array of command line arguments.
 *                   If arg_count == 2, arg_values[0] is the pattern and arg_values[1] is the path.
 *                   If arg_count == 1, arg_values[0] is the pattern and the current directory is used as the path.
 *
 * @return Returns 0 if at least one match is found, otherwise returns 1.
 */
int cli(const int arg_count, char *arg_values[]) {
    int result;
    if (arg_count == 2) {
        result = find_paths_recursive(".");
    } else {
        result = find_paths_recursive(arg_values[2]);
    }
    if (paths == NULL) {
        fprintf(stderr, "shitty-grep: %s: No such file or directory\n", arg_values[2]);
        return 1;
    }
    if (result != 0) {
        return 1;
    }
    int first_file = 0;
    for (int i = 0; i < num_paths; i++) {
        const int matches = look_for(arg_values[1], paths[i], first_file);
        if (matches > 0) {
            first_file = 1;
        }
    }
    for (int i = 0; i < num_paths; i++) {
        free(paths[i]);
    }
    free(paths);
    return first_file == 0 ? 1 : 0;
}

int main(const int argc, char *argv[]) {
    if (argc < 2 && isatty(STDIN_FILENO)) {
        fprintf(stderr, "Usage: shitty-grep PATTERN [PATH]\n");
        return -1;
    }

    // If stdin is connected to a terminal or more than one argument is provided, cli will be used,
    // ignoring any piped data. Returns 1 if no matches and 0 on success.
    return isatty(STDIN_FILENO) || argc > 2 ? cli(argc, argv) : piped(argc, argv);
}
