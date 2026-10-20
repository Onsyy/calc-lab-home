// Emils Ozols 251RDC076
// https://github.com/Onsyy/calc-lab-home
// Compile with: gcc -O2 -Wall -Wextra -std=c17 -o calc calc.c -lm
// Resources used: Claude (Anthropic) - helped with the task and the code, Github codespace copilot? autofill was used aswell

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

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

// AI (Claude): prompt "help me understand the parser for + and - and explain it" - Value, Parser, evaluation, output file.
//
// A value is either an integer or a float. The type travels with the value so
// each operation can decide, from its two operands, which arithmetic to use.
typedef struct {
    int is_float;
    long long i;
    double f;
} Value;

// The parser owns the lexer and remembers the FIRST error only.
// Why only the first: the task wants ERROR:<pos> for the first error in the
// file, so once `failed` is set every parse function just returns, and no later
// position can overwrite err_pos.
typedef struct {
    Lexer lx;
    int failed;
    size_t err_pos;   // 0-based, like token positions
} Parser;

static void fail(Parser *p, size_t pos) {
    if (!p->failed) {
        p->failed = 1;
        p->err_pos = pos;
    }
}

static double as_double(Value v) {
    return v.is_float ? v.f : (double)v.i;
}

// Applies one binary operator. op_pos is the operator's position, needed for
// the "infinite or not a number" error, which the task reports at the operator.
// Integer vs float: if EITHER operand is a float the operation is done in
// double (task: "7 / 2.0 is 3.5"); only int op int stays a long long.
static Value apply(Parser *p, TokKind op, size_t op_pos, Value a, Value b) {
    Value r = { 0, 0, 0.0 };
    if (a.is_float || b.is_float) {
        double x = as_double(a);
        double y = as_double(b);
        r.is_float = 1;
        r.f = (op == T_PLUS) ? x + y : x - y;
        if (!isfinite(r.f)) {
            fail(p, op_pos);
        }
    } else {
        r.i = (op == T_PLUS) ? a.i + b.i : a.i - b.i;
    }
    return r;
}

// primary := NUMBER
// Every "missing operand" error is caught here: whatever token sits where a
// number must be (a second operator, an invalid character, the line's '\n',
// or the end of the file) is reported at its own start, which is exactly the
// position the task's table gives for each of those cases.
static Value parse_primary(Parser *p) {
    Value v = { 0, 0, 0.0 };
    Token t = p->lx.tok;
    if (t.kind == T_INT) {
        v.i = t.ival;
        next_token(&p->lx);
    } else if (t.kind == T_FLOAT) {
        v.is_float = 1;
        v.f = t.fval;
        next_token(&p->lx);
    } else {
        fail(p, t.pos);
    }
    return v;
}

// expr := primary { ('+' | '-') primary }
// The loop folds each new operand into `left` immediately, so 10 - 3 + 1 is
// (10 - 3) + 1: left-associative, as the task requires for + and -.
static Value parse_expr(Parser *p) {
    Value left = parse_primary(p);
    while (!p->failed && (p->lx.tok.kind == T_PLUS || p->lx.tok.kind == T_MINUS)) {
        TokKind op = p->lx.tok.kind;
        size_t op_pos = p->lx.tok.pos;
        next_token(&p->lx);
        Value right = parse_primary(p);
        if (p->failed) {
            break;
        }
        left = apply(p, op, op_pos, left, right);
    }
    return left;
}

// Output buffer that grows by doubling. Results are collected here instead of
// being written straight to the file because one error anywhere means the
// output must be ONLY ERROR:<pos>. Doubling keeps appends amortised O(1), so
// millions of results are never copied over and over.
typedef struct {
    char *data;
    size_t len;
    size_t cap;
    int oom;          // set if realloc ever failed
} OutBuf;

static void out_append(OutBuf *o, const char *s, size_t n) {
    if (o->oom) {
        return;
    }
    if (o->len + n > o->cap) {
        size_t cap = o->cap ? o->cap : 4096;
        while (cap < o->len + n) {
            cap *= 2;
        }
        char *grown = realloc(o->data, cap);
        if (grown == NULL) {
            o->oom = 1;
            return;
        }
        o->data = grown;
        o->cap = cap;
    }
    memcpy(o->data + o->len, s, n);
    o->len += n;
}

// Evaluates every line of the file. Returns 1 and sets *err_pos (1-based) on
// the first error, otherwise 0 with one result per expression in `out`.
// Blank and comment lines are recognised here, before tokenizing, because only
// the FIRST non-blank character decides it: '#' there makes a comment, while a
// '#' anywhere later reaches the tokenizer and becomes an invalid character.
static int evaluate(char *src, size_t len, OutBuf *out, size_t *err_pos) {
    Parser p = { { src, len, 0, { T_END, 0, 0, 0.0 } }, 0, 0 };
    Lexer *lx = &p.lx;
    char num[64];
    while (lx->i < len) {
        size_t j = lx->i;
        while (j < len && (src[j] == ' ' || src[j] == '\t')) {
            j++;
        }
        if (j == len) {
            break;                       // only spaces/tabs left: a blank last line
        }
        if (src[j] == '\n') {
            lx->i = j + 1;               // blank line: no output
            continue;
        }
        if (src[j] == '#') {             // comment line: skip through its '\n'
            const char *nl = memchr(src + j, '\n', len - j);
            lx->i = (nl != NULL) ? (size_t)(nl - src) + 1 : len;
            continue;
        }
        lx->i = j;
        next_token(lx);
        Value v = parse_expr(&p);
        // A complete expression must be followed by the end of the line.
        // Anything else (a number after a number, an invalid character, a ')'
        // with no '(') is reported at that token: it is where parsing stopped.
        if (!p.failed && lx->tok.kind != T_EOL && lx->tok.kind != T_END) {
            fail(&p, lx->tok.pos);
        }
        if (p.failed) {
            *err_pos = p.err_pos + 1;
            return 1;
        }
        int n = v.is_float ? snprintf(num, sizeof num, "%.15g\n", v.f)
                           : snprintf(num, sizeof num, "%lld\n", v.i);
        out_append(out, num, (size_t)n);
        // After T_EOL the lexer index is already at the start of the next line.
    }
    return 0;
}

// Builds "<outdir>/<base>_<name>_<lastname>_<id>.txt" where <base> is the input
// file name without its folder and without ".txt". The folder part is cut off
// because inputs come as paths like labs/task1.txt or ../in/a.txt.
static char *output_path(const char *outdir, const char *input) {
    const char *base = strrchr(input, '/');
    base = (base != NULL) ? base + 1 : input;
    size_t base_len = strlen(base);
    if (base_len >= 4 && strcmp(base + base_len - 4, ".txt") == 0) {
        base_len -= 4;
    }
    const char *suffix = "_" FIRST_NAME "_" LAST_NAME "_" STUDENT_ID ".txt";
    size_t size = strlen(outdir) + 1 + base_len + strlen(suffix) + 1;
    char *path = malloc(size);
    if (path != NULL) {
        snprintf(path, size, "%s/%.*s%s", outdir, (int)base_len, base, suffix);
    }
    return path;
}

// Creates the output folder if it does not exist; an existing folder is fine
// (the task says to write into it). Returns 0 on success.
static int ensure_dir(const char *dir) {
    struct stat st;
    if (stat(dir, &st) == 0) {
        return S_ISDIR(st.st_mode) ? 0 : -1;
    }
    return mkdir(dir, 0777);
}

// Evaluates one input file and writes its result file into outdir.
// Returns 0 on success. An ERROR:<pos> result is a success (exit code 0);
// only an unreadable input or an unwritable output is a failure.
static int process_file(const char *input, const char *outdir) {
    size_t len;
    char *text = read_file(input, &len);
    if (text == NULL) {
        fprintf(stderr, "calc: cannot read '%s'\n", input);
        return -1;
    }
    OutBuf out = { NULL, 0, 0, 0 };
    size_t err_pos = 0;
    if (evaluate(text, len, &out, &err_pos)) {
        char msg[64];
        int n = snprintf(msg, sizeof msg, "ERROR:%zu\n", err_pos);
        out.len = 0;                     // drop partial results
        out_append(&out, msg, (size_t)n);
    }
    free(text);
    if (out.oom) {
        fprintf(stderr, "calc: out of memory\n");
        free(out.data);
        return -1;
    }
    char *path = (ensure_dir(outdir) == 0) ? output_path(outdir, input) : NULL;
    FILE *f = (path != NULL) ? fopen(path, "wb") : NULL;
    int ok = f != NULL && fwrite(out.data, 1, out.len, f) == out.len;
    // fclose can be where a write error first shows up, so its result counts too.
    if (f != NULL && fclose(f) != 0) {
        ok = 0;
    }
    if (!ok) {
        fprintf(stderr, "calc: cannot write output for '%s' into '%s'\n", input, outdir);
    }
    free(path);
    free(out.data);
    return ok ? 0 : -1;
}

int main(int argc, char **argv) {
    const char *outdir = NULL;
    const char *input = NULL;
    for (int k = 1; k < argc; k++) {
        if (strcmp(argv[k], "-o") == 0 || strcmp(argv[k], "--output-dir") == 0) {
            if (k + 1 >= argc) {
                fprintf(stderr, "calc: %s needs a folder\n", argv[k]);
                usage();
                return EXIT_FAILURE;
            }
            outdir = argv[++k];
        } else if (argv[k][0] == '-') {
            fprintf(stderr, "calc: unknown option '%s'\n", argv[k]);
            usage();
            return EXIT_FAILURE;
        } else if (input == NULL) {
            input = argv[k];
        } else {
            fprintf(stderr, "calc: more than one input file\n");
            usage();
            return EXIT_FAILURE;
        }
    }
    // No input is a usage error per section 4: message on stderr, non-zero exit.
    if (input == NULL) {
        usage();
        return EXIT_FAILURE;
    }
    // TEMPORARY (step 4): -d and the default output folder come in a later step.
    if (outdir == NULL) {
        fprintf(stderr, "calc: -o OUTDIR is required for now\n");
        return EXIT_FAILURE;
    }
    return process_file(input, outdir) == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}