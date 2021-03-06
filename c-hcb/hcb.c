#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <memory.h>
#include <string.h>
#include <time.h>
#include <signal.h>
#include "sha256.h"

// TODO: Check that the nonce is numeric only

// Size of a sha256 hash in hexadecimal ascii characters
const int sha256_hex_length = SHA256_BLOCK_SIZE * 2;

// Lenght of the nonce
const int nonce_length = SHA256_BLOCK_SIZE * 2;

// Counter used to produce the nonce
long long unsigned int counter = 0;

// Output file handle
FILE* out_fp;

// Count the number of zero at left of a BYTE array (little-endian)
int numberOfZero(BYTE array[], int len) {
    int number = 0, i, j;
    for (i = 0; i < len; i++) {
        // Check in current byte
        // Little-endian, so decrement
        for (j = 7; j >= 0; j--) {
            if (((array[i]>>j)&1) == 0) {
                number++;
            } else {
                return number;
            }
        }
    }
    return number;
}

// Converts a byte array to an hexadecimal string
char* byteToHex(BYTE array[], int len) {
    char table[16] = "0123456789abcdef";
    char *result = malloc((2*len+1)*sizeof(char));
    int i;
    for (i = 0; i < len; i++) {
        result[i*2]   = table[(array[i]>>4)&0xF];
        result[i*2+1] = table[ array[i]    &0xF];
    }
    result[2*len] = '\0';
    return result;
}

// Add right padding to a string
char* padding_right(char* source, int length, char chr) {
    char* padding = malloc(length * sizeof(char));
    int i = 0;
    for (; i < length - strlen(source); i++) {
        padding[i] = chr;
    }
    padding[i] = '\0';
    char* dest = malloc((length + 1) * sizeof(char));
    sprintf(dest, "%s%s", source, padding);
    return dest;
}

// Print the program usage
void printUsage() {
    printf("Usage: hcb MESSAGE FILE | --continue FILE | --check FILE\n\n");
    printf("\tMESSAGE FILE\tStarts a new hash chain with the given MESSAGE and saves it to FILE\n");
    printf("\t--continue FILE\tContinue a hash chain contained in FILE\n");
    printf("\t\t\tThe new blocks will be automatically added to the FILE \n");
    printf("\t--check FILE\tCheck the validity of a hash chain contained in FILE\n\n");
}

// Function called on program exit
void exit_handler(int _) {
    fprintf(out_fp, "#nonce:%lld\n", counter);
    fflush(out_fp);
    exit(0);
}

// Generate a chain from a message and a difficulty
void generate_chain(char* text, char* path, int difficulty, bool continue_mode) {

    // Check the start message
    for (int i = 0; i < strlen(text); i++) {
        if (text[i] == '\r' || text[i] == '\n') {
            printf("Error: Start message must be over one single line\n\n");
            exit(13);
        }
    }

    // Open file
    out_fp = fopen(path, continue_mode ? "a" : "w");
    if (out_fp == NULL) {
        printf("Error: Unable to open file \"%s\"\n\n", path);
        exit(11);
    }

    // Register exit function
    signal(SIGINT, exit_handler);

    // Create a SHA256 context
    SHA256_CTX ctx;

    // The message that will be hashed
    // It must have the size of max(len of sha256 hash, len(text)) + len(nonce) + 2
    // The +2 is for the line break "\n" and the "\0"
    char* message = malloc(((strlen(text) > sha256_hex_length ? strlen(text) : sha256_hex_length) + nonce_length + 2) * sizeof(char));

    // The nonce is the padded string representation of counter
    char* nonce = malloc((nonce_length + 1) * sizeof(char));

    // The previous hash
    char* prev_hash = malloc(((strlen(text) > sha256_hex_length ? strlen(text) : sha256_hex_length) + 1) * sizeof(char));

    // By default the previous hash is the original text
    strcpy(prev_hash, text);

    // The hash
    BYTE hash[SHA256_BLOCK_SIZE];

    // Difficulty as a string
    char* difficulty_str = malloc((nonce_length + 1) * sizeof(char));

    // String used in sprintf to format the nonce
    char* nonce_format = malloc(10 * sizeof(char));
    sprintf(nonce_format, "%%0%dlld", nonce_length);

    // Print the start of the chain
    if (!continue_mode) {
        fprintf(out_fp, "# File generated by C-HCB\n%s\n%s\n", padding_right("HCB 1.0 ", nonce_length, '-'), text);
    }

    // Iter over difficulty
    while (1) {

        // Iter over counter
        do {

            // Prepare the message
            strcpy(message, prev_hash);
            strcat(message, "\n");
            sprintf(nonce, nonce_format, counter);
            strcat(message, nonce);

            // Compute its hash
            sha256_init(&ctx);
            sha256_update(&ctx, message, strlen(message));
            sha256_final(&ctx, hash);

            // Increment counter
            counter++;

        } while(numberOfZero(hash, SHA256_BLOCK_SIZE) != difficulty);

        // Saves the current hash
        strcpy(prev_hash, byteToHex(hash, SHA256_BLOCK_SIZE));

        // Print the previous hash, the found nonce and the block separators
        sprintf(difficulty_str, "%d", difficulty);
        strcat(difficulty_str, " ");
        fprintf(out_fp, "%s\n%s\n%s\n", nonce, padding_right(difficulty_str, nonce_length, '-'), prev_hash);
        fflush(out_fp);

        // Reset the counter and increment difficulty
        counter = 0;
        difficulty++;

    }

}

// Check a chain stored in a file
BYTE* check_chain(char* path, bool check_for_continue) {

    // Prepare reading variables
    FILE * fp;
    char * line = NULL;
    size_t len = 0;
    int current_line = 1; // The current line number in the chain (comments are ignored)
    int real_line = 0; // The current line number in the file (every line is counted)

    // Open file
    fp = fopen(path, "r");
    if (fp == NULL) {
        printf("Error: Unable to open file \"%s\"\n\n", path);
        exit(4);
    }
    
    // Variables used in the loop
    char* expected_first_line = padding_right("HCB 1.0 ", nonce_length, '-');
    char* message; // Initialized when we'll know the length of the first message
    char* new_hash = malloc((sha256_hex_length + 1) * sizeof(char));

    // Create a SHA256 context
    SHA256_CTX ctx;

    // The hash
    static BYTE hash[SHA256_BLOCK_SIZE];

    // Read file line by line and check the content
    char* last_hash = malloc((nonce_length + 1) * sizeof(char));
    char* last_nonce = malloc((nonce_length + 1) * sizeof(char));
    char* saved_content;
    char* nonce_prefix = "#nonce:";
    if (check_for_continue) {
        // Get file size
        fseek(fp, 0L, SEEK_END);
        int size = ftell(fp);
        rewind(fp);
        // Allocate a string that can handle the entire file
        saved_content = malloc((size + 1) * sizeof(char));
        saved_content[0] = '\0';
    }
    while (getline(&line, &len, fp) != -1) {

        real_line++;

        // Remove final line break
        int len = strlen(line);
        if (line[len-1] == '\n') {
            len--;
            line[len] = '\0';
        }
        if (line[len-1] == '\r') {
            len--;
            line[len] = '\0';
        }

        // Check if line is a comment
        if (len >= 1 && line[0] == '#') {
            // If continue mode, save the line
            if (check_for_continue) {
                strcat(saved_content, line);
                strcat(saved_content, "\n");
            }
            // Search for a nonce to resume to
            if (strncmp(nonce_prefix, line, strlen(nonce_prefix)) == 0) {
                char* string_nonce = &line[strlen(nonce_prefix)];
                char* end_ptr;
                counter = strtoull(string_nonce, &end_ptr, 10);
                if (string_nonce == end_ptr) {
                    printf("Warning: incorrect decimal nonce value \"%s\" in file \"%s\" on line #%d\n", string_nonce, path, real_line);
                    counter = 0;
                }
            }
            continue;
        }

        // Save line if in continue mode
        if (check_for_continue && len > 0) {
            strcat(saved_content, line);
            strcat(saved_content, "\n");
        }
        
        // Check if first line is correct
        if (current_line == 1 && strcmp(line, expected_first_line) != 0) {

            printf("Error while checking \"%s\": First line expected \"%s\" but got \"%s\" on line #%d\n\n", path, expected_first_line, line, real_line);
            exit(5);

        } else if (current_line == 2) {

            // Get the first message
            strcpy(last_hash, line);

            // Init the message variable used to compute the hashes
            message = malloc(((strlen(line) > sha256_hex_length ? strlen(line) : sha256_hex_length) + nonce_length + 2) * sizeof(char));

        } else if (current_line % 3 == 0) {

            // Get a nonce

            // Check the nonce lenght
            if (strlen(line) != nonce_length) {
                printf("Error while checking \"%s\": Nonce line #%d must be %d characters long\n\n", path, real_line, nonce_length);
                exit(15);
            }

            // Check if the nonce is only numeric
            for (int i = 0; i < strlen(line); i++) {
                if (line[i] < '0' || line[i] > '9') {
                    printf("Error while checking \"%s\": Nonce line #%d must be only numeric but got \"%s\"\n\n", path, real_line, line);
                    exit(14);
                }
            }

            // Save the nonce
            strcpy(last_nonce, line);

        } else if (current_line != 1 && current_line % 3 == 1) {

            // Block separator, check if integer is correct
            char* expected_line = malloc((nonce_length + 1) * sizeof(char));
            sprintf(expected_line, "%d ", (current_line - 1) / 3 - 1);
            expected_line = padding_right(expected_line, nonce_length, '-');

            if (strcmp(line, expected_line) != 0) {
                printf("Error while checking \"%s\": Separator line #%d expected \"%s\" but got \"%s\"\n\n", path, real_line, expected_line, line);
                exit(6);
            }

        } else if (current_line % 3 == 2) {

            // Line representing a hash

            // Check the hash lenght
            if (strlen(line) != sha256_hex_length) {
                printf("Error while checking \"%s\": Hash line #%d must be %d characters long\n\n", path, real_line, sha256_hex_length);
                exit(16);
            }

            // First we hash the previous hash and nonce and compare it to the read hash
            sprintf(message, "%s\n%s", last_hash, last_nonce);
            sha256_init(&ctx);
            sha256_update(&ctx, message, strlen(message));
            sha256_final(&ctx, hash);
            new_hash = byteToHex(hash, SHA256_BLOCK_SIZE);

            // Compare it
            if (strcmp(line, new_hash) != 0) {
                printf("Error while checking \"%s\": Hash line #%d expected \"%s\" but got \"%s\"\n\n", path, current_line, new_hash, line);
                exit(7);
            }

            // Check the difficulty
            int difficulty = numberOfZero(hash, SHA256_BLOCK_SIZE);
            int expected_difficulty = (current_line - 2) / 3 - 1;
            if (difficulty != expected_difficulty) {
                printf("Error while checking \"%s\": Hash line #%d (\"%s\") expected difficulty \"%d\" but got \"%d\"\n\n", path, real_line, line, expected_difficulty, difficulty);
                exit(8);
            }

            // Saves the hash
            strcpy(last_hash, new_hash);

        }

        current_line++;

    }

    // Last line must be a hash or a nonce
    if (current_line % 3 != 0) {
        printf("Error while checking \"%s\": File must end with a hash\n\n", path);
        exit(9);
    }

    // Free memory
    fclose(fp);
    if (line) {
        free(line);
    }

    // Print success or file content
    if (!check_for_continue) {
        printf("HCB file is valid.\n\n");
        fflush(stdout);
    } else {
        // Remove last line break
        int len = strlen(saved_content);
        if (saved_content[len-1] == '\n') {
            saved_content[len-1] = '\0';
        }
        // Open file for writing
        out_fp = fopen(path, "w");
        if (out_fp == NULL) {
            printf("Error: Unable to open file \"%s\"\n\n", path);
            exit(12);
        }
        // Saves
        fprintf(out_fp, "%s\n", saved_content);
        fflush(out_fp);
        // Close file
        fclose(out_fp);
    }

    // Return the last hash if we need to continue
    return hash;

}

// Main function
int main(int argc, char** argv) {

    // Check the parameters
    if (argc < 2) {

        printf("Error: missing parameter\n\n");
        printUsage();
        return 1;

    } else if (strcmp(argv[1], "--continue") == 0) {

        if (argc < 3) {
            printf("Error: Missing FILE after --continue\n\n");
            printUsage();
            return 2;
        }

        // Read the input chain and check it (we won't continue an invalid one)
        BYTE* hash = check_chain(argv[2], true);

        // Continue the chain
        generate_chain(byteToHex(hash, SHA256_BLOCK_SIZE), argv[2], numberOfZero(hash, SHA256_BLOCK_SIZE) + 1, true);

    } else if (strcmp(argv[1], "--check") == 0) {

        if (argc < 3) {
            printf("Error: Missing FILE after --check\n\n");
            printUsage();
            return 3;
        }

        // Read the input chain and check it
        check_chain(argv[2], false);

        // Exit success
        exit(0);

    } else {
        
        if (argc < 3) {
            printf("Error: Missing FILE after MESSAGE\n\n");
            printUsage();
            return 10;
        }

        // Generate a new chain with input string as start message
        generate_chain(argv[1], argv[2], 0, false);

    }

}
