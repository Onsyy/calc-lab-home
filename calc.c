// Emils Ozols 251RDC076
// https://github.com/Onsyy/calc-lab-home
// Compile with: gcc -O2 -Wall -Wextra -std=c17 -o calc calc.c -lm
// Resources used: Claude (Anthropic) - helped with the task and the code, Github codespace copilot? autofill was used aswell

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>

// AI (Claude): prompt "explain this lab so i can understand in what order things should be done" - step 1, program skeleton.
//
// Name parts are kept separate because the two output names use different subsets:
// result file <base>_<name>_<lastname>_<id>.txt, default folder <base>_<name>_<id>.
// Names are lowercase plain ASCII like the task's example (anna_berzina_221234);
// the ID is kept exactly as issued. Macros instead of static variables so that
// unused ones (until the CLI is written) do not trigger -Wall warnings.
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

// AI (Claude): prompt "help me understand the tokenizer and explain it" - token types, Lexer, next_token().
//
// Token kinds. T_EOL is a '\n'; T_END is the end of the file. Both end an
// expression, but they are separate so the driver knows whether more lines follow.
typedef enum {
    T_INT, T_FLOAT,
    T_PLUS, T_MINUS, T_STAR, T_SLASH, T_POW,
    T_LPAREN, T_RPAREN,
    T_EOL, T_END,
    T_INVALID
} TokKind;

typedef struct {
    TokKind kind;
    size_t pos;       // 0-based index of the token's FIRST character in the file buffer
    long long ival;   // value if kind == T_INT
    double fval;      // value if kind == T_FLOAT
} Token;

// How positions are tracked: the lexer's only state is one index `i` into the
// buffer that holds the WHOLE file. A token's position is simply the index where
// it starts. Because the task counts positions across the whole file (every
// '\n' and every comment character included), no line/column bookkeeping is
// needed: the reported position is just pos + 1 (the task is 1-based).
// T_END gets pos == len, so it reports len + 1: exactly "file length + 1"
// that the task wants for an error at the end of a file without a final '\n'.
typedef struct {
    char *src;        // whole file, with a '\0' sentinel at src[len]
    size_t len;
    size_t i;         // next unread character
    Token tok;        // current token (one token of lookahead is all the parser needs)
} Lexer;

// is_digit takes the byte through unsigned char: passing a negative char
// (bytes >= 128 in a file) to isdigit() is undefined behaviour.
static int is_digit(char c) {
    return isdigit((unsigned char)c) != 0;
}

// Reads the next token into lx->tok.
static void next_token(Lexer *lx) {
    // Spaces and tabs may appear anywhere between tokens; nothing else is skipped.
    while (lx->i < lx->len && (lx->src[lx->i] == ' ' || lx->src[lx->i] == '\t')) {
        lx->i++;
    }
    Token *t = &lx->tok;
    t->pos = lx->i;
    if (lx->i >= lx->len) {
        t->kind = T_END;
        return;
    }
    char c = lx->src[lx->i];
    switch (c) {
    case '\n': t->kind = T_EOL;    lx->i++; return;
    case '+':  t->kind = T_PLUS;   lx->i++; return;
    case '-':  t->kind = T_MINUS;  lx->i++; return;
    case '/':  t->kind = T_SLASH;  lx->i++; return;
    case '(':  t->kind = T_LPAREN; lx->i++; return;
    case ')':  t->kind = T_RPAREN; lx->i++; return;
    case '*':
        // "**" is one token, so check the next character before deciding.
        // src[len] is the '\0' sentinel, so reading i + 1 is always in bounds.
        if (lx->src[lx->i + 1] == '*') {
            t->kind = T_POW;
            lx->i += 2;
        } else {
            t->kind = T_STAR;
            lx->i++;
        }
        return;
    default:
        break;
    }
    if (is_digit(c)) {
        size_t start = lx->i;
        while (is_digit(lx->src[lx->i])) {
            lx->i++;
        }
        // A float is digits '.' digits. The '.' is only taken when a digit
        // follows it, so "5." stops before the '.', and the next token is an
        // invalid '.' -> the error lands on the '.', as the task requires.
        // ".5" never gets here (it starts with '.'), and in "1.5e3" the number
        // ends at "1.5", so the 'e' becomes the invalid token.
        if (lx->src[lx->i] == '.' && is_digit(lx->src[lx->i + 1])) {
            lx->i++;
            while (is_digit(lx->src[lx->i])) {
                lx->i++;
            }
            // strtod alone would also accept exponents and hex ("1.5e3", "0x1p3"),
            // so the text is cut to exactly our digits.digits by putting a '\0'
            // right after it for the call, then restoring the character.
            // No copy of the number is made.
            char saved = lx->src[lx->i];
            lx->src[lx->i] = '\0';
            t->fval = strtod(lx->src + start, NULL);
            lx->src[lx->i] = saved;
            t->kind = T_FLOAT;
        } else {
            // strtoll starts at a digit, so it reads exactly the digits scanned
            // above (no sign, no spaces). Unlike a hand-written v*10+d loop it
            // clamps instead of overflowing, so even a huge number is not UB.
            t->ival = strtoll(lx->src + start, NULL, 10);
            t->kind = T_INT;
        }
        return;
    }
    // Anything else ('#', '.', letters, '\r', ...) is an invalid character.
    // It is reported at its own position by whoever reads this token.
    t->kind = T_INVALID;
    lx->i++;
}

int main(int argc, char **argv) {
    // No arguments is a usage error per section 4: message on stderr, non-zero exit.
    if (argc < 2) {
        usage();
        return EXIT_FAILURE;
    }
    size_t len;
    char *text = read_file(argv[1], &len);
    if (text == NULL) {
        fprintf(stderr, "calc: cannot read '%s'\n", argv[1]);
        return EXIT_FAILURE;
    }
    // TEMPORARY (step 3): print every token as "position kind [value]" so the
    // tokenizer can be checked by hand against the task's examples.
    static const char *names[] = {
        "INT", "FLOAT", "+", "-", "*", "/", "**", "(", ")", "EOL", "END", "INVALID"
    };
    Lexer lx = { text, len, 0, { T_END, 0, 0, 0.0 } };
    do {
        next_token(&lx);
        printf("%zu %s", lx.tok.pos + 1, names[lx.tok.kind]);
        if (lx.tok.kind == T_INT) {
            printf(" %lld", lx.tok.ival);
        } else if (lx.tok.kind == T_FLOAT) {
            printf(" %.15g", lx.tok.fval);
        }
        printf("\n");
    } while (lx.tok.kind != T_END);
    free(text);
    return EXIT_SUCCESS;
}