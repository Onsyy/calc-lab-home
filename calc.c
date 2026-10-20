// Emils Ozols 251RDC076
// https://github.com/Onsyy/calc-lab-home
// Compile with: gcc -O2 -Wall -Wextra -std=c17 -o calc calc.c -lm
// Resources used: Claude helped me understand the task and Github codespace autofilled lines

#include <stdio.h>
#include <stdlib.h>


// AI (Claude): prompt "explain this lab so i can understand in what order things should be done" - step 1, program skeleton.
// 
// Name parts are kept separate because the two output names use different subsets: 
// result file <base>_<name>_<lastname>_<id>.txt, default folder <base>_<name>_<id>. 
// Names are lowercase plain ASCII like the task's example (anna_berzina_221234); 
// the ID is kept exactly as issued. Macros instead of static variables so that 
#define FIRST_NAME "emils"
#define LAST_NAME  "ozols"
#define STUDENT_ID "251RDC076"

static void usage(void) {
    fprintf(stderr,
            "usage: calc [-d DIR | --dir DIR] [-o OUTDIR | --output-dir OUTDIR] [input.txt]\n");
}

// The whole file is read into one buffer with a single fread
// "rb" so the bytes arrive exactly as stored and positions match the real file.
// The extra byte holds a '\0' sentinel and keeps malloc(0) away for empty files.
// Returns NULL on any failure; every path closes/frees what it opened.
static char *read_file(const char *path, size_t *out_len) {
    FILE *f = fopen(path, "rb");
    if (f == NULL) {
        return NULL;
    }
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return NULL;
    }
    long size = ftell(f);
    if (size < 0 || fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return NULL;
    }
    char *buf = malloc((size_t)size + 1);
    if (buf == NULL) {
        fclose(f);
        return NULL;
    }
    // A short read means the file could not be read completely (e.g. it is a
    // folder): treat it as a failure instead of evaluating half a file.
    if (fread(buf, 1, (size_t)size, f) != (size_t)size) {
        free(buf);
        fclose(f);
        return NULL;
    }
    fclose(f);
    buf[size] = '\0';
    *out_len = (size_t)size;
    return buf;
}

int main(int argc, char **argv) {
    // No arguments is a usage error per section 4: message on stderr, non-zero exit.
    if (argc < 2) {
        usage();
        return EXIT_FAILURE;
    }
    // TEMPORARY (step 2): read argv[1] and print its length so read_file can be
    // checked against `wc -c`. Replaced by the real command line later.
    size_t len;
    char *text = read_file(argv[1], &len);
    if (text == NULL) {
        fprintf(stderr, "calc: cannot read '%s'\n", argv[1]);
        return EXIT_FAILURE;
    }
    printf("%zu\n", len);
    free(text);
    return EXIT_SUCCESS;
}
