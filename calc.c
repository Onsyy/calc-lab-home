// Emils Ozols 251RDC076
// https://github.com/Onsyy/calc-lab-home
// Compile with: gcc -O2 -Wall -Wextra -std=c17 -o calc calc.c -lm
// Resources used: Claude (Anthropic) - helped with the task and the code, Github codespace copilot? autofill was used aswell

#include <ctype.h>
#include <dirent.h>
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
// the ID is kept exactly as issued. Macros, so they can be glued straight into
// string literals like "_" FIRST_NAME "_" STUDENT_ID without building strings at runtime.
#define FIRST_NAME "emils"
#define LAST_NAME  "ozols"
#define STUDENT_ID "251RDC076"

static void usage(void) {
    fprintf(stderr,
            "usage: calc [-d DIR | --dir DIR] [-o OUTDIR | --output-dir OUTDIR] [input.txt]\n");
}

// The whole file is read into one buffer with a single fread.
// Large files: the size is known up front (fseek/ftell), so the buffer is
// allocated once and the data is never copied or re-grown; the parser then
// walks it with one index, so a multi-megabyte file costs one read and one pass.
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
// AI (Claude): prompt "help me understand * and / with precedence and explain it" - apply(), parse_term().
// AI (Claude): prompt "help me understand parentheses and explain it" - ( ) in parse_primary().
// AI (Claude): prompt "help me understand ** and explain it" - int_pow(), apply_pow(), parse_power().
// AI (Claude): prompt "help me understand the command line part and explain it" - base_name(), default_outdir(), process_dir(), main().
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

// Applies one binary operator. Two positions are passed in because the task
// reports different errors at different places:
//  - op_pos:  the operator, for a float result that is infinite or not a number;
//  - rhs_pos: the start of the right operand, for division by zero
//             (in "8 / (4 - 2 * 2)" that is the '(', not the last 2).
// Integer vs float: if EITHER operand is a float the operation is done in
// double (task: "7 / 2.0 is 3.5"); only int op int stays a long long.
static Value apply(Parser *p, TokKind op, size_t op_pos, size_t rhs_pos, Value a, Value b) {
    Value r = { 0, 0, 0.0 };
    if (a.is_float || b.is_float) {
        double x = as_double(a);
        double y = as_double(b);
        r.is_float = 1;
        switch (op) {
        case T_PLUS:  r.f = x + y; break;
        case T_MINUS: r.f = x - y; break;
        case T_STAR:  r.f = x * y; break;
        default:
            // Float division by zero is an error too (1.5 / 0.0), even though
            // C would quietly give inf, so it is checked before dividing.
            if (y == 0.0) {
                fail(p, rhs_pos);
                return r;
            }
            r.f = x / y;
            break;
        }
        if (!isfinite(r.f)) {
            fail(p, op_pos);
        }
    } else {
        switch (op) {
        case T_PLUS:  r.i = a.i + b.i; break;
        case T_MINUS: r.i = a.i - b.i; break;
        case T_STAR:  r.i = a.i * b.i; break;
        default:
            // Integer division by zero is undefined behaviour in C, so it must
            // be caught before the '/' runs, not after.
            if (b.i == 0) {
                fail(p, rhs_pos);
                return r;
            }
            // C's '/' already truncates toward zero (7 / 2 == 3,
            // -7 / 2 == -3), which is exactly what the task asks for.
            r.i = a.i / b.i;
            break;
        }
    }
    return r;
}

// Integer power by repeated squaring: O(log exp) multiplications, so even a
// huge exponent (1 ** 1000000000) is instant. The base is only squared while
// exponent bits remain, so it never grows past the size of the result itself.
// exp == 0 skips the loop and gives 1, which covers x ** 0 and 0 ** 0.
static long long int_pow(long long base, long long exp) {
    long long result = 1;
    while (exp > 0) {
        if (exp & 1) {
            result *= base;
        }
        exp >>= 1;
        if (exp > 0) {
            base *= base;
        }
    }
    return result;
}

// Applies '**'. Positions, as the task's table wants them:
//  - exp_pos: start of the exponent, for a negative integer exponent
//             ("2 ** (1 - 2)" -> the '(');
//  - op_pos:  the '**' itself, for a float result that is inf or NaN
//             ("(0 - 8) ** 0.5" is NaN -> reported at the '**').
// The negative-exponent error is only for int ** int: an integer cannot hold
// 2 ** -1, but with a float involved the task wants pow() (0.5 ** (0 - 1) is 2).
static Value apply_pow(Parser *p, size_t op_pos, size_t exp_pos, Value a, Value b) {
    Value r = { 0, 0, 0.0 };
    if (a.is_float || b.is_float) {
        r.is_float = 1;
        r.f = pow(as_double(a), as_double(b));
        if (!isfinite(r.f)) {
            fail(p, op_pos);
        }
    } else if (b.i < 0) {
        fail(p, exp_pos);
    } else {
        r.i = int_pow(a.i, b.i);
    }
    return r;
}

// Forward declaration: primary needs parse_expr for the inside of ( ),
// and parse_expr is defined further down.
static Value parse_expr(Parser *p);

// primary := NUMBER | '(' expr ')'
// Every "missing operand" error is caught here: whatever token sits where a
// number or '(' must be (a second operator, an invalid character, a ')' as in
// "2 * ()", the line's '\n', or the end of the file) is reported at its own
// start, which is exactly the position the task's table gives for each case.
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
    } else if (t.kind == T_LPAREN) {
        // Parentheses call back up to the LOWEST level (parse_expr), so the
        // inside is a complete expression again: that recursion is what makes
        // ( ) group and lets them nest (the task allows up to 1000 deep).
        next_token(&p->lx);
        v = parse_expr(p);
        if (p->failed) {
            return v;
        }
        // Missing ')': the token found instead is reported. When the line just
        // ends, that token is the '\n' (or the end of the file), which is the
        // position the task's table asks for.
        if (p->lx.tok.kind != T_RPAREN) {
            fail(p, p->lx.tok.pos);
            return v;
        }
        next_token(&p->lx);
    } else {
        fail(p, t.pos);
    }
    return v;
}

// Why this gives the right precedence: each precedence level is its own
// function, and a lower level calls the higher one to get its operands:
// parse_expr (+ -) -> parse_term (* /) -> parse_power (**) -> parse_primary.
// So by the time parse_expr sees a '+', every '*', '/' and '**' next to it has
// already been evaluated: 2 * 3 ** 2 + 1 is (2 * (3 ** 2)) + 1.

// power := primary [ '**' power ]
// Why ** is right-associative: after '**' this function calls ITSELF for the
// right side, so everything to the right is evaluated first:
// 2 ** 3 ** 2 groups as 2 ** (3 ** 2) == 512. (The * / and + - levels use a
// loop instead, which folds to the left.)
static Value parse_power(Parser *p) {
    Value base = parse_primary(p);
    if (p->failed || p->lx.tok.kind != T_POW) {
        return base;
    }
    size_t op_pos = p->lx.tok.pos;
    next_token(&p->lx);
    size_t exp_pos = p->lx.tok.pos;   // start of the exponent
    Value exp = parse_power(p);
    if (p->failed) {
        return base;
    }
    return apply_pow(p, op_pos, exp_pos, base, exp);
}

// term := power { ('*' | '/') power }
// The loop folds each operand into `left` right away, which makes * and /
// left-associative: 100 / 7 / 2 is (100 / 7) / 2.
static Value parse_term(Parser *p) {
    Value left = parse_power(p);
    while (!p->failed && (p->lx.tok.kind == T_STAR || p->lx.tok.kind == T_SLASH)) {
        TokKind op = p->lx.tok.kind;
        size_t op_pos = p->lx.tok.pos;
        next_token(&p->lx);
        size_t rhs_pos = p->lx.tok.pos;   // start of the right operand
        Value right = parse_power(p);
        if (p->failed) {
            break;
        }
        left = apply(p, op, op_pos, rhs_pos, left, right);
    }
    return left;
}

// expr := term { ('+' | '-') term }
// Same loop one level lower, so + and - are left-associative too:
// 10 - 3 + 1 is (10 - 3) + 1.
static Value parse_expr(Parser *p) {
    Value left = parse_term(p);
    while (!p->failed && (p->lx.tok.kind == T_PLUS || p->lx.tok.kind == T_MINUS)) {
        TokKind op = p->lx.tok.kind;
        size_t op_pos = p->lx.tok.pos;
        next_token(&p->lx);
        size_t rhs_pos = p->lx.tok.pos;
        Value right = parse_term(p);
        if (p->failed) {
            break;
        }
        left = apply(p, op, op_pos, rhs_pos, left, right);
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
        // Anything else is reported at that token, because it is where parsing
        // stopped: a number after a number, an invalid character, or an extra
        // ')'. An extra ')' ends up here because every '(' already consumed
        // its own ')' inside parse_primary, so a leftover one has no partner.
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

// Finds the last part of a path: "labs/task1.txt" -> "task1.txt",
// "../in" -> "in", "labs/" -> "labs". Inputs come as paths like ../in/a.txt,
// but output names must only use the file or folder name itself. Trailing '/'
// are skipped first so that "-d labs/" still gives "labs".
// Returns a pointer into `path` and the length of that part in *len.
static const char *base_name(const char *path, size_t *len) {
    size_t end = strlen(path);
    while (end > 1 && path[end - 1] == '/') {
        end--;
    }
    size_t start = end;
    while (start > 0 && path[start - 1] != '/') {
        start--;
    }
    *len = end - start;
    return path + start;
}

// <input_base>: the name without its folder and without ".txt".
static const char *input_base(const char *path, size_t *len) {
    const char *base = base_name(path, len);
    if (*len >= 4 && strncmp(base + *len - 4, ".txt", 4) == 0) {
        *len -= 4;
    }
    return base;
}

// Builds "<outdir>/<base>_<name>_<lastname>_<id>.txt".
static char *output_path(const char *outdir, const char *input) {
    size_t base_len;
    const char *base = input_base(input, &base_len);
    const char *suffix = "_" FIRST_NAME "_" LAST_NAME "_" STUDENT_ID ".txt";
    size_t size = strlen(outdir) + 1 + base_len + strlen(suffix) + 1;
    char *path = malloc(size);
    if (path != NULL) {
        snprintf(path, size, "%s/%.*s%s", outdir, (int)base_len, base, suffix);
    }
    return path;
}

// Builds the default output folder "<input_base>_<name>_<id>" (no last name,
// as in the task's example task1_anna_221234). It goes in the current folder,
// so only the base name is used, never the input's own folder.
static char *default_outdir(const char *input) {
    size_t base_len;
    const char *base = input_base(input, &base_len);
    const char *suffix = "_" FIRST_NAME "_" STUDENT_ID;
    size_t size = base_len + strlen(suffix) + 1;
    char *dir = malloc(size);
    if (dir != NULL) {
        snprintf(dir, size, "%.*s%s", (int)base_len, base, suffix);
    }
    return dir;
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
    // An input with no expressions (empty, or only blank/comment lines) gives an
    // empty output file. out.data is still NULL then, and passing NULL to fwrite
    // is undefined behaviour even with length 0 (the sanitizer reports it), so
    // fwrite is only called when there is something to write.
    int ok = f != NULL && (out.len == 0 || fwrite(out.data, 1, out.len, f) == out.len);
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

// Evaluates every *.txt file directly inside `dir`. Only regular files count:
// stat() filters out subfolders (even one named "x.txt"), and readdir() never
// goes into subfolders, so nested files are ignored as the task requires.
// One bad file does not stop the others; the return value says whether all
// of them worked.
static int process_dir(const char *dir, const char *outdir) {
    DIR *d = opendir(dir);
    if (d == NULL) {
        fprintf(stderr, "calc: cannot open folder '%s'\n", dir);
        return -1;
    }
    int result = 0;
    struct dirent *entry;
    while ((entry = readdir(d)) != NULL) {
        const char *name = entry->d_name;
        size_t name_len = strlen(name);
        if (name_len < 4 || strcmp(name + name_len - 4, ".txt") != 0) {
            continue;
        }
        size_t size = strlen(dir) + 1 + name_len + 1;
        char *path = malloc(size);
        if (path == NULL) {
            result = -1;
            break;
        }
        snprintf(path, size, "%s/%s", dir, name);
        struct stat st;
        if (stat(path, &st) == 0 && S_ISREG(st.st_mode)) {
            if (process_file(path, outdir) != 0) {
                result = -1;
            }
        }
        free(path);
    }
    closedir(d);
    return result;
}

// Command line: calc [-d DIR | --dir DIR] [-o OUTDIR | --output-dir OUTDIR] [input.txt]
// Every way the command line can be wrong (no arguments, unknown option, an
// option without its value, both a file and -d, or neither) prints a message
// to stderr and exits non-zero, as section 4 requires.
int main(int argc, char **argv) {
    const char *outdir = NULL;
    const char *dir = NULL;
    const char *input = NULL;
    for (int k = 1; k < argc; k++) {
        const char *arg = argv[k];
        int is_o = strcmp(arg, "-o") == 0 || strcmp(arg, "--output-dir") == 0;
        int is_d = strcmp(arg, "-d") == 0 || strcmp(arg, "--dir") == 0;
        if (is_o || is_d) {
            if (k + 1 >= argc) {
                fprintf(stderr, "calc: %s needs a folder\n", arg);
                usage();
                return EXIT_FAILURE;
            }
            if (is_o) {
                outdir = argv[++k];
            } else {
                dir = argv[++k];
            }
        } else if (arg[0] == '-') {
            fprintf(stderr, "calc: unknown option '%s'\n", arg);
            usage();
            return EXIT_FAILURE;
        } else if (input == NULL) {
            input = arg;
        } else {
            fprintf(stderr, "calc: more than one input file\n");
            usage();
            return EXIT_FAILURE;
        }
    }
    if ((input == NULL) == (dir == NULL)) {
        if (input != NULL) {
            fprintf(stderr, "calc: give either an input file or -d DIR, not both\n");
        }
        usage();
        return EXIT_FAILURE;
    }
    // A missing input is checked before anything is created, so a typo in the
    // file or folder name does not leave an empty default output folder behind.
    struct stat st;
    if (dir != NULL && (stat(dir, &st) != 0 || !S_ISDIR(st.st_mode))) {
        fprintf(stderr, "calc: '%s' is not a folder\n", dir);
        return EXIT_FAILURE;
    }
    if (input != NULL && (stat(input, &st) != 0 || !S_ISREG(st.st_mode))) {
        fprintf(stderr, "calc: cannot read '%s'\n", input);
        return EXIT_FAILURE;
    }
    // Without -o the folder is <input_base>_<name>_<id>, where <input_base> is
    // the file name without .txt or, with -d, the folder's own name.
    char *made = NULL;
    if (outdir == NULL) {
        made = default_outdir(input != NULL ? input : dir);
        if (made == NULL) {
            fprintf(stderr, "calc: out of memory\n");
            return EXIT_FAILURE;
        }
        outdir = made;
    }
    int rc = (dir != NULL) ? process_dir(dir, outdir) : process_file(input, outdir);
    free(made);
    return rc == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}