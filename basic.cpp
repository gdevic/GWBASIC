// ============================================================================
// basic.cpp — A GW-BASIC style interpreter with a C64-style full-screen
//             editor built on ncurses.
//
// Build:   g++ -std=c++17 -O2 -o basic basic.cpp -lncursesw
// Usage:   basic                 full-screen editor (C64-style)
//          basic FILE.BAS        editor with program loaded
//          basic -r FILE.BAS     run program headless (no curses), then exit
//
// Dialect: GW-BASIC / Microsoft BASIC-80.  Line numbers, GOTO/GOSUB,
// FOR/NEXT, WHILE/WEND, IF/THEN/ELSE, ON..GOTO/GOSUB, DEF FN, DATA/READ,
// DIM, PRINT USING, sequential and random file I/O, ON ERROR/RESUME,
// string functions, CLS/LOCATE/COLOR/INKEY$ on the curses text screen.
// Hardware graphics/sound/port statements raise "Advanced feature" (err 73).
// ============================================================================

#define NCURSES_NOMACROS 1   // keep clear()/move()/refresh() as functions only
#include <ncurses.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <deque>
#include <dirent.h>
#include <fnmatch.h>
#include <fstream>
#include <iostream>
#include <locale.h>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <string>
#include <sys/select.h>
#include <sys/stat.h>
#include <unistd.h>
#include <unordered_map>
#include <vector>

using std::string;

// ---------------------------------------------------------------------------
// Errors (GW-BASIC error codes and messages)
// ---------------------------------------------------------------------------

struct BasicError {
    int code;
    explicit BasicError(int c) : code(c) {}
};

static const std::map<int, const char*>& errTable() {
    static const std::map<int, const char*> t = {
        {1,  "NEXT without FOR"},      {2,  "Syntax error"},
        {3,  "RETURN without GOSUB"},  {4,  "Out of DATA"},
        {5,  "Illegal function call"}, {6,  "Overflow"},
        {7,  "Out of memory"},         {8,  "Undefined line number"},
        {9,  "Subscript out of range"},{10, "Duplicate Definition"},
        {11, "Division by zero"},      {12, "Illegal direct"},
        {13, "Type mismatch"},         {14, "Out of string space"},
        {15, "String too long"},       {16, "String formula too complex"},
        {17, "Can't continue"},        {18, "Undefined user function"},
        {19, "No RESUME"},             {20, "RESUME without error"},
        {22, "Missing operand"},       {23, "Line buffer overflow"},
        {24, "Device Timeout"},        {25, "Device Fault"},
        {26, "FOR without NEXT"},      {27, "Out of Paper"},
        {29, "WHILE without WEND"},    {30, "WEND without WHILE"},
        {50, "FIELD overflow"},        {51, "Internal error"},
        {52, "Bad file number"},       {53, "File not found"},
        {54, "Bad file mode"},         {55, "File already open"},
        {57, "Device I/O Error"},      {58, "File already exists"},
        {61, "Disk full"},             {62, "Input past end"},
        {63, "Bad record number"},     {64, "Bad file name"},
        {66, "Direct statement in file"},{67, "Too many files"},
        {68, "Device Unavailable"},    {69, "Communication buffer overflow"},
        {70, "Permission Denied"},     {71, "Disk not Ready"},
        {72, "Disk media error"},      {73, "Advanced Feature"},
        {74, "Rename across disks"},   {75, "Path/File Access Error"},
        {76, "Path not found"},
    };
    return t;
}

static string errMessage(int code) {
    auto it = errTable().find(code);
    if (it != errTable().end()) return it->second;
    return "Unprintable error";
}

[[noreturn]] static void err(int code) { throw BasicError(code); }

// ---------------------------------------------------------------------------
// Small helpers
// ---------------------------------------------------------------------------

static string upcase(const string& s) {
    string r = s;
    for (char& c : r) c = (char)toupper((unsigned char)c);
    return r;
}

static string rtrim(const string& s) {
    size_t e = s.size();
    while (e > 0 && (s[e - 1] == ' ' || s[e - 1] == '\t' || s[e - 1] == '\r')) e--;
    return s.substr(0, e);
}

static string ltrim(const string& s) {
    size_t b = 0;
    while (b < s.size() && (s[b] == ' ' || s[b] == '\t')) b++;
    return s.substr(b);
}

static string trim(const string& s) { return ltrim(rtrim(s)); }

// CP437 to Unicode, so retro programs (box drawing, card suits...) look right.
static const unsigned short cp437[256] = {
    0x0020,0x263A,0x263B,0x2665,0x2666,0x2663,0x2660,0x2022,
    0x25D8,0x25CB,0x25D9,0x2642,0x2640,0x266A,0x266B,0x263C,
    0x25BA,0x25C4,0x2195,0x203C,0x00B6,0x00A7,0x25AC,0x21A8,
    0x2191,0x2193,0x2192,0x2190,0x221F,0x2194,0x25B2,0x25BC,
    0x0020,0x0021,0x0022,0x0023,0x0024,0x0025,0x0026,0x0027,
    0x0028,0x0029,0x002A,0x002B,0x002C,0x002D,0x002E,0x002F,
    0x0030,0x0031,0x0032,0x0033,0x0034,0x0035,0x0036,0x0037,
    0x0038,0x0039,0x003A,0x003B,0x003C,0x003D,0x003E,0x003F,
    0x0040,0x0041,0x0042,0x0043,0x0044,0x0045,0x0046,0x0047,
    0x0048,0x0049,0x004A,0x004B,0x004C,0x004D,0x004E,0x004F,
    0x0050,0x0051,0x0052,0x0053,0x0054,0x0055,0x0056,0x0057,
    0x0058,0x0059,0x005A,0x005B,0x005C,0x005D,0x005E,0x005F,
    0x0060,0x0061,0x0062,0x0063,0x0064,0x0065,0x0066,0x0067,
    0x0068,0x0069,0x006A,0x006B,0x006C,0x006D,0x006E,0x006F,
    0x0070,0x0071,0x0072,0x0073,0x0074,0x0075,0x0076,0x0077,
    0x0078,0x0079,0x007A,0x007B,0x007C,0x007D,0x007E,0x2302,
    0x00C7,0x00FC,0x00E9,0x00E2,0x00E4,0x00E0,0x00E5,0x00E7,
    0x00EA,0x00EB,0x00E8,0x00EF,0x00EE,0x00EC,0x00C4,0x00C5,
    0x00C9,0x00E6,0x00C6,0x00F4,0x00F6,0x00F2,0x00FB,0x00F9,
    0x00FF,0x00D6,0x00DC,0x00A2,0x00A3,0x00A5,0x20A7,0x0192,
    0x00E1,0x00ED,0x00F3,0x00FA,0x00F1,0x00D1,0x00AA,0x00BA,
    0x00BF,0x2310,0x00AC,0x00BD,0x00BC,0x00A1,0x00AB,0x00BB,
    0x2591,0x2592,0x2593,0x2502,0x2524,0x2561,0x2562,0x2556,
    0x2555,0x2563,0x2551,0x2557,0x255D,0x255C,0x255B,0x2510,
    0x2514,0x2534,0x252C,0x251C,0x2500,0x253C,0x255E,0x255F,
    0x255A,0x2554,0x2569,0x2566,0x2560,0x2550,0x256C,0x2567,
    0x2568,0x2564,0x2565,0x2559,0x2558,0x2552,0x2553,0x256B,
    0x256A,0x2518,0x250C,0x2588,0x2584,0x258C,0x2590,0x2580,
    0x03B1,0x00DF,0x0393,0x03C0,0x03A3,0x03C3,0x00B5,0x03C4,
    0x03A6,0x0398,0x03A9,0x03B4,0x221E,0x03C6,0x03B5,0x2229,
    0x2261,0x00B1,0x2265,0x2264,0x2320,0x2321,0x00F7,0x2248,
    0x00B0,0x2219,0x00B7,0x221A,0x207F,0x00B2,0x25A0,0x00A0,
};

static string utf8Of(unsigned short u) {
    string r;
    if (u < 0x80) r += (char)u;
    else if (u < 0x800) {
        r += (char)(0xC0 | (u >> 6));
        r += (char)(0x80 | (u & 0x3F));
    } else {
        r += (char)(0xE0 | (u >> 12));
        r += (char)(0x80 | ((u >> 6) & 0x3F));
        r += (char)(0x80 | (u & 0x3F));
    }
    return r;
}

// ---------------------------------------------------------------------------
// Values.  Types: '%' integer (16-bit), '!' single, '#' double, '$' string.
// Numerics are kept in a double; the tag drives rounding/printing rules.
// ---------------------------------------------------------------------------

struct Value {
    char t = '!';
    double n = 0.0;
    string s;

    bool isStr() const { return t == '$'; }
    static Value num(double v, char ty = '!') { Value x; x.t = ty; x.n = v; return x; }
    static Value str(const string& v) { Value x; x.t = '$'; x.s = v; return x; }
};

static const double SINGLE_MAX = 3.402823e38;

// Round/range-check a double into the given numeric type.
static double coerceNum(double v, char ty) {
    if (ty == '%') {
        if (!(v >= -32768.5 && v < 32767.5)) err(6);
        return rint(v);                 // banker's rounding, like CINT
    }
    if (ty == '!') {
        if (std::isnan(v)) err(5);
        if (fabs(v) > SINGLE_MAX) err(6);
        return (double)(float)v;
    }
    if (std::isnan(v)) err(5);
    if (std::isinf(v)) err(6);
    return v;
}

static int toInt16(const Value& v) {
    if (v.isStr()) err(13);
    return (int)coerceNum(v.n, '%');
}

static double toNum(const Value& v) {
    if (v.isStr()) err(13);
    return v.n;
}

static string toStr(const Value& v) {
    if (!v.isStr()) err(13);
    return v.s;
}

// Result type of an arithmetic operation.
static char promote(char a, char b) {
    if (a == '#' || b == '#') return '#';
    if (a == '!' || b == '!') return '!';
    return '%';
}

// ---------------------------------------------------------------------------
// Number formatting, GW-BASIC style.
// Positive numbers carry no sign here; PRINT adds the leading blank.
// Singles show up to 7 significant digits, doubles 16.  Values outside
// [0.01, 10^digits) use scientific notation with E (single) or D (double).
// ---------------------------------------------------------------------------

static string fmtNum(double v, char ty) {
    if (ty == '%') {
        char b[16];
        snprintf(b, sizeof b, "%d", (int)lrint(v));
        return b;
    }
    int sig = (ty == '#') ? 16 : 7;
    char ec = (ty == '#') ? 'D' : 'E';
    if (v == 0) return "0";
    bool neg = v < 0;
    double av = fabs(v);
    char buf[64];
    snprintf(buf, sizeof buf, "%.*e", sig - 1, av);
    string m;
    int e = 0;
    {
        const char* p = buf;
        for (; *p && *p != 'e' && *p != 'E'; p++)
            if (isdigit((unsigned char)*p)) m += *p;
        if (*p) e = atoi(p + 1);
    }
    while (m.size() > 1 && m.back() == '0') m.pop_back();
    string out;
    if (e >= -2 && e < sig) {           // fixed notation
        int dp = e + 1;                 // digit count left of the point
        if (dp <= 0) {
            out = ".";
            out.append(-dp, '0');
            out += m;
        } else if ((int)m.size() <= dp) {
            out = m;
            out.append(dp - m.size(), '0');
        } else {
            out = m.substr(0, dp) + "." + m.substr(dp);
        }
    } else {                            // scientific notation
        out = m.substr(0, 1);
        if (m.size() > 1) out += "." + m.substr(1);
        char sb[12];
        snprintf(sb, sizeof sb, "%c%+03d", ec, e);
        out += sb;
    }
    if (neg) out = "-" + out;
    return out;
}

// Number as PRINT shows it: leading blank or '-', no trailing blank.
static string fmtNumSigned(const Value& v) {
    string s = fmtNum(v.n, v.t);
    if (!s.empty() && s[0] == '-') return s;
    return " " + s;
}

// ---------------------------------------------------------------------------
// Numeric literal scanning, shared by the tokenizer and VAL().
// Returns chars consumed (0 = no number present).
// ---------------------------------------------------------------------------

static size_t lexNumber(const string& s, size_t i, double& out, char& ty) {
    size_t start = i;
    out = 0;
    ty = '!';
    if (i < s.size() && s[i] == '&') {                   // &H hex, &O / & octal
        size_t j = i + 1;
        int base = 8;
        if (j < s.size() && (toupper((unsigned char)s[j]) == 'H')) { base = 16; j++; }
        else if (j < s.size() && (toupper((unsigned char)s[j]) == 'O')) { j++; }
        long v = 0;
        size_t d = j;
        while (d < s.size() && isxdigit((unsigned char)s[d])) {
            int dig = isdigit((unsigned char)s[d]) ? s[d] - '0'
                                                   : toupper((unsigned char)s[d]) - 'A' + 10;
            if (dig >= base) break;
            v = v * base + dig;
            if (v > 0xFFFF) err(6);
            d++;
        }
        if (d == j) return 0;                            // no digits: not a number
        if (v > 0x7FFF) v -= 0x10000;                    // 16-bit signed wrap
        out = (double)v;
        ty = '%';
        return d - i;
    }
    bool digits = false, dot = false;
    string num;
    while (i < s.size()) {
        char c = s[i];
        if (isdigit((unsigned char)c)) { digits = true; num += c; i++; }
        else if (c == '.' && !dot) { dot = true; num += c; i++; }
        else break;
    }
    if (!digits && !dot) return 0;
    if (!digits && dot) { /* lone '.' is not a number */ return 0; }
    bool hasExp = false;
    char expc = 0;
    if (i < s.size()) {
        char c = toupper((unsigned char)s[i]);
        if (c == 'E' || c == 'D') {
            size_t j = i + 1;
            if (j < s.size() && (s[j] == '+' || s[j] == '-')) j++;
            if (j < s.size() && isdigit((unsigned char)s[j])) {
                hasExp = true;
                expc = c;
                num += 'e';
                i++;
                if (s[i] == '+' || s[i] == '-') { num += s[i]; i++; }
                while (i < s.size() && isdigit((unsigned char)s[i])) { num += s[i]; i++; }
            }
        }
    }
    char suffix = 0;
    if (i < s.size() && (s[i] == '!' || s[i] == '#' || s[i] == '%')) {
        suffix = s[i];
        i++;
    }
    out = atof(num.c_str());
    int sigDigits = 0;
    for (char c : num) {
        if (c == 'e') break;
        if (isdigit((unsigned char)c)) sigDigits++;
    }
    if (suffix == '%') {
        if (!(out >= -32768.5 && out < 32767.5)) err(6);
        out = rint(out);
        ty = '%';
    } else if (suffix == '#' || expc == 'D') {
        ty = '#';
    } else if (suffix == '!') {
        ty = '!';
    } else if (sigDigits > 7) {
        ty = '#';
    } else if (dot || hasExp) {
        ty = '!';
    } else if (out <= 32767) {
        ty = '%';
    } else {
        ty = '!';
    }
    if (ty == '!' && fabs(out) > SINGLE_MAX) err(6);
    return i - start;
}

// VAL() semantics: skip blanks, optional sign, parse as much as possible.
static double parseVal(const string& s, bool* fullyConsumed = nullptr) {
    size_t i = 0;
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) i++;
    bool neg = false;
    if (i < s.size() && (s[i] == '+' || s[i] == '-')) {
        neg = (s[i] == '-');
        i++;
    }
    double v = 0;
    char ty;
    size_t n = lexNumber(s, i, v, ty);
    if (fullyConsumed) {
        size_t j = i + n;
        while (j < s.size() && (s[j] == ' ' || s[j] == '\t')) j++;
        *fullyConsumed = (n > 0) && (j == s.size());
    }
    if (n == 0) return 0;
    return neg ? -v : v;
}

// ---------------------------------------------------------------------------
// Keywords and tokens
// ---------------------------------------------------------------------------

enum class Kw {
    NONE,
    // statements / commands
    AUTO, BEEP, CALL, CHAIN, CIRCLE, CLEAR, CLOSE, CLS, COLOR, COM, COMMON,
    CONT, DATA, DEF, DEFDBL, DEFINT, DEFSNG, DEFSTR, DELETE, DIM, DRAW, EDIT,
    ELSE, END, ENVIRON, ERASE, ERROR, FIELD, FILES, FOR, GET, GOSUB, GOTO,
    IF, INPUT, IOCTL, KEY, KILL, LET, LINE, LIST, LLIST, LOAD, LOCATE, LOCK,
    LPRINT, LSET, MERGE, MOTOR, NAME, NEW, NEXT, ON, OPEN, OPTION, OUT,
    PAINT, PALETTE, PCOPY, PEN, PLAY, POKE, PRESET, PSET, PRINT, PUT,
    RANDOMIZE, READ, REM, RENUM, RESET, RESTORE, RESUME, RETURN, RSET, RUN,
    SAVE, SCREEN, SHELL, SOUND, STOP, STRIG, SWAP, SYSTEM, TROFF, TRON,
    UNLOCK, VIEW, WAIT, WEND, WHILE, WIDTH, WRITE,
    // clause words
    THEN, TO, STEP, AS, USING, BASE, ALL, MERGEKW_UNUSED, OFF, SEG,
    // operators
    AND, OR, XOR, EQV, IMP, MOD, NOT, IDIV_UNUSED,
    // functions
    ABS, ASC, ATN, CDBL, CHRS, CINT, COS, CSNG, CSRLIN, CVD, CVI, CVS,
    DATES, ENVIRONS, EOFF, ERDEV, ERL, ERRF, EXP, FIX, FN, FRE, HEXS, INKEYS,
    INP, INPUTS, INSTR, INTF, IOCTLS, LEFTS, LEN, LOC, LOF, LOG, LPOS,
    MIDS, MKDS, MKIS, MKSS, OCTS, PEEK, PMAP, POINT, POS, RIGHTS, RND,
    SCREENF_UNUSED, SGN, SIN, SPACES, SPC, SQR, STICK, STRS, STRINGS, TAB,
    TAN, TIMES, TIMER, USR, VAL, VARPTR, VARPTRS,
};

struct KwDef { const char* name; Kw kw; };

// Lookup table: BASIC source text -> keyword.
static const std::vector<KwDef>& kwDefs() {
    static const std::vector<KwDef> defs = {
        {"AUTO",Kw::AUTO},{"BEEP",Kw::BEEP},{"CALL",Kw::CALL},{"CHAIN",Kw::CHAIN},
        {"CIRCLE",Kw::CIRCLE},{"CLEAR",Kw::CLEAR},{"CLOSE",Kw::CLOSE},{"CLS",Kw::CLS},
        {"COLOR",Kw::COLOR},{"COM",Kw::COM},{"COMMON",Kw::COMMON},{"CONT",Kw::CONT},
        {"DATA",Kw::DATA},{"DEF",Kw::DEF},{"DEFDBL",Kw::DEFDBL},{"DEFINT",Kw::DEFINT},
        {"DEFSNG",Kw::DEFSNG},{"DEFSTR",Kw::DEFSTR},{"DELETE",Kw::DELETE},
        {"DIM",Kw::DIM},{"DRAW",Kw::DRAW},{"EDIT",Kw::EDIT},{"ELSE",Kw::ELSE},
        {"END",Kw::END},{"ENVIRON",Kw::ENVIRON},{"ERASE",Kw::ERASE},
        {"ERROR",Kw::ERROR},{"FIELD",Kw::FIELD},{"FILES",Kw::FILES},{"FOR",Kw::FOR},
        {"GET",Kw::GET},{"GOSUB",Kw::GOSUB},{"GOTO",Kw::GOTO},{"IF",Kw::IF},
        {"INPUT",Kw::INPUT},{"IOCTL",Kw::IOCTL},{"KEY",Kw::KEY},{"KILL",Kw::KILL},
        {"LET",Kw::LET},{"LINE",Kw::LINE},{"LIST",Kw::LIST},{"LLIST",Kw::LLIST},
        {"LOAD",Kw::LOAD},{"LOCATE",Kw::LOCATE},{"LOCK",Kw::LOCK},
        {"LPRINT",Kw::LPRINT},{"LSET",Kw::LSET},{"MERGE",Kw::MERGE},
        {"MOTOR",Kw::MOTOR},{"NAME",Kw::NAME},{"NEW",Kw::NEW},{"NEXT",Kw::NEXT},
        {"ON",Kw::ON},{"OPEN",Kw::OPEN},{"OPTION",Kw::OPTION},{"OUT",Kw::OUT},
        {"PAINT",Kw::PAINT},{"PALETTE",Kw::PALETTE},{"PCOPY",Kw::PCOPY},
        {"PEN",Kw::PEN},{"PLAY",Kw::PLAY},{"POKE",Kw::POKE},{"PRESET",Kw::PRESET},
        {"PSET",Kw::PSET},{"PRINT",Kw::PRINT},{"PUT",Kw::PUT},
        {"RANDOMIZE",Kw::RANDOMIZE},{"READ",Kw::READ},{"REM",Kw::REM},
        {"RENUM",Kw::RENUM},{"RESET",Kw::RESET},{"RESTORE",Kw::RESTORE},
        {"RESUME",Kw::RESUME},{"RETURN",Kw::RETURN},{"RSET",Kw::RSET},
        {"RUN",Kw::RUN},{"SAVE",Kw::SAVE},{"SCREEN",Kw::SCREEN},
        {"SHELL",Kw::SHELL},{"SOUND",Kw::SOUND},{"STOP",Kw::STOP},
        {"STRIG",Kw::STRIG},{"SWAP",Kw::SWAP},{"SYSTEM",Kw::SYSTEM},
        {"TROFF",Kw::TROFF},{"TRON",Kw::TRON},{"UNLOCK",Kw::UNLOCK},
        {"VIEW",Kw::VIEW},
        {"WAIT",Kw::WAIT},{"WEND",Kw::WEND},{"WHILE",Kw::WHILE},
        {"WIDTH",Kw::WIDTH},{"WRITE",Kw::WRITE},
        {"THEN",Kw::THEN},{"TO",Kw::TO},{"STEP",Kw::STEP},{"AS",Kw::AS},
        {"USING",Kw::USING},{"BASE",Kw::BASE},{"ALL",Kw::ALL},{"OFF",Kw::OFF},
        {"SEG",Kw::SEG},
        {"AND",Kw::AND},{"OR",Kw::OR},{"XOR",Kw::XOR},{"EQV",Kw::EQV},
        {"IMP",Kw::IMP},{"MOD",Kw::MOD},{"NOT",Kw::NOT},
        {"ABS",Kw::ABS},{"ASC",Kw::ASC},{"ATN",Kw::ATN},{"CDBL",Kw::CDBL},
        {"CHR$",Kw::CHRS},{"CINT",Kw::CINT},{"COS",Kw::COS},{"CSNG",Kw::CSNG},
        {"CSRLIN",Kw::CSRLIN},{"CVD",Kw::CVD},{"CVI",Kw::CVI},{"CVS",Kw::CVS},
        {"DATE$",Kw::DATES},{"ENVIRON$",Kw::ENVIRONS},{"EOF",Kw::EOFF},
        {"ERDEV",Kw::ERDEV},{"ERL",Kw::ERL},{"ERR",Kw::ERRF},{"EXP",Kw::EXP},
        {"FIX",Kw::FIX},{"FN",Kw::FN},{"FRE",Kw::FRE},{"HEX$",Kw::HEXS},
        {"INKEY$",Kw::INKEYS},{"INP",Kw::INP},{"INPUT$",Kw::INPUTS},
        {"INSTR",Kw::INSTR},{"INT",Kw::INTF},{"IOCTL$",Kw::IOCTLS},
        {"LEFT$",Kw::LEFTS},{"LEN",Kw::LEN},{"LOC",Kw::LOC},{"LOF",Kw::LOF},
        {"LOG",Kw::LOG},{"LPOS",Kw::LPOS},{"MID$",Kw::MIDS},{"MKD$",Kw::MKDS},
        {"MKI$",Kw::MKIS},{"MKS$",Kw::MKSS},{"OCT$",Kw::OCTS},{"PEEK",Kw::PEEK},
        {"PMAP",Kw::PMAP},{"POINT",Kw::POINT},{"POS",Kw::POS},
        {"RIGHT$",Kw::RIGHTS},{"RND",Kw::RND},{"SGN",Kw::SGN},{"SIN",Kw::SIN},
        {"SPACE$",Kw::SPACES},{"SPC",Kw::SPC},{"SQR",Kw::SQR},
        {"STICK",Kw::STICK},{"STR$",Kw::STRS},{"STRING$",Kw::STRINGS},
        {"TAB",Kw::TAB},{"TAN",Kw::TAN},{"TIME$",Kw::TIMES},{"TIMER",Kw::TIMER},
        {"USR",Kw::USR},{"VAL",Kw::VAL},{"VARPTR",Kw::VARPTR},
        {"VARPTR$",Kw::VARPTRS},
    };
    return defs;
}

static const std::unordered_map<string, Kw>& kwMap() {
    static std::unordered_map<string, Kw> m = [] {
        std::unordered_map<string, Kw> r;
        for (auto& d : kwDefs()) r[d.name] = d.kw;
        return r;
    }();
    return m;
}

[[maybe_unused]] static const char* kwName(Kw k) {
    for (auto& d : kwDefs())
        if (d.kw == k) return d.name;
    return "?";
}

struct Token {
    enum Type { End, Num, Str, Id, Key, Pun, Dat, Rem } t = End;
    Kw kw = Kw::NONE;
    string s;       // identifier name (with suffix), punct text, string/data body
    double n = 0;   // numeric value
    char nt = '!';  // numeric literal type
    int pos = 0;    // offset of token in source text (for RENUM/errors)
    int len = 0;
};

// Tokenize one program line (text after the line number).  Never throws:
// anything unrecognized is passed through as punctuation and the statement
// executor reports "Syntax error" when it stumbles on it.
static std::vector<Token> tokenize(const string& src) {
    std::vector<Token> out;
    size_t i = 0;
    const size_t n = src.size();
    auto push = [&](Token tk, size_t start, size_t end) {
        tk.pos = (int)start;
        tk.len = (int)(end - start);
        out.push_back(tk);
    };
    while (i < n) {
        char c = src[i];
        if (c == ' ' || c == '\t') { i++; continue; }
        size_t start = i;
        if (c == '"') {                                  // string literal
            string v;
            i++;
            while (i < n && src[i] != '"') v += src[i++];
            if (i < n) i++;                              // closing quote optional at EOL
            Token tk; tk.t = Token::Str; tk.s = v;
            push(tk, start, i);
            continue;
        }
        if (c == '\'') {                                 // ' comment
            Token tk; tk.t = Token::Rem; tk.s = src.substr(i + 1);
            push(tk, start, n);
            break;
        }
        if (c == '?') {                                  // ? is PRINT
            Token tk; tk.t = Token::Key; tk.kw = Kw::PRINT;
            push(tk, start, i + 1);
            i++;
            continue;
        }
        if (isdigit((unsigned char)c) ||
            (c == '.' && i + 1 < n && isdigit((unsigned char)src[i + 1])) ||
            c == '&') {
            double v; char ty;
            size_t used = lexNumber(src, i, v, ty);
            if (used > 0) {
                Token tk; tk.t = Token::Num; tk.n = v; tk.nt = ty;
                push(tk, start, i + used);
                i += used;
                continue;
            }
            // '&' with no digits falls through to punctuation
        }
        if (isalpha((unsigned char)c)) {
            // Scan the whole alphanumeric/dot run, then try keywords at its
            // start.  A keyword matches only if not followed by a letter,
            // '$' or '.', so TOTAL and FORI remain ordinary variables.
            size_t j = i;
            string run;
            while (j < n && (isalnum((unsigned char)src[j]) || src[j] == '.')) {
                run += (char)toupper((unsigned char)src[j]);
                j++;
            }
            char after = (j < n) ? src[j] : '\0';
            Kw matched = Kw::NONE;
            size_t mlen = 0;
            for (size_t k = std::min(run.size(), (size_t)9); k >= 2 && matched == Kw::NONE; k--) {
                string pre = run.substr(0, k);
                char follow = (k < run.size()) ? run[k] : after;
                if (k == run.size() && after == '$') {
                    auto it = kwMap().find(pre + "$");
                    if (it != kwMap().end()) { matched = it->second; mlen = k + 1; break; }
                }
                auto it = kwMap().find(pre);
                if (it != kwMap().end()) {
                    if (!(isalpha((unsigned char)follow) || follow == '$' || follow == '.')) {
                        matched = it->second;
                        mlen = k;
                    }
                }
            }
            if (run.size() > 2 && run.compare(0, 2, "FN") == 0) {
                // FN prefix introduces a user function call: emit FN, then
                // continue with the function's name.
                Token tk; tk.t = Token::Key; tk.kw = Kw::FN;
                push(tk, start, i + 2);
                i += 2;
                continue;
            }
            if (matched != Kw::NONE) {
                Token tk; tk.t = Token::Key; tk.kw = matched;
                push(tk, start, i + mlen);
                i += mlen;
                if (matched == Kw::REM) {
                    size_t rs = i;
                    Token r; r.t = Token::Rem; r.s = src.substr(i);
                    push(r, rs, n);
                    break;
                }
                if (matched == Kw::DATA) {
                    // DATA items: raw text, comma separated, ':' ends.
                    // There is always at least one item, and one after each
                    // comma — so "DATA" and "DATA 1," both yield empty items.
                    while (true) {
                        while (i < n && (src[i] == ' ' || src[i] == '\t')) i++;
                        size_t is = i;
                        string item;
                        bool quoted = false;
                        if (i < n && src[i] == '"') {
                            quoted = true;
                            i++;
                            while (i < n && src[i] != '"') item += src[i++];
                            if (i < n) i++;
                            while (i < n && src[i] != ',' && src[i] != ':') i++;
                        } else {
                            while (i < n && src[i] != ',' && src[i] != ':') item += src[i++];
                            item = rtrim(item);
                        }
                        Token d; d.t = Token::Dat; d.s = item;
                        d.nt = quoted ? 'Q' : ' ';
                        push(d, is, std::max(is, i));
                        if (i < n && src[i] == ',') {
                            Token p; p.t = Token::Pun; p.s = ",";
                            push(p, i, i + 1);
                            i++;
                            continue;
                        }
                        break;                           // ':' or end of line
                    }
                }
                continue;
            }
            // Ordinary identifier with optional type suffix.
            string name = run;
            i = j;
            if (i < n && (src[i] == '$' || src[i] == '%' || src[i] == '!' || src[i] == '#')) {
                name += src[i];
                i++;
            }
            Token tk; tk.t = Token::Id; tk.s = name;
            push(tk, start, i);
            continue;
        }
        // Punctuation, with two-char relational operators normalized.
        string p(1, c);
        if (i + 1 < n) {
            char d = src[i + 1];
            if ((c == '<' && (d == '=' || d == '>')) ||
                (c == '>' && (d == '=' || d == '<')) ||
                (c == '=' && (d == '<' || d == '>'))) {
                if ((c == '<' && d == '>') || (c == '>' && d == '<')) p = "<>";
                else if (c == '<' || d == '<') p = "<=";
                else p = ">=";
                i++;
            }
        }
        Token tk; tk.t = Token::Pun; tk.s = p;
        push(tk, start, i + 1);
        i++;
    }
    return out;
}

// ---------------------------------------------------------------------------
// Screen abstraction.  CursesScreen drives the terminal; PlainScreen is the
// headless stdout/stdin fallback used by `basic -r file.bas` and pipes.
// ---------------------------------------------------------------------------

// Extended keys are reported GW-style as CHR$(0)+CHR$(scan).
struct KeyEvent {
    int ch = -1;        // ASCII byte, or 0 for extended
    int scan = 0;       // scan code when ch==0
    bool none() const { return ch < 0; }
};

static volatile sig_atomic_t g_sigint = 0;
static void sigintHandler(int) { g_sigint = 1; }

struct Screen {
    int widthLimit = 80;          // logical width set by WIDTH
    virtual ~Screen() {}
    virtual int rows() = 0;
    virtual int cols() = 0;
    virtual void cls() = 0;
    virtual void locate(int r, int c) = 0;            // 1-based
    virtual int row() = 0;                            // 1-based
    virtual int col() = 0;                            // 1-based
    virtual void setColor(int fg, int bg) = 0;
    virtual void putByte(unsigned char b) = 0;        // raw glyph, advances
    virtual void flush() {}
    virtual unsigned char charAt(int r, int c) = 0;   // 1-based
    virtual unsigned char attrAt(int, int) { return 7; }
    virtual void setViewPrint(int, int) {}            // VIEW PRINT t TO b
    virtual void setSoftKeys(const std::vector<string>*) {}
    virtual KeyEvent pollKey() = 0;                   // non-blocking
    virtual KeyEvent waitKey() = 0;                   // blocking
    virtual void beepNow() = 0;
    virtual void setCursorVisible(bool) {}
    // Non-blocking break (Ctrl-C) check between statements.
    virtual bool breakPending() {
        if (g_sigint) { g_sigint = 0; return true; }
        return false;
    }

    // Write text interpreting BASIC control characters.
    void write(const string& s) {
        for (unsigned char b : s) writeChar(b);
    }
    void writeChar(unsigned char b) {
        switch (b) {
            case 7:  beepNow(); return;
            case 8:  if (col() > 1) locate(row(), col() - 1); return;
            case 9:  do { putByte(' '); } while ((col() - 1) % 8 != 0); return;
            case 10: case 13: newline(); return;
            case 11: locate(1, 1); return;
            case 12: cls(); return;
            case 28: locate(row(), std::min(col() + 1, cols())); return;
            case 29: if (col() > 1) locate(row(), col() - 1); return;
            case 30: if (row() > 1) locate(row() - 1, col()); return;
            case 31: if (row() < rows()) locate(row() + 1, col()); return;
            default: putByte(b); return;
        }
    }
    virtual void newline() = 0;
};

// ---------------------------------------------------------------------------
// CursesScreen: scrolling text screen with a shadow buffer so the editor can
// re-read what is on screen (the heart of C64-style editing) and so wrapped
// logical lines can be stitched back together.
// ---------------------------------------------------------------------------

struct CursesScreen : Screen {
    int H = 24, W = 80;
    int fg = 7, bg = 0;
    int viewTop = 1, viewBot = 24;       // VIEW PRINT scroll region (1-based)
    bool keyRowOn = false;               // KEY ON reserves the bottom row
    std::vector<string> shadow;          // H rows of W bytes (original CP437 bytes)
    std::vector<string> attrSh;          // parallel color attributes (fg | bg<<4)
    std::vector<char> wrapped;           // row continues the previous row's logical line
    std::deque<KeyEvent> pending;        // keys read while polling for break

    unsigned char curAttr() const {
        return (unsigned char)((fg & 15) | ((bg & 7) << 4));
    }

    CursesScreen() {
        initscr();
        raw();
        noecho();
        nonl();
        keypad(stdscr, TRUE);
        scrollok(stdscr, TRUE);
        idlok(stdscr, TRUE);
        set_escdelay(50);
        if (has_colors()) {
            start_color();
            use_default_colors();
            for (int f = 0; f < 8; f++)
                for (int b = 0; b < 8; b++)
                    init_pair(1 + f * 8 + b, f, b == 0 ? -1 : b);
        }
        getmaxyx(stdscr, H, W);
        if (W > 255) W = 255;
        widthLimit = std::min(80, W);
        viewTop = 1;
        viewBot = H;
        shadow.assign(H, string(W, ' '));
        attrSh.assign(H, string(W, (char)7));
        wrapped.assign(H, 0);
        applyColor();
    }
    ~CursesScreen() override { endwin(); }

    void applyColor() {
        int pair = 1 + (fg & 7) * 8 + (bg & 7);
        attrset(COLOR_PAIR(pair) | ((fg & 8) ? A_BOLD : 0));
        bkgdset(' ' | COLOR_PAIR(1 + 7 * 8 + (bg & 7)));
    }

    int rows() override { return H; }
    int cols() override { return widthLimit; }
    int row() override { int y, x; getyx(stdscr, y, x); (void)x; return y + 1; }
    int col() override { int y, x; getyx(stdscr, y, x); (void)y; return x + 1; }

    // Redraw one row from the shadow buffers, honoring each cell's color.
    void redrawShadowRow(int r) {
        for (int c = 1; c <= W; c++) {
            unsigned char b = (unsigned char)shadow[r - 1][c - 1];
            unsigned char a = (unsigned char)attrSh[r - 1][c - 1];
            int pair = 1 + (a & 7) * 8 + ((a >> 4) & 7);
            attrset(COLOR_PAIR(pair) | ((a & 8) ? A_BOLD : 0));
            unsigned short u = cp437[b];
            if (u < 128) mvaddch(r - 1, c - 1, (chtype)u);
            else mvaddstr(r - 1, c - 1, utf8Of(u).c_str());
        }
        applyColor();
    }

    void cls() override {
        if (viewTop == 1 && viewBot == H) {
            clear();
            for (auto& r : shadow) r.assign(W, ' ');
            for (auto& r : attrSh) r.assign(W, (char)curAttr());
            std::fill(wrapped.begin(), wrapped.end(), 0);
            move(0, 0);
            refresh();
            return;
        }
        for (int r = viewTop; r <= viewBot; r++) {   // clear the viewport only
            shadow[r - 1].assign(W, ' ');
            attrSh[r - 1].assign(W, (char)curAttr());
            wrapped[r - 1] = 0;
            redrawShadowRow(r);
        }
        move(viewTop - 1, 0);
        refresh();
    }

    void setViewPrint(int t, int b) override {
        int maxRow = keyRowOn ? H - 1 : H;
        if (t <= 0) {                                // VIEW PRINT: reset
            viewTop = 1;
            viewBot = maxRow;
        } else {
            viewTop = std::min(t, maxRow);
            viewBot = std::min(b, maxRow);
        }
        locate(viewTop, 1);
    }

    void setSoftKeys(const std::vector<string>* labels) override {
        int y, x;
        getyx(stdscr, y, x);
        if (labels) {
            keyRowOn = true;
            if (viewBot == H) viewBot = H - 1;
            string rowtxt;
            for (int i = 0; i < 10; i++) {
                string vis = std::to_string(i + 1);
                for (char c : (*labels)[i]) {
                    if (vis.size() >= 7) break;
                    vis += (c == '\r') ? ' ' : c;
                }
                vis.resize(8, ' ');
                rowtxt += vis;
            }
            rowtxt.resize(W, ' ');
            for (int c = 1; c <= W; c++) putCell(H, c, (unsigned char)rowtxt[c - 1]);
        } else {
            keyRowOn = false;
            if (viewBot == H - 1) viewBot = H;
            for (int c = 1; c <= W; c++) putCell(H, c, ' ');
        }
        move(y, x);
        refresh();
    }

    void locate(int r, int c) override {
        r = std::max(1, std::min(r, H));
        c = std::max(1, std::min(c, W));
        move(r - 1, c - 1);
    }

    void setColor(int f, int b) override {
        fg = f & 15;
        bg = b & 7;
        applyColor();
    }

    void scrollUp() {                    // full-screen scroll (editor use)
        scrl(1);
        shadow.erase(shadow.begin());
        shadow.push_back(string(W, ' '));
        attrSh.erase(attrSh.begin());
        attrSh.push_back(string(W, (char)curAttr()));
        wrapped.erase(wrapped.begin());
        wrapped.push_back(0);
    }

    void scrollRegion() {
        if (viewTop == 1 && viewBot == H) {          // fast path: whole screen
            scrollUp();
            return;
        }
        for (int r = viewTop; r < viewBot; r++) {
            shadow[r - 1] = shadow[r];
            attrSh[r - 1] = attrSh[r];
            wrapped[r - 1] = wrapped[r];
        }
        shadow[viewBot - 1].assign(W, ' ');
        attrSh[viewBot - 1].assign(W, (char)curAttr());
        wrapped[viewBot - 1] = 0;
        for (int r = viewTop; r <= viewBot; r++) redrawShadowRow(r);
    }

    void newline() override {
        int y = row();
        if (y >= viewBot) { scrollRegion(); move(viewBot - 1, 0); }
        else move(y, 0);
    }

    void putByte(unsigned char b) override {
        int y = row() - 1, x = col() - 1;
        shadow[y][x] = (char)b;
        attrSh[y][x] = (char)curAttr();
        unsigned short u = cp437[b];
        if (u < 128) addch((chtype)u);
        else addstr(utf8Of(u).c_str());
        move(y, std::min(x + 1, W - 1));
        if (x + 1 >= widthLimit) {                  // wrap to next row
            if (y + 1 >= viewBot) { scrollRegion(); y--; }
            move(y + 1, 0);
            wrapped[y + 1] = 1;
        }
    }

    // Overwrite a cell without moving the BASIC cursor logic (editor use).
    void putCell(int r, int c, unsigned char b) {
        if (r < 1 || r > H || c < 1 || c > W) return;
        shadow[r - 1][c - 1] = (char)b;
        attrSh[r - 1][c - 1] = (char)curAttr();
        unsigned short u = cp437[b];
        if (u < 128) mvaddch(r - 1, c - 1, (chtype)u);
        else mvaddstr(r - 1, c - 1, utf8Of(u).c_str());
    }

    void flush() override { refresh(); }

    unsigned char charAt(int r, int c) override {
        if (r < 1 || r > H || c < 1 || c > W) return 32;
        return (unsigned char)shadow[r - 1][c - 1];
    }

    unsigned char attrAt(int r, int c) override {
        if (r < 1 || r > H || c < 1 || c > W) return 7;
        return (unsigned char)attrSh[r - 1][c - 1];
    }

    // Map a curses key to a GW-BASIC key event.
    KeyEvent translate(int k) {
        KeyEvent e;
        if (k == ERR) return e;
        switch (k) {
            case KEY_UP:    e.ch = 0; e.scan = 72; return e;
            case KEY_DOWN:  e.ch = 0; e.scan = 80; return e;
            case KEY_LEFT:  e.ch = 0; e.scan = 75; return e;
            case KEY_RIGHT: e.ch = 0; e.scan = 77; return e;
            case KEY_HOME:  e.ch = 0; e.scan = 71; return e;
            case KEY_END:   e.ch = 0; e.scan = 79; return e;
            case KEY_PPAGE: e.ch = 0; e.scan = 73; return e;
            case KEY_NPAGE: e.ch = 0; e.scan = 81; return e;
            case KEY_IC:    e.ch = 0; e.scan = 82; return e;
            case KEY_DC:    e.ch = 0; e.scan = 83; return e;
            case KEY_BACKSPACE: e.ch = 8; return e;
            case KEY_ENTER: e.ch = 13; return e;
        }
        if (k >= KEY_F(1) && k <= KEY_F(10)) {
            e.ch = 0;
            e.scan = 59 + (k - KEY_F(1));
            return e;
        }
        if (k == '\n' || k == '\r') { e.ch = 13; return e; }
        if (k >= 0 && k < 256) { e.ch = k; return e; }
        return e;                                    // unknown special: none
    }

    KeyEvent pollKey() override {
        if (!pending.empty()) {
            KeyEvent e = pending.front();
            pending.pop_front();
            return e;
        }
        nodelay(stdscr, TRUE);
        int k = getch();
        nodelay(stdscr, FALSE);
        return translate(k);
    }

    KeyEvent waitKey() override {
        if (!pending.empty()) {
            KeyEvent e = pending.front();
            pending.pop_front();
            return e;
        }
        refresh();
        while (true) {
            int k = getch();
            KeyEvent e = translate(k);
            if (!e.none()) return e;
        }
    }

    // Non-blocking check used between statements: returns true on Ctrl-C,
    // queues anything else for INKEY$.
    bool breakPending() override {
        if (g_sigint) { g_sigint = 0; return true; }
        nodelay(stdscr, TRUE);
        int k = getch();
        nodelay(stdscr, FALSE);
        if (k == ERR) return false;
        if (k == 3) return true;
        KeyEvent e = translate(k);
        if (!e.none()) pending.push_back(e);
        return false;
    }

    void beepNow() override { beep(); }
    void setCursorVisible(bool v) override { curs_set(v ? 1 : 0); }
};

// ---------------------------------------------------------------------------
// PlainScreen: line-oriented stdio.  LOCATE/COLOR are tracked but invisible;
// CLS resets the virtual cursor.  Good for tests and piping.
// ---------------------------------------------------------------------------

struct PlainScreen : Screen {
    int r_ = 1, c_ = 1;

    int rows() override { return 25; }
    int cols() override { return widthLimit; }
    void cls() override { r_ = 1; c_ = 1; }
    void locate(int r, int c) override { r_ = std::max(1, r); c_ = std::max(1, c); }
    int row() override { return r_; }
    int col() override { return c_; }
    void setColor(int, int) override {}
    void newline() override {
        fputc('\n', stdout);
        c_ = 1;
        if (r_ < rows()) r_++;
    }
    void putByte(unsigned char b) override {
        unsigned short u = cp437[b];
        if (u < 128) fputc((char)u, stdout);
        else fputs(utf8Of(u).c_str(), stdout);
        c_++;
        if (c_ > widthLimit) { fputc('\n', stdout); c_ = 1; if (r_ < rows()) r_++; }
    }
    void flush() override { fflush(stdout); }
    unsigned char charAt(int, int) override { return 32; }

    KeyEvent pollKey() override {
        // INKEY$ from a pipe: deliver buffered bytes if any are ready.
        if (isatty(0)) return KeyEvent{};
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(0, &fds);
        struct timeval tv = {0, 0};
        if (select(1, &fds, nullptr, nullptr, &tv) > 0) {
            char b;
            if (read(0, &b, 1) == 1) {
                KeyEvent e;
                e.ch = (b == '\n') ? 13 : (unsigned char)b;   // Enter is CHR$(13)
                return e;
            }
        }
        return KeyEvent{};
    }
    KeyEvent waitKey() override {
        int c = fgetc(stdin);
        KeyEvent e;
        if (c != EOF) e.ch = (c == '\n') ? 13 : c;
        return e;
    }
    void beepNow() override { fputc('\a', stdout); }
};

// ---------------------------------------------------------------------------
// The interpreter.  The struct body spans several sections below; it holds
// program text, variables, the run-time stacks and every statement executor.
// ---------------------------------------------------------------------------

struct ProgLine {
    string text;                 // source after the line number
    std::vector<Token> toks;
    bool tokd = false;
};

struct Pc {                      // program counter: line + token index
    int line = -2;               // -1 = direct (immediate) buffer, -2 = none
    size_t ti = 0;
};

// Control-flow signals (not errors).
struct BreakSignal { int line; };
struct StopSignal { int line; };     // STOP (prints Break in n) or END (silent)
struct EndSignal {};

struct Basic {
    Screen& scr;
    bool interactive;            // true when sitting under the screen editor

    explicit Basic(Screen& s, bool inter) : scr(s), interactive(inter) {
        defType.fill('!');
        memset(mem, 0, sizeof mem);
        keyMacros[0] = "LIST ";
        keyMacros[1] = "RUN\r";
        keyMacros[2] = "LOAD\"";
        keyMacros[3] = "SAVE\"";
        keyMacros[4] = "CONT\r";
        keyMacros[6] = "TRON\r";
        keyMacros[7] = "TROFF\r";
        keyMacros[8] = "KEY ";
    }

    // ---- program storage ----------------------------------------------
    std::map<int, ProgLine> prog;
    int lastUsedLine = -1;       // '.' in LIST/EDIT/DELETE

    // ---- variables ------------------------------------------------------
    struct Array {
        std::vector<int> dims;   // extent per dimension (index range size)
        int base = 0;
        std::vector<Value> data;
    };
    std::unordered_map<string, Value> vars;
    std::unordered_map<string, Array> arrays;
    std::array<char, 26> defType;
    int optionBase = 0;
    bool optionBaseLocked = false;
    struct FnDef { std::vector<string> params; std::vector<Token> body; char ret = '!'; };
    std::unordered_map<string, FnDef> fns;
    std::set<string> commons;

    // ---- token stream under execution ------------------------------------
    const std::vector<Token>* T = nullptr;
    size_t ti = 0;
    int curLine = -1;            // line being executed, -1 = direct
    std::vector<Token> directToks;

    // ---- run-time stacks --------------------------------------------------
    // GW-BASIC keeps one unified stack: RETURN discards FOR/WHILE entries
    // pushed inside the subroutine, and NEXT's search stops at the GOSUB
    // entry.  We keep three stacks but record GOSUB high-water marks and
    // per-entry sequence numbers to reproduce those semantics.
    struct ForEntry { string var; double limit, step; Pc body; uint64_t seq = 0; };
    struct WhileEntry { Pc pos; uint64_t seq = 0; };
    struct GosubEntry { Pc ret; size_t forMark = 0, whileMark = 0; };
    std::vector<ForEntry> forStack;
    std::vector<WhileEntry> whileStack;
    std::vector<GosubEntry> gosubStack;
    uint64_t stackSeq = 0;
    string skipNextVarOnce;      // see FOR body-skip handling

    GosubEntry makeGosub(Pc ret) {
        GosubEntry e;
        e.ret = ret;
        e.forMark = forStack.size();
        e.whileMark = whileStack.size();
        return e;
    }
    size_t forFloor() const { return gosubStack.empty() ? 0 : gosubStack.back().forMark; }
    size_t whileFloor() const { return gosubStack.empty() ? 0 : gosubStack.back().whileMark; }

    // ---- ON TIMER(n) GOSUB trap --------------------------------------------
    int timerTrapLine = 0;
    double timerInterval = 0;
    bool timerOn = false;
    double timerNextFire = 0;
    bool inTimerTrap = false;
    size_t timerFireDepth = 0;

    static double nowSeconds() {
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        return ts.tv_sec + ts.tv_nsec / 1e9;
    }

    // ---- recursion guards ---------------------------------------------------
    int fnDepth = 0;             // nested DEF FN calls
    int evalDepth = 0;           // nested expression evaluations

    // ---- DATA pointer ----------------------------------------------------
    Pc dataPc{-2, 0};

    // ---- error handling / CONT --------------------------------------------
    int errHandlerLine = 0;
    bool inHandler = false;
    int errCode = 0, errLine = 0;
    Pc errPc;                    // statement that raised the error
    Pc contPc;
    bool contValid = false;
    int breakCounter = 0;

    // ---- misc machine state -----------------------------------------------
    bool tron = false;
    unsigned char mem[65536];
    uint32_t rndSeed = 0x004FC752 & 0xFFFFFF;
    double lastRnd = 0;
    static const int MAXFILES = 15;
    struct LRef {                // assignment target (scalar or array element)
        string key;
        bool isArr = false;
        std::vector<int> idx;
    };
    struct FieldDef { LRef ref; int off, len; };
    struct FileHandle {
        std::fstream f;
        char mode = 0;           // 'I','O','A','R'
        int reclen = 128;
        string path;
        std::vector<FieldDef> fields;
        string recbuf;
        long lastRec = 0;        // last record read/written (random mode)
        int col = 1;             // print column for PRINT# zones
    };
    std::array<std::unique_ptr<FileHandle>, MAXFILES + 1> files;
    FILE* printer = nullptr;
    int lpos = 1;

    // ---- editor interaction ------------------------------------------------
    bool autoOn = false;
    int autoNext = 10, autoInc = 10;
    int pendingEdit = -1;
    bool exitRequested = false;
    string keyMacros[10];

    // =======================================================================
    // Token stream helpers
    // =======================================================================
    static const Token& endTok() { static Token t; return t; }
    const Token& cur() const { return (T && ti < T->size()) ? (*T)[ti] : endTok(); }
    const Token& peek(size_t k) const {
        return (T && ti + k < T->size()) ? (*T)[ti + k] : endTok();
    }
    void adv() { ti++; }
    bool isPun(const char* p) const { return cur().t == Token::Pun && cur().s == p; }
    bool eatPun(const char* p) { if (isPun(p)) { adv(); return true; } return false; }
    void wantPun(const char* p) { if (!eatPun(p)) err(2); }
    bool isKw(Kw k) const { return cur().t == Token::Key && cur().kw == k; }
    bool eatKw(Kw k) { if (isKw(k)) { adv(); return true; } return false; }
    void wantKw(Kw k) { if (!eatKw(k)) err(2); }
    bool atStmtEnd() const {
        const Token& t = cur();
        return t.t == Token::End || t.t == Token::Rem ||
               (t.t == Token::Pun && t.s == ":") ||
               (t.t == Token::Key && t.kw == Kw::ELSE);
    }
    int wantLineNum() {
        if (cur().t != Token::Num) err(2);
        double v = cur().n;
        adv();
        if (v < 0 || v > 65529 || v != floor(v)) err(8);
        return (int)v;
    }

    // =======================================================================
    // Position handling
    // =======================================================================
    Pc here() const { Pc p; p.line = curLine; p.ti = ti; return p; }

    const std::vector<Token>& toksFor(int line) {
        if (line == -1) return directToks;
        auto it = prog.find(line);
        if (it == prog.end()) err(8);
        if (!it->second.tokd) {
            it->second.toks = tokenize(it->second.text);
            it->second.tokd = true;
        }
        return it->second.toks;
    }

    void go(Pc p) {
        curLine = p.line;
        T = &toksFor(p.line);
        ti = p.ti;
    }

    void goLine(int n) {
        if (!prog.count(n)) err(8);
        if (tron) { scr.write("[" + std::to_string(n) + "]"); }
        Pc p; p.line = n; p.ti = 0;
        go(p);
    }

    // Move to the next program line; false when the program (or the direct
    // buffer) is exhausted.
    bool advanceLine() {
        if (curLine == -1) return false;
        auto it = prog.upper_bound(curLine);
        if (it == prog.end()) return false;
        if (tron) { scr.write("[" + std::to_string(it->first) + "]"); }
        Pc p; p.line = it->first; p.ti = 0;
        go(p);
        return true;
    }

    // Position just past the statement that starts at p (for RESUME NEXT).
    Pc afterStatement(Pc p) {
        const std::vector<Token>& tv = toksFor(p.line);
        size_t i = p.ti;
        if (i < tv.size() && tv[i].t == Token::Key && tv[i].kw == Kw::IF) {
            Pc r; r.line = p.line; r.ti = tv.size();
            return r;
        }
        int depth = 0;
        while (i < tv.size()) {
            const Token& t = tv[i];
            if (t.t == Token::Pun && t.s == "(") depth++;
            else if (t.t == Token::Pun && t.s == ")") depth--;
            else if (depth == 0 && t.t == Token::Pun && t.s == ":") break;
            else if (depth == 0 && t.t == Token::Key && t.kw == Kw::ELSE) break;
            else if (t.t == Token::Rem) { i = tv.size(); break; }
            i++;
        }
        Pc r; r.line = p.line; r.ti = i;
        return r;
    }

    // =======================================================================
    // Variables
    // =======================================================================
    char defaultTypeFor(const string& name) const {
        char c = name[0];
        if (c >= 'A' && c <= 'Z') return defType[c - 'A'];
        return '!';
    }

    // Normalize an identifier (already uppercased by the lexer) to a
    // storage key that always carries its type suffix.
    string varKey(const string& name) const {
        char last = name.back();
        if (last == '$' || last == '%' || last == '!' || last == '#') return name;
        return name + defaultTypeFor(name);
    }

    static char typeOfKey(const string& key) { return key.back(); }

    Value defaultValue(char ty) const {
        if (ty == '$') return Value::str("");
        return Value::num(0, ty);
    }

    Value getVar(const string& key) {
        auto it = vars.find(key);
        if (it != vars.end()) return it->second;
        return defaultValue(typeOfKey(key));
    }

    Value coerceTo(char ty, const Value& v) {
        if (ty == '$') {
            if (!v.isStr()) err(13);
            if (v.s.size() > 255) err(15);
            return v;
        }
        if (v.isStr()) err(13);
        return Value::num(coerceNum(v.n, ty), ty);
    }

    void setVar(const string& key, const Value& v) {
        vars[key] = coerceTo(typeOfKey(key), v);
        onVarAssigned(key);
    }

    Array& makeArray(const string& key, const std::vector<int>& extents) {
        if (arrays.count(key)) err(10);
        Array a;
        a.base = optionBase;
        a.dims = extents;
        size_t total = 1;
        for (int e : extents) {
            if (e < 1) err(9);
            total *= (size_t)e;
            if (total > 4u * 1024 * 1024) err(7);
        }
        a.data.assign(total, defaultValue(typeOfKey(key)));
        optionBaseLocked = true;
        auto res = arrays.emplace(key, std::move(a));
        return res.first->second;
    }

    Value& arrayElem(const string& key, const std::vector<int>& idx) {
        auto it = arrays.find(key);
        if (it == arrays.end()) {
            std::vector<int> ext(idx.size(), 11 - optionBase);
            makeArray(key, ext);
            it = arrays.find(key);
        }
        Array& a = it->second;
        if (idx.size() != a.dims.size()) err(9);
        size_t off = 0;
        for (size_t d = 0; d < idx.size(); d++) {
            int i = idx[d] - a.base;
            if (i < 0 || i >= a.dims[d]) err(9);
            off = off * a.dims[d] + i;
        }
        return a.data[off];
    }

    // =======================================================================
    // Program line management (used by the editor and LOAD/MERGE)
    // =======================================================================
    void invalidateCont() { contValid = false; }

    void storeLine(int n, const string& text) {
        if (n < 0 || n > 65529) err(8);
        invalidateCont();
        lastUsedLine = n;
        if (trim(text).empty()) {
            if (!prog.count(n)) err(8);
            prog.erase(n);
            return;
        }
        ProgLine pl;
        pl.text = text;
        prog[n] = std::move(pl);
    }

    void clearVariables() {
        vars.clear();
        arrays.clear();
        fns.clear();
        forStack.clear();
        whileStack.clear();
        gosubStack.clear();
        dataPc = Pc{-2, 0};
        optionBaseLocked = false;
        inHandler = false;
        errHandlerLine = 0;
        timerTrapLine = 0;
        timerOn = false;
        inTimerTrap = false;
        fnDepth = 0;
        evalDepth = 0;
    }

    void closeAllFiles() {
        for (int i = 1; i <= MAXFILES; i++)
            if (files[i]) closeFile(i);
        if (printer) { fclose(printer); printer = nullptr; lpos = 1; }
    }

    void doNew() {
        prog.clear();
        clearVariables();
        closeAllFiles();
        defType.fill('!');
        commons.clear();
        invalidateCont();
        errCode = errLine = 0;
        autoOn = false;
        lastUsedLine = -1;
    }

    // =======================================================================
    // RND — GW-BASIC's 24-bit linear congruential generator
    // =======================================================================
    double rndNext() {
        rndSeed = (rndSeed * 214013u + 2531011u) & 0xFFFFFF;
        lastRnd = (double)rndSeed / 16777216.0;
        return lastRnd;
    }
    double rndFunc(bool hasArg, double a) {
        if (!hasArg || a > 0) return rndNext();
        if (a == 0) return lastRnd;
        // negative: deterministic reseed from the argument
        union { float f; uint32_t u; } cv;
        cv.f = (float)a;
        rndSeed = (cv.u ^ (cv.u >> 8)) & 0xFFFFFF;
        return rndNext();
    }
    void randomizeSeed(double v) {
        int32_t n = (int32_t)llrint(fmod(v, 65536.0));
        rndSeed = (rndSeed & 0xFF) | (((uint32_t)(uint16_t)n) << 8);
    }

    // =======================================================================
    // Break / output helpers
    // =======================================================================
    void checkBreak() {
        if (++breakCounter >= 8) {
            breakCounter = 0;
            if (scr.breakPending()) throw BreakSignal{curLine};
        }
        // Fire a pending ON TIMER trap between statements (program mode only).
        if (timerOn && timerTrapLine && !inTimerTrap && curLine != -1 &&
            nowSeconds() >= timerNextFire && prog.count(timerTrapLine)) {
            inTimerTrap = true;
            timerFireDepth = gosubStack.size();
            gosubStack.push_back(makeGosub(here()));
            goLine(timerTrapLine);
        }
    }

    void out(const string& s) { scr.write(s); }
    void outNl() { scr.write("\n"); }
    void outLine(const string& s) { scr.write(s); outNl(); }

    // GW-BASIC's "machine infinity" (largest MBF float). Division by zero
    // and arithmetic overflow print a warning, yield this, and continue.
    static constexpr double MACHINE_INF = 1.7014118346046923e38;

    void arithWarn(const char* msg) {
        if (scr.col() != 1) outNl();
        outLine(msg);
        scr.flush();
    }

    // Wrap an arithmetic result: int overflow promotes to single, float
    // overflow warns and clamps to machine infinity.
    Value arithResult(double res, char rt) {
        if (rt == '%') {
            if (res >= -32768 && res <= 32767) return Value::num(rint(res), '%');
            rt = '!';
        }
        if (std::isnan(res)) err(5);
        if (std::isinf(res) || fabs(res) > MACHINE_INF) {
            arithWarn("Overflow");
            res = res < 0 ? -MACHINE_INF : MACHINE_INF;
        }
        if (rt == '!') res = (double)(float)res;
        return Value::num(res, rt);
    }

    // Make sure we are at column 1 (GW prints messages on a fresh line).
    void freshLine() { if (scr.col() != 1) outNl(); }

    // =======================================================================
    // L-value references (assignment targets) — LRef is declared above with
    // the file structures, since FIELD bindings hold one.
    // =======================================================================
    LRef parseVarRef() {
        if (cur().t != Token::Id) err(2);
        string name = cur().s;
        adv();
        LRef r;
        r.key = varKey(name);
        if (eatPun("(")) {
            r.isArr = true;
            do {
                r.idx.push_back(evalInt16());
            } while (eatPun(","));
            wantPun(")");
        }
        return r;
    }

    void assignRef(const LRef& r, const Value& v) {
        if (r.isArr) {
            Value& slot = arrayElem(r.key, r.idx);
            slot = coerceTo(typeOfKey(r.key), v);
        } else {
            setVar(r.key, v);
        }
    }

    Value readRef(const LRef& r) {
        if (r.isArr) {
            LRef rr = r;
            return arrayElem(rr.key, rr.idx);
        }
        return getVar(r.key);
    }

    void onVarAssigned(const string&) {}

    // =======================================================================
    // Expression evaluator (operator precedence per GW-BASIC)
    // =======================================================================
    Value mkNum(double d, char ty) { return Value::num(coerceNum(d, ty), ty); }

    static int16_t bit16(const Value& v) {
        if (v.isStr()) err(13);
        if (!(v.n >= -32768.5 && v.n < 32767.5)) err(6);
        return (int16_t)lrint(v.n);
    }

    Value evalExpr() {
        if (evalDepth > 300) err(7);    // deeply nested parens: out of memory
        evalDepth++;
        Value v;
        try {
            v = evalImp();
        } catch (...) {
            evalDepth--;
            throw;
        }
        evalDepth--;
        return v;
    }

    Value evalImp() {
        Value l = evalEqv();
        while (isKw(Kw::IMP)) {
            adv();
            Value r = evalEqv();
            l = Value::num((int16_t)(~bit16(l) | bit16(r)), '%');
        }
        return l;
    }
    Value evalEqv() {
        Value l = evalXor();
        while (isKw(Kw::EQV)) {
            adv();
            Value r = evalXor();
            l = Value::num((int16_t)~(bit16(l) ^ bit16(r)), '%');
        }
        return l;
    }
    Value evalXor() {
        Value l = evalOr();
        while (isKw(Kw::XOR)) {
            adv();
            Value r = evalOr();
            l = Value::num((int16_t)(bit16(l) ^ bit16(r)), '%');
        }
        return l;
    }
    Value evalOr() {
        Value l = evalAnd();
        while (isKw(Kw::OR)) {
            adv();
            Value r = evalAnd();
            l = Value::num((int16_t)(bit16(l) | bit16(r)), '%');
        }
        return l;
    }
    Value evalAnd() {
        Value l = evalNot();
        while (isKw(Kw::AND)) {
            adv();
            Value r = evalNot();
            l = Value::num((int16_t)(bit16(l) & bit16(r)), '%');
        }
        return l;
    }
    Value evalNot() {
        if (eatKw(Kw::NOT)) {
            Value v = evalNot();
            return Value::num((int16_t)~bit16(v), '%');
        }
        return evalRel();
    }
    Value evalRel() {
        Value l = evalAdd();
        while (cur().t == Token::Pun &&
               (cur().s == "=" || cur().s == "<" || cur().s == ">" ||
                cur().s == "<=" || cur().s == ">=" || cur().s == "<>")) {
            string op = cur().s;
            adv();
            Value r = evalAdd();
            if (l.isStr() != r.isStr()) err(13);
            int c;
            if (l.isStr()) c = l.s.compare(r.s) < 0 ? -1 : (l.s == r.s ? 0 : 1);
            else c = l.n < r.n ? -1 : (l.n == r.n ? 0 : 1);
            bool res = (op == "=")  ? c == 0
                     : (op == "<")  ? c < 0
                     : (op == ">")  ? c > 0
                     : (op == "<=") ? c <= 0
                     : (op == ">=") ? c >= 0
                                    : c != 0;
            l = Value::num(res ? -1 : 0, '%');
        }
        return l;
    }
    Value evalAdd() {
        Value l = evalMod();
        while (cur().t == Token::Pun && (cur().s == "+" || cur().s == "-")) {
            bool plus = cur().s == "+";
            adv();
            Value r = evalMod();
            if (plus && l.isStr() && r.isStr()) {
                if (l.s.size() + r.s.size() > 255) err(15);
                l = Value::str(l.s + r.s);
                continue;
            }
            if (l.isStr() || r.isStr()) err(13);
            char rt = promote(l.t, r.t);
            l = arithResult(plus ? l.n + r.n : l.n - r.n, rt);
        }
        return l;
    }
    Value evalMod() {
        Value l = evalIdiv();
        while (isKw(Kw::MOD)) {
            adv();
            Value r = evalIdiv();
            int a = bit16(l), b = bit16(r);
            if (b == 0) {
                arithWarn("Division by zero");
                l = Value::num(a < 0 ? -32767 : 32767, '%');
                continue;
            }
            l = Value::num((int16_t)(a % b), '%');
        }
        return l;
    }
    Value evalIdiv() {
        Value l = evalMul();
        while (isPun("\\")) {
            adv();
            Value r = evalMul();
            int a = bit16(l), b = bit16(r);
            if (b == 0) {
                arithWarn("Division by zero");
                l = Value::num(a < 0 ? -32767 : 32767, '%');
                continue;
            }
            if (a == -32768 && b == -1) err(6);
            l = Value::num((int16_t)(a / b), '%');
        }
        return l;
    }
    Value evalMul() {
        Value l = evalUnary();
        while (cur().t == Token::Pun && (cur().s == "*" || cur().s == "/")) {
            bool mul = cur().s == "*";
            adv();
            Value r = evalUnary();
            if (l.isStr() || r.isStr()) err(13);
            char rt = promote(l.t, r.t);
            if (mul) {
                l = arithResult(l.n * r.n, rt);
            } else {
                if (rt == '%') rt = '!';
                if (r.n == 0) {
                    arithWarn("Division by zero");
                    l = Value::num(l.n < 0 ? -MACHINE_INF : MACHINE_INF, rt);
                    if (rt == '!') l.n = (double)(float)l.n;
                } else {
                    l = arithResult(l.n / r.n, rt);
                }
            }
        }
        return l;
    }
    Value evalUnary() {
        if (isPun("-")) {
            adv();
            Value v = evalUnary();
            if (v.isStr()) err(13);
            char rt = v.t;
            if (rt == '%' && -v.n > 32767) rt = '!';
            return Value::num(-v.n, rt);
        }
        if (isPun("+")) {
            adv();
            return evalUnary();
        }
        return evalPow();
    }
    Value evalPow() {
        Value l = evalAtom();
        while (isPun("^")) {
            adv();
            // The exponent operand is a (possibly signed) atom so that the
            // chain stays left-associative: 2^3^2 = 64, yet 2^-3 works.
            int sign = 1;
            while (isPun("-") || isPun("+")) {
                if (isPun("-")) sign = -sign;
                adv();
            }
            Value r = evalAtom();
            if (l.isStr() || r.isStr()) err(13);
            double e = sign * r.n;
            char rt = (l.t == '#' || r.t == '#') ? '#' : '!';
            if (l.n == 0 && e < 0) {
                arithWarn("Division by zero");
                l = Value::num(rt == '!' ? (double)(float)MACHINE_INF : MACHINE_INF, rt);
                continue;
            }
            if (l.n < 0 && e != floor(e)) err(5);
            l = arithResult(pow(l.n, e), rt);
        }
        return l;
    }

    // ---- typed convenience evaluators ----
    double evalNum() { return toNum(evalExpr()); }
    string evalStr() { return toStr(evalExpr()); }
    int evalInt16() { return toInt16(evalExpr()); }
    int evalIntRange(int lo, int hi) {
        double v = evalNum();
        if (!(v >= lo - 0.5 && v < hi + 0.5)) err(5);
        int n = (int)lrint(v);
        if (n < lo || n > hi) err(5);
        return n;
    }

    // Single numeric argument in parentheses.
    double argNum() { wantPun("("); double v = evalNum(); wantPun(")"); return v; }
    string argStr() { wantPun("("); string s = evalStr(); wantPun(")"); return s; }

    char numFuncType(char argType) { return argType == '#' ? '#' : '!'; }

    // =======================================================================
    // Atoms: literals, variables, functions
    // =======================================================================
    Value evalAtom() {
        const Token& t = cur();
        if (t.t == Token::Num) {
            Value v = Value::num(t.n, t.nt);
            adv();
            return v;
        }
        if (t.t == Token::Str) {
            Value v = Value::str(t.s);
            adv();
            return v;
        }
        if (t.t == Token::Pun && t.s == "(") {
            adv();
            Value v = evalExpr();
            wantPun(")");
            return v;
        }
        if (t.t == Token::Id) {
            LRef r = parseVarRef();
            return readRef(r);
        }
        if (t.t == Token::Key) return evalFunction(t.kw);
        err(2);
    }

    Value evalFunction(Kw k) {
        switch (k) {
            case Kw::ABS: { adv(); wantPun("("); Value v = evalExpr(); wantPun(")");
                if (v.isStr()) err(13);
                return mkNum(fabs(v.n), v.t); }
            case Kw::ASC: { adv(); string s = argStr();
                if (s.empty()) err(5);
                return Value::num((unsigned char)s[0], '%'); }
            case Kw::ATN: { adv(); wantPun("("); Value v = evalExpr(); wantPun(")");
                return mkNum(atan(toNum(v)), numFuncType(v.t)); }
            case Kw::CDBL: { adv(); return mkNum(argNum(), '#'); }
            case Kw::CHRS: { adv(); wantPun("(");
                int n = evalIntRange(0, 255); wantPun(")");
                return Value::str(string(1, (char)n)); }
            case Kw::CINT: { adv(); return mkNum(argNum(), '%'); }
            case Kw::COS: { adv(); wantPun("("); Value v = evalExpr(); wantPun(")");
                return mkNum(cos(toNum(v)), numFuncType(v.t)); }
            case Kw::CSNG: { adv(); return mkNum(argNum(), '!'); }
            case Kw::CSRLIN: { adv(); return Value::num(scr.row(), '%'); }
            case Kw::CVI: { adv(); string s = argStr();
                if (s.size() < 2) err(5);
                int16_t v;
                memcpy(&v, s.data(), 2);
                return Value::num(v, '%'); }
            case Kw::CVS: { adv(); string s = argStr();
                if (s.size() < 4) err(5);
                float v;
                memcpy(&v, s.data(), 4);
                return Value::num((double)v, '!'); }
            case Kw::CVD: { adv(); string s = argStr();
                if (s.size() < 8) err(5);
                double v;
                memcpy(&v, s.data(), 8);
                return Value::num(v, '#'); }
            case Kw::DATES: { adv();
                time_t now = time(nullptr);
                struct tm tmv;
                localtime_r(&now, &tmv);
                char b[40];
                snprintf(b, sizeof b, "%02d-%02d-%04d",
                         tmv.tm_mon + 1, tmv.tm_mday, tmv.tm_year + 1900);
                return Value::str(b); }
            case Kw::ENVIRONS: { adv(); wantPun("(");
                Value a = evalExpr();
                wantPun(")");
                if (a.isStr()) {
                    const char* e = getenv(a.s.c_str());
                    return Value::str(e ? e : "");
                }
                err(5); }
            case Kw::EOFF: { adv(); wantPun("(");
                int n = evalFileNum();
                wantPun(")");
                return Value::num(fileEof(n) ? -1 : 0, '%'); }
            case Kw::ERDEV: err(73);
            case Kw::ERL: { adv(); return Value::num(errLine < 0 ? 65535 : errLine, '!'); }
            case Kw::ERRF: { adv(); return Value::num(errCode, '%'); }
            case Kw::EXP: { adv(); wantPun("("); Value v = evalExpr(); wantPun(")");
                return arithResult(exp(toNum(v)), numFuncType(v.t)); }
            case Kw::FIX: { adv(); wantPun("("); Value v = evalExpr(); wantPun(")");
                if (v.isStr()) err(13);
                return mkNum(trunc(v.n), v.t == '%' ? '%' : v.t); }
            case Kw::FRE: { adv(); wantPun("("); evalExpr(); wantPun(")");
                return Value::num(60300, '!'); }
            case Kw::HEXS: { adv(); wantPun("(");
                double d = evalNum();
                wantPun(")");
                if (!(d >= -32768.5 && d < 65535.5)) err(6);
                long v = lrint(d) & 0xFFFF;
                char b[8];
                snprintf(b, sizeof b, "%lX", v);
                return Value::str(b); }
            case Kw::OCTS: { adv(); wantPun("(");
                double d = evalNum();
                wantPun(")");
                if (!(d >= -32768.5 && d < 65535.5)) err(6);
                long v = lrint(d) & 0xFFFF;
                char b[8];
                snprintf(b, sizeof b, "%lo", v);
                return Value::str(b); }
            case Kw::INKEYS: { adv(); return Value::str(inkey()); }
            case Kw::INP: { adv(); argNum(); return Value::num(0, '%'); }
            case Kw::INPUTS: { adv(); return inputDollar(); }
            case Kw::INSTR: { adv(); return instrFunc(); }
            case Kw::INTF: { adv(); wantPun("("); Value v = evalExpr(); wantPun(")");
                if (v.isStr()) err(13);
                return mkNum(floor(v.n), v.t == '%' ? '%' : v.t); }
            case Kw::IOCTLS: err(73);
            case Kw::LEFTS: { adv(); wantPun("(");
                string s = evalStr();
                wantPun(",");
                int n = evalIntRange(0, 255);
                wantPun(")");
                return Value::str(s.substr(0, std::min((size_t)n, s.size()))); }
            case Kw::LEN: { adv(); return Value::num((double)argStr().size(), '%'); }
            case Kw::LOC: { adv(); wantPun("(");
                int n = evalFileNum();
                wantPun(")");
                return Value::num((double)fileLoc(n), '!'); }
            case Kw::LOF: { adv(); wantPun("(");
                int n = evalFileNum();
                wantPun(")");
                return Value::num((double)fileLof(n), '!'); }
            case Kw::LOG: { adv(); wantPun("("); Value v = evalExpr(); wantPun(")");
                double x = toNum(v);
                if (x <= 0) err(5);
                return mkNum(log(x), numFuncType(v.t)); }
            case Kw::LPOS: { adv(); wantPun("("); evalExpr(); wantPun(")");
                return Value::num(lpos, '%'); }
            case Kw::MIDS: { adv(); wantPun("(");
                string s = evalStr();
                wantPun(",");
                int n = evalIntRange(1, 255);
                int m = 255;
                if (eatPun(",")) m = evalIntRange(0, 255);
                wantPun(")");
                if ((size_t)n > s.size()) return Value::str("");
                return Value::str(s.substr(n - 1, m)); }
            case Kw::MKIS: { adv(); wantPun("(");
                int v = evalInt16();
                wantPun(")");
                int16_t x = (int16_t)v;
                return Value::str(string((char*)&x, 2)); }
            case Kw::MKSS: { adv(); wantPun("(");
                float x = (float)coerceNum(evalNum(), '!');
                wantPun(")");
                return Value::str(string((char*)&x, 4)); }
            case Kw::MKDS: { adv(); wantPun("(");
                double x = evalNum();
                wantPun(")");
                return Value::str(string((char*)&x, 8)); }
            case Kw::PEEK: { adv(); wantPun("(");
                double d = evalNum();
                wantPun(")");
                if (!(d >= -32768.5 && d < 65535.5)) err(5);
                return Value::num(mem[lrint(d) & 0xFFFF], '%'); }
            case Kw::PMAP: case Kw::POINT: err(73);
            case Kw::POS: { adv(); wantPun("("); evalExpr(); wantPun(")");
                return Value::num(scr.col(), '%'); }
            case Kw::RIGHTS: { adv(); wantPun("(");
                string s = evalStr();
                wantPun(",");
                int n = evalIntRange(0, 255);
                wantPun(")");
                size_t k = std::min((size_t)n, s.size());
                return Value::str(s.substr(s.size() - k)); }
            case Kw::RND: { adv();
                bool hasArg = false;
                double a = 0;
                if (eatPun("(")) {
                    a = evalNum();
                    wantPun(")");
                    hasArg = true;
                }
                return Value::num(rndFunc(hasArg, a), '!'); }
            case Kw::SCREEN: { adv(); wantPun("(");
                int r = evalIntRange(1, 255);
                wantPun(",");
                int c = evalIntRange(1, 255);
                int z = 0;
                if (eatPun(",")) z = evalInt16();
                wantPun(")");
                if (z != 0) return Value::num(scr.attrAt(r, c), '%');
                return Value::num(scr.charAt(r, c), '%'); }
            case Kw::SGN: { adv(); double v = argNum();
                return Value::num(v > 0 ? 1 : (v < 0 ? -1 : 0), '%'); }
            case Kw::SIN: { adv(); wantPun("("); Value v = evalExpr(); wantPun(")");
                return mkNum(sin(toNum(v)), numFuncType(v.t)); }
            case Kw::SPACES: { adv(); wantPun("(");
                int n = evalIntRange(0, 255);
                wantPun(")");
                return Value::str(string(n, ' ')); }
            case Kw::SQR: { adv(); wantPun("("); Value v = evalExpr(); wantPun(")");
                double x = toNum(v);
                if (x < 0) err(5);
                return mkNum(sqrt(x), numFuncType(v.t)); }
            case Kw::STICK: { adv(); argNum(); return Value::num(0, '%'); }
            case Kw::STRS: { adv(); wantPun("("); Value v = evalExpr(); wantPun(")");
                if (v.isStr()) err(13);
                return Value::str(fmtNumSigned(v)); }
            case Kw::STRINGS: { adv(); wantPun("(");
                int n = evalIntRange(0, 255);
                wantPun(",");
                Value a = evalExpr();
                wantPun(")");
                char c;
                if (a.isStr()) {
                    if (a.s.empty()) err(5);
                    c = a.s[0];
                } else {
                    double d = a.n;
                    if (!(d >= -0.5 && d < 255.5)) err(5);
                    c = (char)lrint(d);
                }
                return Value::str(string(n, c)); }
            case Kw::TAN: { adv(); wantPun("("); Value v = evalExpr(); wantPun(")");
                return mkNum(tan(toNum(v)), numFuncType(v.t)); }
            case Kw::TIMES: { adv();
                time_t now = time(nullptr);
                struct tm tmv;
                localtime_r(&now, &tmv);
                char b[12];
                snprintf(b, sizeof b, "%02d:%02d:%02d",
                         tmv.tm_hour, tmv.tm_min, tmv.tm_sec);
                return Value::str(b); }
            case Kw::TIMER: { adv();
                struct timespec ts;
                clock_gettime(CLOCK_REALTIME, &ts);
                time_t now = ts.tv_sec;
                struct tm tmv;
                localtime_r(&now, &tmv);
                double sec = tmv.tm_hour * 3600.0 + tmv.tm_min * 60.0 +
                             tmv.tm_sec + ts.tv_nsec / 1e9;
                return Value::num((double)(float)sec, '!'); }
            case Kw::USR: err(73);
            case Kw::VAL: { adv(); return valOf(argStr()); }
            case Kw::VARPTR: { adv(); wantPun("(");
                long a = 0;
                if (isPun("#")) {
                    adv();
                    a = 0x600 + evalFileNum() * 0x100;
                } else {
                    LRef r = parseVarRef();
                    a = (long)(std::hash<string>{}(r.key) & 0x3FFF) + 0x1000;
                    (void)r;
                }
                wantPun(")");
                return Value::num((double)a, '!'); }
            case Kw::VARPTRS: err(73);
            case Kw::FN: { adv(); return callFn(); }
            case Kw::TAB: case Kw::SPC: err(5);   // only valid inside PRINT
            default: err(2);
        }
    }

    string inkey() {
        KeyEvent e = scr.pollKey();
        if (e.none()) return "";
        if (e.ch == 3) throw BreakSignal{curLine};   // Ctrl-C always breaks
        if (e.ch == 0) {
            string s;
            s += '\0';
            s += (char)e.scan;
            return s;
        }
        return string(1, (char)e.ch);
    }

    Value inputDollar() {
        wantPun("(");
        int n = evalIntRange(1, 255);
        if (eatPun(",")) {
            eatPun("#");
            int fn = evalFileNum();
            wantPun(")");
            FileHandle& fh = fileFor(fn, "IR");
            string s;
            for (int i = 0; i < n; i++) {
                int c = fh.f.get();
                if (c == EOF) err(62);
                s += (char)c;
            }
            return Value::str(s);
        }
        wantPun(")");
        string s;
        scr.flush();
        for (int i = 0; i < n; i++) {
            KeyEvent e = scr.waitKey();
            if (e.none()) err(62);
            if (e.ch == 3) throw BreakSignal{curLine};
            if (e.ch > 0) s += (char)e.ch;
            else i--;                        // ignore extended keys
        }
        return Value::str(s);
    }

    // VAL: GW strips blanks/tabs anywhere in the string and keeps the
    // natural precision of the literal (so long numbers stay double).
    Value valOf(const string& s) {
        string t;
        for (char c : s)
            if (c != ' ' && c != '\t' && c != '\n' && c != '\r') t += c;
        size_t i = 0;
        bool neg = false;
        if (i < t.size() && (t[i] == '+' || t[i] == '-')) {
            neg = (t[i] == '-');
            i++;
        }
        double v = 0;
        char ty = '!';
        size_t n = lexNumber(t, i, v, ty);
        if (n == 0) return Value::num(0, '!');
        return Value::num(neg ? -v : v, ty);
    }

    Value instrFunc() {
        wantPun("(");
        Value first = evalExpr();
        int start = 1;
        string x, y;
        if (!first.isStr()) {
            start = (int)lrint(first.n);
            if (start < 1 || start > 255) err(5);
            wantPun(",");
            x = evalStr();
        } else {
            x = first.s;
        }
        wantPun(",");
        y = evalStr();
        wantPun(")");
        if ((size_t)start > x.size()) return Value::num(0, '%');
        if (y.empty()) return Value::num(start, '%');
        size_t p = x.find(y, start - 1);
        return Value::num(p == string::npos ? 0 : (double)(p + 1), '%');
    }

    Value callFn() {
        if (cur().t != Token::Id) err(2);
        string key = varKey(cur().s);
        adv();
        auto it = fns.find(key);
        if (it == fns.end()) err(18);
        if (fnDepth >= 64) err(7);      // runaway FN recursion
        FnDef& fn = it->second;
        std::vector<Value> args;
        if (!fn.params.empty()) {
            wantPun("(");
            for (size_t i = 0; i < fn.params.size(); i++) {
                if (i) wantPun(",");
                args.push_back(coerceTo(typeOfKey(fn.params[i]), evalExpr()));
            }
            wantPun(")");
        }
        // Save shadowed parameter values, bind, evaluate, restore.
        std::vector<std::pair<string, Value>> saved;
        std::vector<string> absent;
        for (size_t i = 0; i < fn.params.size(); i++) {
            auto vit = vars.find(fn.params[i]);
            if (vit != vars.end()) saved.push_back({fn.params[i], vit->second});
            else absent.push_back(fn.params[i]);
            vars[fn.params[i]] = args[i];
        }
        const std::vector<Token>* savedT = T;
        size_t savedTi = ti;
        T = &fn.body;
        ti = 0;
        Value result;
        fnDepth++;
        try {
            result = evalExpr();
            if (ti < fn.body.size()) err(2);
        } catch (...) {
            fnDepth--;
            T = savedT;
            ti = savedTi;
            for (auto& p : saved) vars[p.first] = p.second;
            for (auto& a : absent) vars.erase(a);
            throw;
        }
        fnDepth--;
        T = savedT;
        ti = savedTi;
        for (auto& p : saved) vars[p.first] = p.second;
        for (auto& a : absent) vars.erase(a);
        return coerceTo(fn.ret, result);
    }

    // =======================================================================
    // Statement dispatch
    // =======================================================================
    Pc stmtBegin;

    void statement() {
        evalDepth = 0;
        checkBreak();
        stmtBegin = here();
        const Token& t = cur();
        if (t.t == Token::Id) { stmtLet(); return; }
        if (t.t == Token::Rem) { ti = T->size(); return; }
        if (t.t != Token::Key) err(2);
        Kw k = t.kw;
        adv();
        switch (k) {
            case Kw::LET: stmtLet(); break;
            case Kw::MIDS: stmtMidAssign(); break;
            case Kw::PRINT: stmtPrint(false); break;
            case Kw::LPRINT: stmtPrint(true); break;
            case Kw::WRITE: stmtWrite(); break;
            case Kw::INPUT: stmtInput(); break;
            case Kw::LINE: stmtLine(); break;
            case Kw::IF: stmtIf(); break;
            case Kw::FOR: stmtFor(); break;
            case Kw::NEXT: stmtNext(); break;
            case Kw::WHILE: stmtWhile(); break;
            case Kw::WEND: stmtWend(); break;
            case Kw::GOTO: goLine(wantLineNum()); break;
            case Kw::GOSUB: {
                int n = wantLineNum();
                gosubStack.push_back(makeGosub(here()));
                if (gosubStack.size() > 5000) err(7);
                goLine(n);
                break;
            }
            case Kw::RETURN: stmtReturn(); break;
            case Kw::ON: stmtOn(); break;
            case Kw::END:
                closeAllFiles();
                contPc = afterStatement(stmtBegin);
                contValid = (curLine != -1);
                throw EndSignal{};
            case Kw::STOP:
                contPc = afterStatement(stmtBegin);
                contValid = (curLine != -1);
                throw StopSignal{curLine};
            case Kw::REM: ti = T->size(); break;
            case Kw::DATA:
                while (cur().t == Token::Dat || isPun(",")) adv();
                break;
            case Kw::READ: stmtRead(); break;
            case Kw::RESTORE: stmtRestore(); break;
            case Kw::DIM: stmtDim(); break;
            case Kw::ERASE: stmtErase(); break;
            case Kw::OPTION:
                wantKw(Kw::BASE);
                // only the literal digits 0 or 1 are accepted
                if (cur().t != Token::Num || (cur().n != 0 && cur().n != 1)) err(2);
                if (optionBaseLocked) err(10);
                optionBase = (int)cur().n;
                adv();
                optionBaseLocked = true;
                break;
            case Kw::DEF: stmtDef(); break;
            case Kw::DEFINT: stmtDefType('%'); break;
            case Kw::DEFSNG: stmtDefType('!'); break;
            case Kw::DEFDBL: stmtDefType('#'); break;
            case Kw::DEFSTR: stmtDefType('$'); break;
            case Kw::SWAP: stmtSwap(); break;
            case Kw::RANDOMIZE: stmtRandomize(); break;
            case Kw::CLS:
                if (!atStmtEnd()) evalIntRange(0, 2);   // CLS 0/1/2 all clear
                scr.cls();
                break;
            case Kw::VIEW: {
                wantKw(Kw::PRINT);
                if (atStmtEnd()) {
                    scr.setViewPrint(0, 0);
                    break;
                }
                int t = evalIntRange(1, scr.rows());
                wantKw(Kw::TO);
                int b = evalIntRange(1, scr.rows());
                if (t >= b) err(5);
                scr.setViewPrint(t, b);
                break;
            }
            case Kw::LOCATE: stmtLocate(); break;
            case Kw::COLOR: stmtColor(); break;
            case Kw::WIDTH: stmtWidth(); break;
            case Kw::BEEP: scr.beepNow(); break;
            case Kw::SOUND: {
                double f = evalNum();
                wantPun(",");
                double d = evalNum();
                if (f < 37 || f > 32767) err(5);
                if (d > 0) scr.beepNow();
                break;
            }
            case Kw::PLAY: evalStr(); scr.beepNow(); break;
            case Kw::KEY: stmtKey(); break;
            case Kw::POKE: {
                double a = evalNum();
                wantPun(",");
                int v = evalIntRange(0, 255);
                if (!(a >= -32768.5 && a < 65535.5)) err(5);
                mem[lrint(a) & 0xFFFF] = (unsigned char)v;
                break;
            }
            case Kw::OUT: evalNum(); wantPun(","); evalNum(); break;
            case Kw::TIMER:
                if (eatKw(Kw::ON)) {
                    timerOn = true;
                    timerNextFire = nowSeconds() + timerInterval;
                } else if (eatKw(Kw::OFF) || eatKw(Kw::STOP)) {
                    timerOn = false;
                } else {
                    err(2);
                }
                break;
            case Kw::ERROR: {
                int n = evalIntRange(1, 255);
                err(n);
            }
            case Kw::RESUME: stmtResume(); break;
            case Kw::TRON: tron = true; break;
            case Kw::TROFF: tron = false; break;
            case Kw::CLEAR: stmtClear(); break;
            // file statements (defined further down)
            case Kw::OPEN: stmtOpen(); break;
            case Kw::CLOSE: stmtClose(); break;
            case Kw::FIELD: stmtField(); break;
            case Kw::GET: stmtGetPut(false); break;
            case Kw::PUT: stmtGetPut(true); break;
            case Kw::LSET: stmtLRSet(true); break;
            case Kw::RSET: stmtLRSet(false); break;
            case Kw::KILL: stmtKill(); break;
            case Kw::NAME: stmtName(); break;
            case Kw::FILES: stmtFiles(); break;
            case Kw::RESET: closeAllFiles(); break;
            // program management (also usable inside programs, like GW)
            case Kw::LIST: stmtList(false); break;
            case Kw::LLIST: stmtList(true); break;
            case Kw::NEW: doNew(); throw EndSignal{};
            case Kw::RUN: stmtRun(); break;
            case Kw::CONT: stmtCont(); break;
            case Kw::LOAD: stmtLoad(); break;
            case Kw::SAVE: stmtSave(); break;
            case Kw::MERGE: stmtMerge(); break;
            case Kw::CHAIN: stmtChain(); break;
            case Kw::COMMON: stmtCommon(); break;
            case Kw::DELETE: stmtDelete(); break;
            case Kw::RENUM: stmtRenum(); break;
            case Kw::AUTO: stmtAuto(); break;
            case Kw::EDIT: stmtEdit(); break;
            case Kw::SHELL: stmtShell(); break;
            case Kw::SYSTEM: exitRequested = true; throw EndSignal{};
            case Kw::ENVIRON: {
                string s = evalStr();
                size_t eq = s.find('=');
                if (eq == string::npos) err(5);
                setenv(s.substr(0, eq).c_str(), s.substr(eq + 1).c_str(), 1);
                break;
            }
            case Kw::SCREEN: {
                // SCREEN 0[,...] accepted; graphics modes unsupported.
                if (!atStmtEnd()) {
                    if (!isPun(",")) {
                        int m = evalIntRange(0, 255);
                        if (m != 0) err(73);
                    }
                    while (eatPun(",")) {
                        if (!atStmtEnd() && !isPun(",")) evalExpr();
                    }
                }
                break;
            }
            // hardware / graphics: not available in a terminal
            case Kw::CALL: case Kw::CIRCLE: case Kw::COM: case Kw::DRAW:
            case Kw::IOCTL: case Kw::LOCK: case Kw::MOTOR: case Kw::PAINT:
            case Kw::PALETTE: case Kw::PCOPY: case Kw::PEN: case Kw::PRESET:
            case Kw::PSET: case Kw::STRIG: case Kw::UNLOCK: case Kw::WAIT:
                err(73);
            default:
                err(2);
        }
    }

    // =======================================================================
    // Assignment
    // =======================================================================
    void stmtLet() {
        if (isKw(Kw::MIDS)) { adv(); stmtMidAssign(); return; }
        LRef r = parseVarRef();
        wantPun("=");
        assignRef(r, evalExpr());
    }

    void stmtMidAssign() {
        wantPun("(");
        LRef r = parseVarRef();
        if (typeOfKey(r.key) != '$') err(13);
        wantPun(",");
        int n = evalIntRange(1, 255);
        int m = -1;
        if (eatPun(",")) m = evalIntRange(0, 255);
        wantPun(")");
        wantPun("=");
        string repl = evalStr();
        Value v = readRef(r);
        string s = v.s;
        if ((size_t)n > s.size()) err(5);
        size_t count = repl.size();
        if (m >= 0) count = std::min(count, (size_t)m);
        count = std::min(count, s.size() - (n - 1));
        s.replace(n - 1, count, repl.substr(0, count));
        assignRef(r, Value::str(s));
    }

    // =======================================================================
    // PRINT and friends
    // =======================================================================
    struct Sink {
        virtual ~Sink() {}
        virtual int col() = 0;
        virtual int width() = 0;
        virtual void text(const string& s) = 0;
        virtual void nl() = 0;
    };
    struct ScreenSink : Sink {
        Screen& s;
        explicit ScreenSink(Screen& sc) : s(sc) {}
        int col() override { return s.col(); }
        int width() override { return s.widthLimit; }
        void text(const string& t) override { s.write(t); }
        void nl() override { s.write("\n"); }
    };
    struct PrinterSink : Sink {
        Basic& b;
        explicit PrinterSink(Basic& bb) : b(bb) {}
        int col() override { return b.lpos; }
        int width() override { return 132; }
        void text(const string& t) override {
            b.ensurePrinter();
            for (char c : t) {
                fputc(c, b.printer);
                if (c == '\n') b.lpos = 1;
                else b.lpos++;
            }
        }
        void nl() override {
            b.ensurePrinter();
            fputc('\n', b.printer);
            b.lpos = 1;
        }
    };
    struct FileSink : Sink {
        FileHandle& fh;
        explicit FileSink(FileHandle& f) : fh(f) {}
        int col() override { return fh.col; }
        int width() override { return 32767; }
        void text(const string& t) override {
            fh.f.write(t.data(), t.size());
            for (char c : t) {
                if (c == '\n') fh.col = 1;
                else fh.col++;
            }
        }
        void nl() override {
            fh.f.put('\n');
            fh.col = 1;
        }
    };

    void ensurePrinter() {
        if (!printer) {
            printer = fopen("printer.out", "a");
            if (!printer) err(27);
            lpos = 1;
        }
    }

    void stmtPrint(bool toPrinter) {
        if (toPrinter) {
            PrinterSink sink(*this);
            printBody(sink);
            if (printer) fflush(printer);
            return;
        }
        if (isPun("#")) {
            adv();
            int n = evalFileNum();
            wantPun(",");
            FileHandle& fh = fileFor(n, "OA");   // no buffer ops on RANDOM
            FileSink sink(fh);
            printBody(sink);
            return;
        }
        ScreenSink sink(scr);
        printBody(sink);
        scr.flush();
    }

    void printBody(Sink& sink) {
        bool pendingNl = true;
        while (!atStmtEnd()) {
            if (eatPun(";")) { pendingNl = false; continue; }
            if (eatPun(",")) {
                pendingNl = false;
                int zone = ((sink.col() - 1) / 14 + 1) * 14 + 1;
                if (zone + 13 > sink.width()) sink.nl();
                else sink.text(string(zone - sink.col(), ' '));
                continue;
            }
            if (isKw(Kw::TAB)) {
                adv();
                wantPun("(");
                int n = evalIntRange(1, 255);
                wantPun(")");
                int w = sink.width();
                if (n > w) n = ((n - 1) % w) + 1;
                if (sink.col() > n) sink.nl();
                if (sink.col() < n) sink.text(string(n - sink.col(), ' '));
                pendingNl = false;
                continue;
            }
            if (isKw(Kw::SPC)) {
                adv();
                wantPun("(");
                int n = evalIntRange(0, 255);
                wantPun(")");
                sink.text(string(n % sink.width(), ' '));
                pendingNl = false;
                continue;
            }
            if (isKw(Kw::USING)) {
                adv();
                printUsing(sink, pendingNl);
                if (pendingNl) sink.nl();
                return;
            }
            Value v = evalExpr();
            string s;
            if (v.isStr()) s = v.s;
            else s = fmtNumSigned(v) + " ";
            if (sink.col() > 1 && (int)s.size() < sink.width() &&
                sink.col() + (int)s.size() - 1 > sink.width())
                sink.nl();
            sink.text(s);
            pendingNl = true;
        }
        if (pendingNl) sink.nl();
    }

    // =======================================================================
    // WRITE [#n,] expr list — quoted strings, comma separated
    // =======================================================================
    void stmtWrite() {
        std::unique_ptr<Sink> owned;
        Sink* sink;
        if (isPun("#")) {
            adv();
            int n = evalFileNum();
            wantPun(",");
            owned.reset(new FileSink(fileFor(n, "OA")));
            sink = owned.get();
        } else {
            owned.reset(new ScreenSink(scr));
            sink = owned.get();
        }
        bool first = true;
        while (!atStmtEnd()) {
            if (!first) {
                if (!eatPun(",") && !eatPun(";")) err(2);
            }
            Value v = evalExpr();
            if (!first) sink->text(",");
            if (v.isStr()) sink->text("\"" + v.s + "\"");
            else {
                string s = fmtNum(v.n, v.t);
                sink->text(s);
            }
            first = false;
        }
        sink->nl();
        scr.flush();
    }

    // =======================================================================
    // INPUT family
    // =======================================================================
    string readLineRaw() {
        scr.flush();
        if (!interactive) {
            string line;
            if (!std::getline(std::cin, line)) err(62);
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.size() > 254) line.resize(254);   // GW's input buffer cap
            out(line);                  // echo; the caller decides about the CR
            scr.flush();
            return line;
        }
        string buf;
        while (true) {
            KeyEvent e = scr.waitKey();
            if (e.ch == 3) throw BreakSignal{curLine};
            if (e.ch == 13) return buf;
            if (e.ch == 8 || e.ch == 127) {
                if (!buf.empty()) {
                    buf.pop_back();
                    int c = scr.col(), r = scr.row();
                    if (c > 1) {
                        scr.locate(r, c - 1);
                        scr.putByte(' ');
                        scr.locate(r, c - 1);
                    }
                    scr.flush();
                }
                continue;
            }
            if (e.ch >= 32 && e.ch < 256 && buf.size() < 254) {
                buf += (char)e.ch;
                scr.putByte((unsigned char)e.ch);
                scr.flush();
            }
        }
    }

    // Split a keyboard INPUT reply into comma-separated items.
    static std::vector<string> splitInput(const string& line) {
        std::vector<string> items;
        size_t i = 0;
        const size_t n = line.size();
        while (true) {
            while (i < n && line[i] == ' ') i++;
            string item;
            if (i < n && line[i] == '"') {
                i++;
                while (i < n && line[i] != '"') item += line[i++];
                if (i < n) i++;
                while (i < n && line[i] != ',') i++;
            } else {
                size_t start = i;
                while (i < n && line[i] != ',') i++;
                item = rtrim(line.substr(start, i - start));
            }
            items.push_back(item);
            if (i < n && line[i] == ',') { i++; continue; }
            break;
        }
        return items;
    }

    void stmtInput() {
        if (isPun("#")) { adv(); inputFile(false); return; }
        if (curLine == -1) err(12);
        bool keepLine = eatPun(";");           // INPUT; — stay on the line
        string prompt;
        bool question = true;
        if (cur().t == Token::Str) {
            prompt = cur().s;
            adv();
            if (eatPun(",")) question = false;
            else if (eatPun(";")) question = true;
            else err(2);
        }
        std::vector<LRef> refs;
        do {
            refs.push_back(parseVarRef());
        } while (eatPun(","));
        while (true) {
            out(prompt);
            if (question) out("? ");
            string line = readLineRaw();
            if (!keepLine) outNl();
            if (trim(line).empty()) break;       // blank reply: keep old values
            std::vector<string> items = splitInput(line);
            if (items.size() != refs.size()) {
                outLine("?Redo from start");
                continue;
            }
            bool ok = true;
            std::vector<std::pair<size_t, Value>> vals;
            for (size_t i = 0; i < refs.size(); i++) {
                if (trim(items[i]).empty()) continue;   // empty item: unchanged
                if (typeOfKey(refs[i].key) == '$') {
                    vals.push_back({i, Value::str(items[i])});
                } else {
                    bool full;
                    double d = parseVal(items[i], &full);
                    if (!full) { ok = false; break; }
                    vals.push_back({i, Value::num(d, '#')});
                }
            }
            if (!ok) {
                outLine("?Redo from start");
                continue;
            }
            for (auto& pv : vals) assignRef(refs[pv.first], pv.second);
            break;
        }
    }

    // LINE INPUT and graphics LINE share a keyword.
    void stmtLine() {
        if (!isKw(Kw::INPUT)) err(73);          // graphics LINE: unsupported
        adv();
        if (isPun("#")) { adv(); inputFile(true); return; }
        if (curLine == -1) err(12);
        bool keepLine = eatPun(";");
        string prompt;
        if (cur().t == Token::Str) {
            prompt = cur().s;
            adv();
            if (!eatPun(";")) eatPun(",");
        }
        LRef r = parseVarRef();
        if (typeOfKey(r.key) != '$') err(13);
        out(prompt);
        string line = readLineRaw();
        if (!keepLine) outNl();
        assignRef(r, Value::str(line.substr(0, 255)));
    }

    // INPUT #n / LINE INPUT #n
    void inputFile(bool lineMode) {
        int n = evalFileNum();
        wantPun(",");
        FileHandle& fh = fileFor(n, "I");
        if (lineMode) {
            LRef r = parseVarRef();
            if (typeOfKey(r.key) != '$') err(13);
            string line;
            if (!std::getline(fh.f, line)) err(62);
            if (!line.empty() && line.back() == '\r') line.pop_back();
            assignRef(r, Value::str(line.substr(0, 255)));
            return;
        }
        do {
            LRef r = parseVarRef();
            bool isStr = typeOfKey(r.key) == '$';
            string item = readFileItem(fh, isStr);
            if (isStr) assignRef(r, Value::str(item.substr(0, 255)));
            else assignRef(r, Value::num(parseVal(item), '#'));
        } while (eatPun(","));
    }

    // One comma/newline-delimited item from a sequential file.
    string readFileItem(FileHandle& fh, bool isStr) {
        std::fstream& f = fh.f;
        int c = f.get();
        while (c == ' ' || c == '\t' || c == '\r' || c == '\n') c = f.get();
        if (c == EOF) err(62);
        string item;
        if (c == '"') {
            c = f.get();
            while (c != EOF && c != '"') { item += (char)c; c = f.get(); }
            // consume up to the delimiter
            c = f.get();
            while (c != EOF && c != ',' && c != '\n') c = f.get();
            return item;
        }
        while (c != EOF && c != ',' && c != '\n' && c != '\r' &&
               !(!isStr && (c == ' ' || c == '\t'))) {
            item += (char)c;
            if (isStr && item.size() >= 255) {
                // unquoted items cap at 255; the rest is the next item
                return rtrim(item);
            }
            c = f.get();
        }
        if (c == '\r') {
            if (f.peek() == '\n') f.get();
        } else if (!isStr && (c == ' ' || c == '\t')) {
            // a blank ends a number; further items may share the line
            int p = f.peek();
            while (p == ' ' || p == '\t') { f.get(); p = f.peek(); }
            if (p == ',') f.get();
            else if (p == '\r') { f.get(); if (f.peek() == '\n') f.get(); }
            else if (p == '\n') f.get();
        }
        return isStr ? rtrim(item) : trim(item);
    }

    // =======================================================================
    // IF / THEN / ELSE  (single-line, GW style)
    // =======================================================================
    void skipToElse() {
        int depth = 0;
        while (ti < T->size()) {
            const Token& t = (*T)[ti];
            if (t.t == Token::Key && t.kw == Kw::IF) depth++;
            else if (t.t == Token::Key && t.kw == Kw::ELSE) {
                if (depth == 0) { ti++; return; }
                depth--;
            } else if (t.t == Token::Rem) { ti = T->size(); return; }
            ti++;
        }
    }

    void stmtIf() {
        Value cv = evalExpr();
        if (cv.isStr()) err(13);
        bool cond = cv.n != 0;
        if (isKw(Kw::GOTO)) {
            adv();
            int n = wantLineNum();
            if (cond) { goLine(n); return; }
        } else {
            wantKw(Kw::THEN);
            if (cond) {
                if (cur().t == Token::Num) { goLine(wantLineNum()); }
                return;                       // statements follow inline
            }
            // condition false: find the matching ELSE
        }
        if (cond) return;
        skipToElse();
        if (ti < T->size() && cur().t == Token::Num) goLine(wantLineNum());
    }

    // =======================================================================
    // FOR / NEXT
    // =======================================================================
    void stmtFor() {
        if (cur().t != Token::Id) err(2);
        string key = varKey(cur().s);
        if (typeOfKey(key) == '$') err(13);
        adv();
        wantPun("=");
        Value init = coerceTo(typeOfKey(key), evalExpr());
        wantKw(Kw::TO);
        double limit = evalNum();
        double step = 1;
        if (isKw(Kw::STEP)) { adv(); step = evalNum(); }
        setVar(key, init);
        // Restarting a FOR with the same variable discards inner loops —
        // but the search stops at the current GOSUB frame, like GW's FNDFOR.
        for (size_t i = forStack.size(); i-- > forFloor();) {
            if (forStack[i].var == key) {
                while (!whileStack.empty() && whileStack.back().seq > forStack[i].seq)
                    whileStack.pop_back();
                forStack.resize(i);
                break;
            }
        }
        if ((step > 0 && init.n > limit) || (step < 0 && init.n < limit)) {
            scanPastNext(key);
            return;
        }
        ForEntry e;
        e.var = key;
        e.limit = limit;
        e.step = step;
        e.body = here();
        e.seq = ++stackSeq;
        forStack.push_back(e);
        if (forStack.size() > 5000) err(7);
    }

    // The loop body is skipped entirely: find the matching NEXT.
    void scanPastNext(const string& key) {
        int depth = 0;
        Pc p = here();
        while (true) {
            const std::vector<Token>& tv = toksFor(p.line);
            while (p.ti < tv.size()) {
                const Token& t = tv[p.ti];
                if (t.t == Token::Key && t.kw == Kw::FOR) depth++;
                else if (t.t == Token::Key && t.kw == Kw::NEXT) {
                    // gather the variable list
                    size_t j = p.ti + 1;
                    std::vector<string> list;
                    while (j < tv.size() && tv[j].t == Token::Id) {
                        list.push_back(varKey(tv[j].s));
                        j++;
                        if (j < tv.size() && tv[j].t == Token::Pun && tv[j].s == ",") j++;
                        else break;
                    }
                    int closes = list.empty() ? 1 : (int)list.size();
                    if (depth >= closes) {
                        depth -= closes;
                    } else {
                        // our FOR closes here
                        if (!list.empty() && depth < (int)list.size() &&
                            (int)list.size() > depth + 1) {
                            // vars remain after ours: re-enter NEXT, skipping ours
                            skipNextVarOnce = key;
                            Pc q;
                            q.line = p.line;
                            q.ti = p.ti;
                            go(q);
                            return;
                        }
                        Pc q;
                        q.line = p.line;
                        q.ti = j;
                        go(q);
                        return;
                    }
                    p.ti = j;
                    continue;
                } else if (t.t == Token::Rem) {
                    p.ti = tv.size();
                    break;
                }
                p.ti++;
            }
            // advance to the next line
            if (p.line == -1) err(26);
            auto it = prog.upper_bound(p.line);
            if (it == prog.end()) err(26);
            p.line = it->first;
            p.ti = 0;
        }
    }

    void stmtNext() {
        bool first = true;
        while (true) {
            string key;
            if (cur().t == Token::Id) {
                key = varKey(cur().s);
                adv();
            }
            if (first && !skipNextVarOnce.empty() && key == skipNextVarOnce) {
                skipNextVarOnce.clear();
            } else {
                size_t lo = forFloor();            // don't cross the GOSUB frame
                int idx = -1;
                if (key.empty()) {
                    if (forStack.size() <= lo) err(1);
                    idx = (int)forStack.size() - 1;
                } else {
                    for (size_t i = forStack.size(); i-- > lo;)
                        if (forStack[i].var == key) { idx = (int)i; break; }
                    if (idx < 0) err(1);
                }
                // drop abandoned inner loops, including WHILEs opened after
                while (!whileStack.empty() && whileStack.back().seq > forStack[idx].seq)
                    whileStack.pop_back();
                forStack.resize(idx + 1);
                ForEntry& e = forStack.back();
                string var = e.var;
                double v = toNum(getVar(var)) + e.step;
                Value nv = coerceTo(typeOfKey(var), Value::num(v, '#'));
                setVar(var, nv);
                bool done = e.step >= 0 ? nv.n > e.limit : nv.n < e.limit;
                if (!done) {
                    go(e.body);
                    return;
                }
                forStack.pop_back();
            }
            first = false;
            if (!eatPun(",")) break;
        }
    }

    // =======================================================================
    // WHILE / WEND
    // =======================================================================
    // Locate the position just past the WEND matching a WHILE at the current
    // position.  GW performs this scan on every WHILE execution, so a WHILE
    // with no following WEND errors even when the condition is true.
    Pc findWend() {
        int depth = 0;
        Pc p = here();
        while (true) {
            const std::vector<Token>& tv = toksFor(p.line);
            while (p.ti < tv.size()) {
                const Token& t = tv[p.ti];
                if (t.t == Token::Key && t.kw == Kw::WHILE) depth++;
                else if (t.t == Token::Key && t.kw == Kw::WEND) {
                    if (depth == 0) {
                        p.ti++;
                        return p;
                    }
                    depth--;
                } else if (t.t == Token::Rem) {
                    p.ti = tv.size();
                    break;
                }
                p.ti++;
            }
            if (p.line == -1) err(29);
            auto it = prog.upper_bound(p.line);
            if (it == prog.end()) err(29);
            p.line = it->first;
            p.ti = 0;
        }
    }

    void stmtWhile() {
        Pc afterWend = findWend();              // raises 29 when unmatched
        double c = evalNum();
        if (c != 0) {
            WhileEntry e;
            e.pos = stmtBegin;
            e.seq = ++stackSeq;
            whileStack.push_back(e);
            if (whileStack.size() > 5000) err(7);
            return;
        }
        go(afterWend);
    }

    void stmtWend() {
        if (whileStack.size() <= whileFloor()) err(30);
        Pc p = whileStack.back().pos;
        whileStack.pop_back();
        go(p);                                  // re-runs the WHILE statement
    }

    // =======================================================================
    // RETURN / ON
    // =======================================================================
    void stmtReturn() {
        if (gosubStack.empty()) err(3);
        GosubEntry e = gosubStack.back();
        gosubStack.pop_back();
        // discard FOR/WHILE frames opened inside the subroutine (GW FNDFOR)
        if (forStack.size() > e.forMark) forStack.resize(e.forMark);
        if (whileStack.size() > e.whileMark) whileStack.resize(e.whileMark);
        if (inTimerTrap && gosubStack.size() <= timerFireDepth) {
            inTimerTrap = false;                // trap handler done: re-arm
            timerNextFire = nowSeconds() + timerInterval;
        }
        if (cur().t == Token::Num) {
            goLine(wantLineNum());
            return;
        }
        go(e.ret);
    }

    void stmtOn() {
        if (isKw(Kw::ERROR)) {
            adv();
            wantKw(Kw::GOTO);
            if (cur().t != Token::Num) err(2);
            int n = (int)cur().n;
            adv();
            if (n == 0) {
                errHandlerLine = 0;
                if (inHandler) {
                    inHandler = false;
                    curLine = errLine;         // report at the original site
                    err(errCode);              // becomes fatal in the caller
                }
                return;
            }
            if (!prog.count(n)) err(8);
            errHandlerLine = n;
            return;
        }
        if (isKw(Kw::TIMER)) {                  // ON TIMER(n) GOSUB line
            adv();
            wantPun("(");
            double n = evalNum();
            wantPun(")");
            wantKw(Kw::GOSUB);
            int line = wantLineNum();
            if (n < 1 || n > 86400) err(5);
            timerTrapLine = line;               // 0 disables the trap
            timerInterval = n;
            timerNextFire = nowSeconds() + n;
            return;
        }
        if (isKw(Kw::KEY) || isKw(Kw::PLAY) || isKw(Kw::STRIG) ||
            isKw(Kw::PEN) || isKw(Kw::COM))
            err(73);
        int v = evalIntRange(0, 255);
        bool isGosub;
        if (eatKw(Kw::GOTO)) isGosub = false;
        else if (eatKw(Kw::GOSUB)) isGosub = true;
        else { err(2); }
        std::vector<int> targets;
        if (cur().t == Token::Num) {
            targets.push_back(wantLineNum());
            while (eatPun(",")) targets.push_back(wantLineNum());
        }
        if (v >= 1 && v <= (int)targets.size()) {
            int n = targets[v - 1];
            if (isGosub) gosubStack.push_back(makeGosub(here()));
            goLine(n);
        }
    }

    // =======================================================================
    // DATA / READ / RESTORE
    // =======================================================================
    int lastDataLine = -1;       // where the last READ item came from
    bool lastDataQuoted = false;

    string nextDataItem() {
        Pc p = dataPc;
        if (p.line == -2) {
            if (prog.empty()) err(4);
            p.line = prog.begin()->first;
            p.ti = 0;
        }
        while (true) {
            if (!prog.count(p.line)) err(4);
            const std::vector<Token>& tv = toksFor(p.line);
            while (p.ti < tv.size()) {
                if (tv[p.ti].t == Token::Dat) {
                    string item = tv[p.ti].s;
                    lastDataLine = p.line;
                    lastDataQuoted = (tv[p.ti].nt == 'Q');
                    p.ti++;
                    dataPc = p;
                    return item;
                }
                p.ti++;
            }
            auto it = prog.upper_bound(p.line);
            if (it == prog.end()) err(4);
            p.line = it->first;
            p.ti = 0;
        }
    }

    void stmtRead() {
        do {
            LRef r = parseVarRef();
            string item = nextDataItem();
            if (typeOfKey(r.key) == '$') {
                assignRef(r, Value::str(item));
            } else {
                bool full;
                double d = parseVal(item, &full);
                bool bad = lastDataQuoted ||
                           (!full && !trim(item).empty());
                if (bad) {
                    // GW reports the bad item at its DATA line, not the READ
                    curLine = lastDataLine;
                    err(2);
                }
                assignRef(r, Value::num(d, '#'));
            }
        } while (eatPun(","));
    }

    void stmtRestore() {
        if (cur().t == Token::Num) {
            int n = wantLineNum();
            if (!prog.count(n)) err(8);
            dataPc = Pc{n, 0};
        } else {
            dataPc = Pc{-2, 0};
        }
    }

    // =======================================================================
    // DIM and friends
    // =======================================================================
    void stmtDim() {
        do {
            if (cur().t != Token::Id) err(2);
            string key = varKey(cur().s);
            adv();
            wantPun("(");
            std::vector<int> ext;
            do {
                int hi = evalInt16();
                if (hi < optionBase) err(9);
                ext.push_back(hi - optionBase + 1);
            } while (eatPun(","));
            wantPun(")");
            makeArray(key, ext);
        } while (eatPun(","));
    }

    void stmtErase() {
        do {
            if (cur().t != Token::Id) err(2);
            string key = varKey(cur().s);
            adv();
            if (!arrays.erase(key)) err(5);
        } while (eatPun(","));
    }

    void stmtDefType(char ty) {
        do {
            if (cur().t != Token::Id || cur().s.size() != 1) err(2);
            char a = cur().s[0];
            adv();
            char b = a;
            if (eatPun("-")) {
                if (cur().t != Token::Id || cur().s.size() != 1) err(2);
                b = cur().s[0];
                adv();
            }
            if (a > b) std::swap(a, b);
            for (char c = a; c <= b; c++)
                if (c >= 'A' && c <= 'Z') defType[c - 'A'] = ty;
        } while (eatPun(","));
    }

    void stmtDef() {
        if (isKw(Kw::SEG)) {                     // DEF SEG[=expr]: accepted, ignored
            adv();
            if (eatPun("=")) evalNum();
            return;
        }
        if (isKw(Kw::USR)) {                     // DEF USR: not supported
            err(73);
        }
        wantKw(Kw::FN);
        if (curLine == -1) err(12);              // DEF FN is illegal direct
        if (cur().t != Token::Id) err(2);
        string key = varKey(cur().s);
        adv();
        FnDef fn;
        fn.ret = typeOfKey(key);
        if (eatPun("(")) {
            do {
                if (cur().t != Token::Id) err(2);
                fn.params.push_back(varKey(cur().s));
                adv();
            } while (eatPun(","));
            wantPun(")");
        }
        wantPun("=");
        while (!atStmtEnd()) {
            fn.body.push_back(cur());
            adv();
        }
        if (fn.body.empty()) err(2);
        fns[key] = std::move(fn);
    }

    void stmtSwap() {
        LRef a = parseVarRef();
        wantPun(",");
        LRef b = parseVarRef();
        if (typeOfKey(a.key) != typeOfKey(b.key)) err(13);
        // GW raises Illegal function call when a scalar operand is undefined
        if (!a.isArr && !vars.count(a.key)) err(5);
        if (!b.isArr && !vars.count(b.key)) err(5);
        Value va = readRef(a), vb = readRef(b);
        assignRef(a, vb);
        assignRef(b, va);
    }

    void stmtRandomize() {
        if (!atStmtEnd()) {
            randomizeSeed(evalNum());
            return;
        }
        while (true) {
            out("Random number seed (-32768 to 32767)? ");
            string line = readLineRaw();
            outNl();
            bool full;
            double d = parseVal(line, &full);
            if (!full) { outLine("?Redo from start"); continue; }
            if (d < -32768 || d > 32767) { outLine("Overflow"); continue; }
            randomizeSeed(d);
            return;
        }
    }

    // =======================================================================
    // Screen statements
    // =======================================================================
    void stmtLocate() {
        int r = scr.row(), c = scr.col();
        if (!atStmtEnd() && !isPun(",")) r = evalIntRange(1, 255);
        if (eatPun(",")) {
            if (!atStmtEnd() && !isPun(",")) c = evalIntRange(1, 255);
            if (eatPun(",")) {
                if (!atStmtEnd() && !isPun(",")) {
                    int vis = evalIntRange(0, 255);
                    scr.setCursorVisible(vis != 0);
                }
                while (eatPun(",")) {
                    if (!atStmtEnd() && !isPun(",")) evalExpr();
                }
            }
        }
        if (r > scr.rows() || c > scr.cols()) err(5);
        scr.locate(r, c);
        scr.flush();
    }

    void stmtColor() {
        int fg = -1, bg = -1;
        if (!atStmtEnd() && !isPun(",")) fg = evalIntRange(0, 31);
        if (eatPun(",")) {
            if (!atStmtEnd() && !isPun(",")) bg = evalIntRange(0, 15);
            if (eatPun(",")) {
                if (!atStmtEnd()) evalExpr();          // border: ignored
            }
        }
        CursesScreen* cs = dynamic_cast<CursesScreen*>(&scr);
        int curFg = cs ? cs->fg : 7, curBg = cs ? cs->bg : 0;
        scr.setColor(fg >= 0 ? (fg & 15) : curFg, bg >= 0 ? (bg & 7) : curBg);
    }

    void stmtWidth() {
        if (cur().t == Token::Str) {              // WIDTH "LPT1:", n — ignored
            adv();
            wantPun(",");
            evalNum();
            return;
        }
        if (isPun("#")) {                         // WIDTH #n, m — ignored
            adv();
            evalNum();
            wantPun(",");
            evalNum();
            return;
        }
        int n = evalIntRange(1, 255);
        if (n != 40 && n != 80) err(5);
        bool changed = (scr.widthLimit != n);
        scr.widthLimit = n;
        if (eatPun(",")) evalNum();               // height: ignored
        if (changed) scr.cls();                   // GW clears on width change
    }

    void stmtKey() {
        if (isKw(Kw::LIST)) {
            adv();
            for (int i = 0; i < 10; i++) {
                string vis;
                for (char c : keyMacros[i]) vis += (c == '\r' ? ' ' : c);
                outLine("F" + std::to_string(i + 1) + " " + vis);
            }
            return;
        }
        if (isKw(Kw::ON)) {                       // show the soft-key row
            adv();
            std::vector<string> labels(keyMacros, keyMacros + 10);
            scr.setSoftKeys(&labels);
            return;
        }
        if (isKw(Kw::OFF)) {
            adv();
            scr.setSoftKeys(nullptr);
            return;
        }
        int n = evalIntRange(1, 255);
        wantPun(",");
        string s = evalStr();
        if (n >= 1 && n <= 10) keyMacros[n - 1] = s.substr(0, 15);
    }

    // =======================================================================
    // Error control
    // =======================================================================
    void stmtResume() {
        if (!inHandler) err(20);
        if (isKw(Kw::NEXT)) {
            adv();
            inHandler = false;
            go(afterStatement(errPc));
            return;
        }
        if (cur().t == Token::Num) {
            int n = wantLineNum();
            if (n == 0) {
                inHandler = false;
                go(errPc);
                return;
            }
            if (!prog.count(n)) err(8);  // still in handler: error is fatal
            inHandler = false;
            goLine(n);
            return;
        }
        inHandler = false;
        go(errPc);
    }

    void stmtClear() {
        // CLEAR [,size[,stack]] — sizes are accepted and ignored
        if (!atStmtEnd() && !isPun(",")) evalExpr();
        while (eatPun(",")) {
            if (!atStmtEnd() && !isPun(",")) evalExpr();
        }
        clearVariables();
        closeAllFiles();
        invalidateCont();
    }

    // =======================================================================
    // Files
    // =======================================================================
    // A file-number expression; out of range is error 52, not 5.
    int evalFileNum() {
        double d = evalNum();
        if (!(d >= -32768.5 && d < 32767.5)) err(6);
        int n = (int)lrint(d);
        if (n < 1 || n > MAXFILES) err(52);
        return n;
    }

    FileHandle& fileFor(int n, const char* allowedModes) {
        if (n < 1 || n > MAXFILES) err(52);
        if (!files[n]) err(52);
        if (!strchr(allowedModes, files[n]->mode)) err(54);
        return *files[n];
    }

    bool pathIsOpen(const string& path, char* openMode = nullptr) {
        for (int i = 1; i <= MAXFILES; i++) {
            if (files[i] && files[i]->path == path) {
                if (openMode) *openMode = files[i]->mode;
                return true;
            }
        }
        return false;
    }

    void closeFile(int n) {
        if (n < 1 || n > MAXFILES || !files[n]) return;
        files[n]->f.flush();
        files[n].reset();
    }

    bool fileEof(int n) {
        if (!files[n]) err(52);
        FileHandle& fh = *files[n];
        if (fh.mode == 'I') return fh.f.peek() == EOF;
        if (fh.mode == 'R') return fh.lastRec * fh.reclen >= fileLof(n);
        return true;
    }

    long fileLoc(int n) {
        if (!files[n]) err(52);
        FileHandle& fh = *files[n];
        if (fh.mode == 'R') return fh.lastRec;
        std::streampos p = (fh.mode == 'I') ? fh.f.tellg() : fh.f.tellp();
        long off = (p < 0) ? 0 : (long)p;
        return (off + 127) / 128;
    }

    long fileLof(int n) {
        if (!files[n]) err(52);
        FileHandle& fh = *files[n];
        fh.f.clear();
        if (fh.mode == 'I' || fh.mode == 'R') {
            std::streampos cur = fh.f.tellg();
            fh.f.seekg(0, std::ios::end);
            long size = (long)fh.f.tellg();
            fh.f.seekg(cur);
            return size;
        }
        fh.f.flush();
        std::streampos cur = fh.f.tellp();
        fh.f.seekp(0, std::ios::end);
        long size = (long)fh.f.tellp();
        fh.f.seekp(cur);
        return size;
    }

    void stmtOpen() {
        // Old syntax:  OPEN mode$, [#]n, file$ [, reclen]
        // New syntax:  OPEN file$ [FOR mode] [ACCESS ...] AS [#]n [LEN = m]
        string first = evalStr();
        char mode;
        string path;
        int n;
        int reclen = 128;
        if (isPun(",")) {                          // old syntax
            adv();
            if (first.empty()) err(54);
            mode = (char)toupper((unsigned char)first[0]);
            if (!strchr("IOAR", mode)) err(54);
            eatPun("#");
            n = evalFileNum();
            wantPun(",");
            path = evalStr();
            if (eatPun(",")) reclen = evalIntRange(1, 32767);
        } else {                                   // new syntax
            path = first;
            mode = 'R';
            if (eatKw(Kw::FOR)) {
                if (eatKw(Kw::INPUT)) mode = 'I';
                else if (cur().t == Token::Id && cur().s == "OUTPUT") { mode = 'O'; adv(); }
                else if (cur().t == Token::Id && cur().s == "APPEND") { mode = 'A'; adv(); }
                else if (cur().t == Token::Id && cur().s == "RANDOM") { mode = 'R'; adv(); }
                else err(2);
            }
            if (cur().t == Token::Id && cur().s == "ACCESS") {   // ignored
                adv();
                if (eatKw(Kw::READ)) { if (cur().t == Token::Id && cur().s == "WRITE") adv(); }
                else if (cur().t == Token::Id && cur().s == "WRITE") adv();
            }
            wantKw(Kw::AS);
            eatPun("#");
            n = evalFileNum();
            if (isKw(Kw::LEN)) {
                adv();
                wantPun("=");
                reclen = evalIntRange(1, 32767);
            }
        }
        if (files[n]) err(55);
        if (path.empty()) err(64);
        // GW raises 55 when the file is already open, except INPUT+INPUT.
        char om0;
        if (pathIsOpen(path, &om0) && !(mode == 'I' && om0 == 'I')) err(55);
        auto fh = std::make_unique<FileHandle>();
        fh->mode = mode;
        fh->reclen = reclen;
        fh->path = path;
        std::ios::openmode om = std::ios::binary;
        switch (mode) {
            case 'I': om |= std::ios::in; break;
            case 'O': om |= std::ios::out | std::ios::trunc; break;
            case 'A': om |= std::ios::out | std::ios::app; break;
            case 'R': om |= std::ios::in | std::ios::out; break;
        }
        fh->f.open(path, om);
        if (!fh->f.is_open() && mode == 'R') {
            // create the file, then reopen read/write
            std::ofstream touch(path, std::ios::binary | std::ios::app);
            touch.close();
            fh->f.open(path, om);
        }
        if (!fh->f.is_open()) err(mode == 'I' ? 53 : 75);
        fh->recbuf.assign(reclen, ' ');
        files[n] = std::move(fh);
    }

    void stmtClose() {
        if (atStmtEnd()) {
            closeAllFiles();
            return;
        }
        do {
            eatPun("#");
            int n = evalFileNum();
            closeFile(n);
        } while (eatPun(","));
    }

    static bool sameRef(const LRef& a, const LRef& b) {
        return a.key == b.key && a.isArr == b.isArr && a.idx == b.idx;
    }

    void stmtField() {
        eatPun("#");
        int n = evalFileNum();
        FileHandle& fh = fileFor(n, "R");
        // GW keeps earlier FIELD mappings: multiple FIELDs overlay the buffer.
        int off = 0;
        do {
            wantPun(",");
            int w = evalIntRange(0, 32767);
            wantKw(Kw::AS);
            LRef r = parseVarRef();
            if (typeOfKey(r.key) != '$') err(13);
            if (off + w > fh.reclen) err(50);
            // re-FIELDing the same variable replaces its binding
            for (size_t i = 0; i < fh.fields.size(); i++) {
                if (sameRef(fh.fields[i].ref, r)) {
                    fh.fields.erase(fh.fields.begin() + i);
                    break;
                }
            }
            FieldDef fd;
            fd.ref = r;
            fd.off = off;
            fd.len = w;
            fh.fields.push_back(fd);
            assignRef(r, Value::str(fh.recbuf.substr(off, w)));
            off += w;
        } while (isPun(","));
    }

    void stmtGetPut(bool isPut) {
        if (isPun("(")) err(73);                   // graphics GET/PUT
        eatPun("#");
        int n = evalFileNum();
        FileHandle& fh = fileFor(n, "R");
        long rec = fh.lastRec + 1;
        if (eatPun(",")) {
            double d = evalNum();
            if (d < 1 || d > 16777215) err(63);
            rec = (long)llrint(d);
        }
        fh.f.clear();
        if (isPut) {
            // The record buffer (filled by LSET/RSET) is the source of truth.
            fh.f.seekp((std::streamoff)(rec - 1) * fh.reclen);
            fh.f.write(fh.recbuf.data(), fh.reclen);
            fh.f.flush();
        } else {
            fh.f.seekg((std::streamoff)(rec - 1) * fh.reclen);
            string buf(fh.reclen, '\0');
            fh.f.read(&buf[0], fh.reclen);
            std::streamsize got = fh.f.gcount();
            if (got == 0) err(62);
            fh.f.clear();
            fh.recbuf = buf;
            for (auto& fd : fh.fields)
                assignRef(fd.ref, Value::str(fh.recbuf.substr(fd.off, fd.len)));
        }
        fh.lastRec = rec;
    }

    void stmtLRSet(bool isLeft) {
        LRef r = parseVarRef();
        if (typeOfKey(r.key) != '$') err(13);
        wantPun("=");
        string v = evalStr();
        // Is this variable FIELDed to an open file?
        FileHandle* ffh = nullptr;
        FieldDef* fdef = nullptr;
        for (int i = 1; i <= MAXFILES && !fdef; i++) {
            if (!files[i]) continue;
            for (auto& fd : files[i]->fields)
                if (sameRef(fd.ref, r)) { ffh = files[i].get(); fdef = &fd; break; }
        }
        size_t width = fdef ? (size_t)fdef->len : toStr(readRef(r)).size();
        string s;
        if (v.size() >= width) {
            s = v.substr(0, width);
        } else if (isLeft) {
            s = v + string(width - v.size(), ' ');
        } else {
            s = string(width - v.size(), ' ') + v;
        }
        assignRef(r, Value::str(s));
        if (fdef) ffh->recbuf.replace(fdef->off, fdef->len, s);
    }

    void stmtKill() {
        string pat = evalStr();
        if (pat.empty()) err(64);
        if (pat.find('*') != string::npos || pat.find('?') != string::npos) {
            DIR* d = opendir(".");
            if (!d) err(76);
            std::vector<string> hits;
            struct dirent* de;
            while ((de = readdir(d))) {
                if (de->d_name[0] == '.') continue;
                if (fnmatch(pat.c_str(), de->d_name, FNM_CASEFOLD) == 0)
                    hits.push_back(de->d_name);
            }
            closedir(d);
            if (hits.empty()) err(53);
            for (auto& h : hits)
                if (pathIsOpen(h)) err(55);
            for (auto& h : hits) unlink(h.c_str());
            return;
        }
        if (pathIsOpen(pat)) err(55);
        if (unlink(pat.c_str()) != 0) err(53);
    }

    void stmtName() {
        string oldName = evalStr();
        wantKw(Kw::AS);
        string newName = evalStr();
        if (pathIsOpen(oldName) || pathIsOpen(newName)) err(55);
        struct stat st;
        if (stat(oldName.c_str(), &st) != 0) err(53);
        if (stat(newName.c_str(), &st) == 0) err(58);
        if (rename(oldName.c_str(), newName.c_str()) != 0) err(75);
    }

    void stmtFiles() {
        string pat = "*";
        if (!atStmtEnd()) pat = evalStr();
        if (pat.empty()) pat = "*";
        char cwd[1024];
        if (getcwd(cwd, sizeof cwd)) outLine(cwd);
        DIR* d = opendir(".");
        if (!d) err(76);
        std::vector<string> names;
        struct dirent* de;
        while ((de = readdir(d))) {
            string nm = de->d_name;
            if (nm == "." || nm == "..") continue;
            if (fnmatch(pat.c_str(), nm.c_str(), FNM_CASEFOLD) == 0) {
                struct stat st;
                if (stat(nm.c_str(), &st) == 0 && S_ISDIR(st.st_mode)) nm += "/";
                names.push_back(nm);
            }
        }
        closedir(d);
        if (names.empty()) err(53);
        std::sort(names.begin(), names.end());
        int perRow = std::max(1, scr.widthLimit / 18);
        for (size_t i = 0; i < names.size(); i++) {
            string nm = names[i];
            if ((int)nm.size() < 17) nm.append(17 - nm.size(), ' ');
            out(nm + " ");
            if ((int)(i % perRow) == perRow - 1) outNl();
        }
        if (names.size() % perRow != 0) outNl();
    }

    // =======================================================================
    // Program management
    // =======================================================================
    // Parse [n | .][-[m | .]] line ranges for LIST/DELETE.
    void parseLineRange(int& lo, int& hi) {
        lo = 0;
        hi = 65529;
        bool any = false;
        if (cur().t == Token::Num) { lo = hi = wantLineNum(); any = true; }
        else if (isPun(".")) { adv(); lo = hi = std::max(0, lastUsedLine); any = true; }
        if (isPun("-")) {
            adv();
            hi = 65529;
            if (cur().t == Token::Num) hi = wantLineNum();
            else if (isPun(".")) { adv(); hi = std::max(0, lastUsedLine); }
            if (!any) lo = 0;
            any = true;
        }
    }

    void stmtList(bool toPrinter) {
        int lo, hi;
        parseLineRange(lo, hi);
        std::unique_ptr<Sink> sink;
        if (toPrinter) sink.reset(new PrinterSink(*this));
        else sink.reset(new ScreenSink(scr));
        for (auto& kv : prog) {
            if (kv.first < lo || kv.first > hi) continue;
            sink->text(std::to_string(kv.first) + " " + kv.second.text);
            sink->nl();
            if (scr.breakPending()) throw BreakSignal{-1};
        }
        if (toPrinter && printer) fflush(printer);
        scr.flush();
        if (curLine != -1) throw EndSignal{};   // LIST returns to command level
    }

    void stmtDelete() {
        int lo, hi;
        parseLineRange(lo, hi);
        auto it = prog.lower_bound(lo);
        bool any = false;
        while (it != prog.end() && it->first <= hi) {
            it = prog.erase(it);
            any = true;
        }
        if (!any) err(5);
        invalidateCont();
        throw EndSignal{};                         // back to command level
    }

    // Reset interpreter state and start the program.
    void startRun(int fromLine) {
        clearVariables();
        closeAllFiles();
        errCode = 0;
        errLine = 0;
        invalidateCont();
        if (prog.empty()) throw EndSignal{};
        if (fromLine < 0) fromLine = prog.begin()->first;
        goLine(fromLine);
    }

    void stmtRun() {
        if (cur().t == Token::Str || isKw(Kw::FN) || cur().t == Token::Id) {
            string f = evalStr();
            bool runIt = true;                     // RUN "file" always runs
            if (eatPun(",")) {
                if (cur().t == Token::Id && cur().s == "R") adv();
            }
            loadProgramFile(f);
            (void)runIt;
            startRun(-1);
            return;
        }
        int from = -1;
        if (cur().t == Token::Num) from = wantLineNum();
        startRun(from);
    }

    void stmtCont() {
        if (!contValid) err(17);
        contValid = false;
        go(contPc);
    }

    // Add .BAS when the name has no extension, GW style.
    static string withBasExt(const string& name) {
        size_t slash = name.find_last_of('/');
        string base = (slash == string::npos) ? name : name.substr(slash + 1);
        if (base.find('.') != string::npos) return name;
        return name + ".BAS";
    }

    // Find a loadable variant of the name (.BAS / .bas, as typed).
    static string resolveLoadName(const string& name) {
        auto exists = [](const string& p) {
            struct stat st;
            return stat(p.c_str(), &st) == 0 && !S_ISDIR(st.st_mode);
        };
        if (exists(name)) return name;
        size_t slash = name.find_last_of('/');
        string base = (slash == string::npos) ? name : name.substr(slash + 1);
        if (base.find('.') == string::npos) {
            if (exists(name + ".BAS")) return name + ".BAS";
            if (exists(name + ".bas")) return name + ".bas";
        }
        return name;                               // will fail with err 53
    }

    void mergeFromFile(const string& fname) {
        string path = resolveLoadName(fname);
        std::ifstream f(path, std::ios::binary);
        if (!f.is_open()) err(53);
        int first = f.peek();
        if (first == 0xFF || first == 0xFE) err(54);   // tokenized GW file
        string line;
        while (std::getline(f, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (trim(line).empty()) continue;
            size_t i = 0;
            while (i < line.size() && isdigit((unsigned char)line[i])) i++;
            if (i == 0) err(66);                   // direct statement in file
            int n = atoi(line.substr(0, i).c_str());
            if (n > 65529) err(8);
            if (i < line.size() && line[i] == ' ') i++;
            string text = line.substr(i);
            ProgLine pl;
            pl.text = text;
            prog[n] = std::move(pl);
        }
        invalidateCont();
    }

    void loadProgramFile(const string& fname) {
        std::map<int, ProgLine> saved;
        saved.swap(prog);
        try {
            mergeFromFile(fname);
        } catch (...) {
            saved.swap(prog);                      // keep old program on failure
            throw;
        }
    }

    void stmtLoad() {
        string f = evalStr();
        bool run = false;
        if (eatPun(",")) {
            if (cur().t == Token::Id && upcase(cur().s) == "R") { run = true; adv(); }
            else err(2);
        }
        loadProgramFile(f);
        if (run) {
            // ,R keeps open data files
            clearVariables();
            errCode = errLine = 0;
            invalidateCont();
            if (prog.empty()) throw EndSignal{};
            goLine(prog.begin()->first);
            return;
        }
        clearVariables();
        closeAllFiles();
        invalidateCont();
        throw EndSignal{};
    }

    void stmtSave() {
        string f = evalStr();
        if (eatPun(",")) {
            if (cur().t == Token::Id &&
                (upcase(cur().s) == "A" || upcase(cur().s) == "P")) adv();
            else err(2);
        }
        if (f.empty()) err(64);
        string path = withBasExt(f);
        std::ofstream o(path, std::ios::binary | std::ios::trunc);
        if (!o.is_open()) err(75);
        for (auto& kv : prog)
            o << kv.first << " " << kv.second.text << "\n";
    }

    void stmtMerge() {
        string f = evalStr();
        mergeFromFile(f);
        throw EndSignal{};
    }

    void stmtCommon() {
        do {
            if (cur().t != Token::Id) err(2);
            string key = varKey(cur().s);
            adv();
            if (eatPun("(")) wantPun(")");
            commons.insert(key);
        } while (eatPun(","));
    }

    void stmtChain() {
        bool merge = eatKw(Kw::MERGE);
        string f = evalStr();
        int startLine = -1;
        bool all = false;
        int delLo = -1, delHi = -1;
        if (eatPun(",")) {
            if (!atStmtEnd() && !isPun(",")) {
                if (!isKw(Kw::ALL)) startLine = (int)evalNum();
            }
            if (eatPun(",") || isKw(Kw::ALL)) {
                if (eatKw(Kw::ALL)) all = true;
                if (eatPun(",")) {
                    if (eatKw(Kw::DELETE)) parseLineRange(delLo, delHi);
                }
            }
        }
        // capture variables that survive the chain
        std::unordered_map<string, Value> keepVars;
        std::unordered_map<string, Array> keepArrays;
        if (all) {
            keepVars = vars;
            keepArrays = arrays;
        } else {
            for (auto& key : commons) {
                auto vi = vars.find(key);
                if (vi != vars.end()) keepVars[key] = vi->second;
                auto ai = arrays.find(key);
                if (ai != arrays.end()) keepArrays[key] = ai->second;
            }
        }
        if (merge) {
            if (delLo >= 0) {
                auto it = prog.lower_bound(delLo);
                while (it != prog.end() && it->first <= delHi) it = prog.erase(it);
            }
            mergeFromFile(f);
        } else {
            loadProgramFile(f);
        }
        clearVariables();
        vars = std::move(keepVars);
        arrays = std::move(keepArrays);
        errCode = errLine = 0;
        invalidateCont();
        if (prog.empty()) throw EndSignal{};
        if (startLine >= 0) goLine(startLine);
        else goLine(prog.begin()->first);
    }

    void stmtAuto() {
        autoNext = 10;
        autoInc = 10;
        if (cur().t == Token::Num) autoNext = wantLineNum();
        else if (isPun(".")) { adv(); autoNext = std::max(0, lastUsedLine); }
        if (eatPun(",")) {
            if (cur().t == Token::Num) autoInc = wantLineNum();
        }
        if (autoInc < 1) err(5);
        autoOn = true;
    }

    void stmtEdit() {
        int n;
        if (isPun(".")) { adv(); n = lastUsedLine; }
        else n = wantLineNum();
        if (!prog.count(n)) err(8);
        lastUsedLine = n;
        pendingEdit = n;
    }

    void stmtShell() {
        string cmd;
        if (!atStmtEnd()) cmd = evalStr();
        if (cmd.empty()) err(5);
        CursesScreen* cs = dynamic_cast<CursesScreen*>(&scr);
        if (cs) {
            def_prog_mode();
            endwin();
            int rc = system(cmd.c_str());
            (void)rc;
            reset_prog_mode();
            refresh();
        } else {
            int rc = system(cmd.c_str());
            (void)rc;
        }
    }

    // =======================================================================
    // RENUM
    // =======================================================================
    void stmtRenum() {
        int newStart = 10, oldStart = 0, inc = 10;
        if (cur().t == Token::Num) newStart = wantLineNum();
        if (eatPun(",")) {
            if (cur().t == Token::Num) oldStart = wantLineNum();
            if (eatPun(",")) {
                if (cur().t == Token::Num) inc = wantLineNum();
            }
        }
        if (inc < 1) err(5);
        // build the old->new map
        std::map<int, int> remap;
        int next = newStart;
        for (auto& kv : prog) {
            if (kv.first >= oldStart) {
                if (next > 65529) err(5);
                remap[kv.first] = next;
                next += inc;
            } else {
                remap[kv.first] = kv.first;
                if (kv.first >= newStart) err(5);  // would reorder lines
            }
        }
        // rewrite line-number references in every line
        static const std::set<Kw> refKws = {
            Kw::GOTO, Kw::GOSUB, Kw::THEN, Kw::ELSE, Kw::RESTORE,
            Kw::RESUME, Kw::RETURN, Kw::RUN, Kw::LIST, Kw::DELETE, Kw::EDIT,
        };
        std::map<int, ProgLine> renumbered;
        for (auto& kv : prog) {
            string text = kv.second.text;
            std::vector<Token> toks = tokenize(text);
            struct Splice { int pos, len; string repl; };
            std::vector<Splice> splices;
            for (size_t i = 0; i < toks.size(); i++) {
                if (toks[i].t != Token::Key || !refKws.count(toks[i].kw)) continue;
                size_t j = i + 1;
                while (j < toks.size() && toks[j].t == Token::Num) {
                    int target = (int)toks[j].n;
                    if (toks[j].n == floor(toks[j].n) && target >= 0 && target <= 65529) {
                        auto mi = remap.find(target);
                        if (mi != remap.end()) {
                            if (mi->second != target)
                                splices.push_back({toks[j].pos, toks[j].len,
                                                   std::to_string(mi->second)});
                        } else {
                            outLine("Undefined line " + std::to_string(target) +
                                    " in " + std::to_string(kv.first));
                        }
                    }
                    j++;
                    if (j + 1 < toks.size() && toks[j].t == Token::Pun &&
                        toks[j].s == "," && toks[j + 1].t == Token::Num) j++;
                    else break;
                }
            }
            for (size_t k = splices.size(); k-- > 0;)
                text.replace(splices[k].pos, splices[k].len, splices[k].repl);
            ProgLine pl;
            pl.text = text;
            renumbered[remap[kv.first]] = std::move(pl);
        }
        prog = std::move(renumbered);
        dataPc = Pc{-2, 0};
        invalidateCont();
        if (curLine != -1) throw EndSignal{};   // RENUM stops a running program
    }

    // =======================================================================
    // PRINT USING
    // =======================================================================
    struct UseField {
        bool found = false;
        bool isStr = false;
        char strKind = 0;       // '!', '&', '\\'
        int strLen = 0;
        int before = 0, commas = 0, after = 0;
        bool dot = false, dollar = false, star = false, expo = false;
        int expDigits = 2;      // 2 for ^^^^, 3 for ^^^^^
        int sign = 0;           // 0 none, 1 leading +, 2 trailing +, 3 trailing -
    };

    // Scan fmt from *fp: append literal chars to lit until a field starts,
    // then parse the field.  Returns field.found=false if none until the end.
    UseField nextUseField(const string& fmt, size_t& fp, string& lit) {
        UseField f;
        const size_t n = fmt.size();
        while (fp < n) {
            char c = fmt[fp];
            if (c == '_') {                        // literal escape
                fp++;
                if (fp < n) lit += fmt[fp++];
                continue;
            }
            if (c == '!') {
                fp++;
                f.found = true; f.isStr = true; f.strKind = '!'; f.strLen = 1;
                return f;
            }
            if (c == '&') {
                fp++;
                f.found = true; f.isStr = true; f.strKind = '&';
                return f;
            }
            if (c == '\\') {
                size_t j = fp + 1;
                int spaces = 0;
                while (j < n && fmt[j] == ' ') { spaces++; j++; }
                if (j < n && fmt[j] == '\\') {
                    fp = j + 1;
                    f.found = true; f.isStr = true; f.strKind = '\\';
                    f.strLen = spaces + 2;
                    return f;
                }
                lit += c;
                fp++;
                continue;
            }
            bool numStart = false;
            size_t save = fp;
            if (c == '+') {
                size_t j = fp + 1;
                if (j < n && (fmt[j] == '#' || fmt[j] == '.' ||
                              (fmt[j] == '$' && j + 1 < n && fmt[j + 1] == '$') ||
                              (fmt[j] == '*' && j + 1 < n && fmt[j + 1] == '*'))) {
                    f.sign = 1;
                    fp = j;
                    numStart = true;
                }
            }
            if (!numStart && c == '$' && fp + 1 < n && fmt[fp + 1] == '$') {
                f.dollar = true;
                f.before += 2;
                fp += 2;
                numStart = true;
            } else if (!numStart && c == '*' && fp + 1 < n && fmt[fp + 1] == '*') {
                f.star = true;
                f.before += 2;
                fp += 2;
                if (fp < n && fmt[fp] == '$') { f.dollar = true; f.before++; fp++; }
                numStart = true;
            } else if (!numStart && c == '#') {
                numStart = true;
            } else if (!numStart && c == '.' && fp + 1 < n && fmt[fp + 1] == '#') {
                numStart = true;
            }
            if (numStart) {
                if (f.sign == 1 && fmt[fp] == '$' && fp + 1 < n && fmt[fp + 1] == '$') {
                    f.dollar = true;
                    f.before += 2;
                    fp += 2;
                } else if (f.sign == 1 && fmt[fp] == '*' && fp + 1 < n && fmt[fp + 1] == '*') {
                    f.star = true;
                    f.before += 2;
                    fp += 2;
                    if (fp < n && fmt[fp] == '$') { f.dollar = true; f.before++; fp++; }
                }
                while (fp < n && (fmt[fp] == '#' || fmt[fp] == ',')) {
                    if (fmt[fp] == '#') f.before++;
                    else f.commas++;
                    fp++;
                }
                if (fp < n && fmt[fp] == '.') {
                    f.dot = true;
                    fp++;
                    while (fp < n && fmt[fp] == '#') { f.after++; fp++; }
                }
                if (f.before + f.after == 0) {     // lone '.' was a literal
                    fp = save;
                    lit += fmt[fp++];
                    continue;
                }
                int carets = 0;
                while (fp < n && fmt[fp] == '^' && carets < 5) { carets++; fp++; }
                if (carets >= 4) {
                    f.expo = true;
                    f.expDigits = (carets == 5) ? 3 : 2;
                } else {
                    fp -= carets;
                }
                if (f.sign != 1 && fp < n && fmt[fp] == '+') { f.sign = 2; fp++; }
                else if (fp < n && fmt[fp] == '-') { f.sign = 3; fp++; }
                if (f.before + f.after > 24) err(5);    // GW's field-width cap
                f.found = true;
                return f;
            }
            lit += c;
            fp++;
        }
        return f;
    }

    static string commify(const string& digits, bool useCommas) {
        if (!useCommas) return digits;
        string out;
        int count = 0;
        for (size_t i = digits.size(); i-- > 0;) {
            out += digits[i];
            if (++count % 3 == 0 && i > 0) out += ',';
        }
        std::reverse(out.begin(), out.end());
        return out;
    }

    // |x| rounded to 'after' decimals — half away from zero, like GW.
    static void roundFixed(double ax, int after, string& ip, string& fp) {
        double scale = pow(10.0, after);
        double scaled = ax * scale;
        char b[64];
        if (scaled < 9e15) {
            snprintf(b, sizeof b, "%.0f", floor(scaled + 0.5));
            string s = b;
            if ((int)s.size() <= after) s = string(after + 1 - s.size(), '0') + s;
            ip = s.substr(0, s.size() - after);
            fp = s.substr(s.size() - after);
        } else {
            snprintf(b, sizeof b, "%.*f", after, ax);
            string s = b;
            size_t d = s.find('.');
            if (d == string::npos) { ip = s; fp = string(after, '0'); }
            else { ip = s.substr(0, d); fp = s.substr(d + 1); }
        }
    }

    string usingNumber(const UseField& f, const Value& v) {
        if (v.isStr()) err(13);
        double x = v.n;
        bool neg = x < 0;
        double ax = fabs(x);
        if (f.expo) {
            // Unless a sign is specified, GW reserves one digit position to
            // the left for the sign, so "##.##^^^^"; 234.56 -> " 2.35E+02".
            int digitsLeft = (f.sign == 0) ? f.before - 1 : f.before;
            if (digitsLeft < 0) digitsLeft = 0;
            int exp = 0;
            double mant = 0;
            if (ax > 0) {
                exp = (int)floor(log10(ax)) - digitsLeft + 1;
                mant = ax / pow(10.0, exp);
                char tb[64];
                snprintf(tb, sizeof tb, "%.*f", f.after, mant);
                double lim = (digitsLeft == 0) ? 1.0 : pow(10.0, digitsLeft);
                if (atof(tb) >= lim) { exp++; mant = ax / pow(10.0, exp); }
            }
            char mb[64];
            snprintf(mb, sizeof mb, "%.*f", f.after, mant);
            string m = mb;
            if (digitsLeft == 0 && !m.empty() && m[0] == '0') m = m.substr(1);
            string lead;
            if (f.sign == 0) lead = neg ? "-" : " ";
            else if (f.sign == 1) lead = neg ? "-" : "+";
            char eb[16];
            snprintf(eb, sizeof eb, "E%+0*d", (f.expDigits == 3) ? 4 : 3, exp);
            string outv = lead + m + eb;
            if (f.sign == 2) outv += neg ? "-" : "+";
            else if (f.sign == 3) outv += neg ? "-" : " ";
            return outv;
        }
        string ip, fp_;
        roundFixed(ax, f.after, ip, fp_);
        bool ipZero = ip.find_first_not_of('0') == string::npos;
        // With no digit position left of the point, the 0 is not printed.
        string ipShow = (f.before == 0 && ipZero) ? "" : ip;
        string lead;
        if (f.sign == 1) lead = neg ? "-" : "+";
        else if (f.sign == 0 && neg) lead = "-";
        string tail;
        if (f.dot) tail = "." + fp_;
        if (f.sign == 2) tail += neg ? "-" : "+";
        else if (f.sign == 3) tail += neg ? "-" : " ";
        string body = lead + (f.dollar ? "$" : "") + commify(ipShow, f.commas > 0);
        int capacity = f.before + f.commas + (f.sign == 1 ? 1 : 0);
        if ((int)body.size() > capacity) {
            // Overflow: % then the value, keeping sign, $ and comma grouping.
            string ov = "%" + lead + (f.dollar ? "$" : "") +
                        commify(ip, f.commas > 0);
            return ov + tail;
        }
        string pad((size_t)(capacity - body.size()), f.star ? '*' : ' ');
        return pad + body + tail;
    }

    string usingString(const UseField& f, const Value& v) {
        if (!v.isStr()) err(13);
        if (f.strKind == '&') return v.s;
        if (f.strKind == '!') return v.s.empty() ? " " : v.s.substr(0, 1);
        string s = v.s.substr(0, f.strLen);
        if ((int)s.size() < f.strLen) s.append(f.strLen - s.size(), ' ');
        return s;
    }

    void printUsing(Sink& sink, bool& pendingNl) {
        string fmt = evalStr();
        if (fmt.empty()) err(5);
        wantPun(";");
        size_t fp = 0;
        bool anyField = false;
        while (true) {
            Value v = evalExpr();
            string lit;
            UseField f = nextUseField(fmt, fp, lit);
            if (!f.found) {
                sink.text(lit);
                fp = 0;
                lit.clear();
                f = nextUseField(fmt, fp, lit);
                if (!f.found) err(5);              // no fields in format at all
            }
            anyField = true;
            sink.text(lit);
            sink.text(f.isStr ? usingString(f, v) : usingNumber(f, v));
            pendingNl = true;
            if (eatPun(";") || eatPun(",")) {
                if (atStmtEnd()) { pendingNl = false; break; }
                continue;
            }
            if (atStmtEnd()) break;
            err(2);
        }
        (void)anyField;
        // trailing literal text up to the next field (or end of format)
        string lit;
        size_t save = fp;
        UseField f = nextUseField(fmt, fp, lit);
        sink.text(lit);
        if (f.found) fp = save;
        scr.flush();
    }

    // =======================================================================
    // Execution engine
    // =======================================================================
    void execLoop() {
        while (true) {
            if (!T || ti >= T->size()) {
                if (!advanceLine()) {
                    if (inHandler && curLine != -1) {
                        inHandler = false;       // ran off the end mid-handler
                        throw BasicError(19);    // No RESUME
                    }
                    return;
                }
                continue;
            }
            const Token& t = cur();
            if (t.t == Token::Pun && t.s == ":") { adv(); continue; }
            if (t.t == Token::Rem) { ti = T->size(); continue; }
            if (t.t == Token::Key && t.kw == Kw::ELSE) { ti = T->size(); continue; }
            try {
                statement();
            } catch (BasicError& e) {
                if (!takeErrorHandler(e.code)) throw;
            }
        }
    }

    // Dispatch to an ON ERROR handler if one is armed.
    bool takeErrorHandler(int code) {
        errCode = code;
        errLine = (curLine == -1) ? -1 : curLine;
        if (errHandlerLine > 0 && !inHandler && prog.count(errHandlerLine)) {
            inHandler = true;
            errPc = stmtBegin;
            Pc p;
            p.line = errHandlerLine;
            p.ti = 0;
            go(p);
            return true;
        }
        return false;
    }

    void printError(int code, int line) {
        freshLine();
        string msg = errMessage(code);
        if (line >= 0) msg += " in " + std::to_string(line);
        outLine(msg);
        scr.flush();
    }

    // Execute one immediate line; prints the standard messages itself.
    void runDirect(const string& src) {
        directToks = tokenize(src);
        Pc p;
        p.line = -1;
        p.ti = 0;
        try {
            go(p);
            execLoop();
            if (curLine != -1) closeAllFiles();   // a program ran to its end
        } catch (BasicError& e) {
            errCode = e.code;
            errLine = (curLine == -1) ? -1 : curLine;
            inHandler = false;
            printError(e.code, errLine);
        } catch (BreakSignal& bs) {
            freshLine();
            if (bs.line >= 0) {
                contPc = stmtBegin;
                contValid = true;
                outLine("Break in " + std::to_string(bs.line));
            } else {
                outLine("Break");
            }
        } catch (StopSignal& ss) {
            freshLine();
            if (ss.line >= 0) outLine("Break in " + std::to_string(ss.line));
            else outLine("Break");
        } catch (EndSignal&) {
        }
        scr.flush();
    }

    // Run the loaded program start to finish (batch -r mode).
    int runBatch() {
        try {
            startRun(-1);
            execLoop();
            closeAllFiles();
            return 0;
        } catch (BasicError& e) {
            printError(e.code, curLine == -1 ? -1 : curLine);
            closeAllFiles();
            return 1;
        } catch (BreakSignal& bs) {
            freshLine();
            outLine(bs.line >= 0 ? "Break in " + std::to_string(bs.line) : "Break");
            closeAllFiles();
            return 1;
        } catch (StopSignal& ss) {
            freshLine();
            if (ss.line >= 0) outLine("Break in " + std::to_string(ss.line));
            closeAllFiles();
            return 0;
        } catch (EndSignal&) {
            closeAllFiles();
            return 0;
        }
    }

    // One typed line: numbered lines go to the program, the rest executes.
    // Returns true when "Ok" should follow.
    bool handleLine(const string& rawIn) {
        string t = ltrim(rtrim(rawIn));
        if (t.empty()) return false;
        size_t i = 0;
        while (i < t.size() && isdigit((unsigned char)t[i])) i++;
        if (i > 0 && i <= 5) {
            long n = atol(t.substr(0, i).c_str());
            if (n > 65529) {                     // out of range: Syntax error
                printError(2, -1);
                return true;
            }
            string rest = t.substr(i);
            if (!rest.empty() && rest[0] == ' ') rest = rest.substr(1);
            try {
                storeLine((int)n, rest);
            } catch (BasicError& e) {
                printError(e.code, -1);
                return true;
            }
            return false;
        }
        runDirect(rawIn);
        return true;
    }
};  // struct Basic

// ---------------------------------------------------------------------------
// The C64-style full-screen editor: the screen itself is the line buffer.
// Type anywhere; Enter grabs the logical line under the cursor and either
// stores it (when it starts with a line number) or executes it immediately.
// ---------------------------------------------------------------------------

struct Editor {
    CursesScreen& scr;
    Basic& b;
    bool insertMode = false;

    Editor(CursesScreen& s, Basic& bb) : scr(s), b(bb) {}

    void banner() {
        scr.write("GW-BASIC style interpreter  (single-file C++ remake)\n");
        scr.write("Type your program with line numbers; commands run directly.\n");
        scr.write("RUN LIST SAVE\"F\" LOAD\"F\" NEW SYSTEM - F1..F10 are soft keys\n");
        scr.write("60300 Bytes free\n");
        scr.write("Ok\n");
        scr.flush();
    }

    // Find top/bottom rows of the logical line containing 1-based row r.
    void logicalSpan(int r, int& top, int& bot) {
        top = r;
        while (top > 1 && scr.wrapped[top - 1]) top--;
        bot = r;
        while (bot < scr.H && scr.wrapped[bot]) bot++;
    }

    string grabLogicalLine() {
        int top, bot;
        logicalSpan(scr.row(), top, bot);
        // Rows wrap at the logical width, which can be narrower than the
        // physical row (WIDTH 40), so only take widthLimit columns of each.
        int w = scr.widthLimit;
        string s;
        for (int i = top; i <= bot; i++) s += scr.shadow[i - 1].substr(0, w);
        return rtrim(s);
    }

    // AUTO prompt: number plus '*' when the line already exists (cursor sits
    // on the asterisk so typing replaces it, GW style).
    void autoPrompt() {
        scr.write(std::to_string(b.autoNext));
        if (b.prog.count(b.autoNext)) {
            int r = scr.row(), c = scr.col();
            scr.write("*");
            scr.locate(r, c);
        } else {
            scr.write(" ");
        }
    }

    void commitLine() {
        insertMode = false;                        // Enter cancels insert mode
        string line = grabLogicalLine();
        int top, bot;
        logicalSpan(scr.row(), top, bot);
        scr.locate(bot, 1);
        scr.write("\n");
        if (b.autoOn) {
            // Enter on an untouched prompt: keep an existing line and move
            // on; on a new line number it ends AUTO.
            string t = trim(line);
            string p1 = std::to_string(b.autoNext);
            if (t == p1 || t == p1 + "*") {
                if (b.prog.count(b.autoNext)) {
                    b.autoNext += b.autoInc;
                    autoPrompt();
                } else {
                    b.autoOn = false;
                }
                scr.flush();
                return;
            }
        }
        bool wasAuto = b.autoOn;
        bool ok = b.handleLine(line);
        if (b.exitRequested) return;
        if (ok) {
            if (wasAuto) b.autoOn = false;         // a command ends AUTO mode
            // EDIT and a fresh AUTO show their output without an Ok first
            if (b.pendingEdit < 0 && !(b.autoOn && !wasAuto)) scr.write("Ok\n");
        }
        if (b.pendingEdit >= 0) {
            doEdit();
        } else if (b.autoOn) {
            string lt = ltrim(line);
            size_t i = 0;
            while (i < lt.size() && isdigit((unsigned char)lt[i])) i++;
            if (i > 0) b.autoNext = atoi(lt.substr(0, i).c_str()) + b.autoInc;
            autoPrompt();
        }
        scr.flush();
    }

    void doEdit() {
        int n = b.pendingEdit;
        b.pendingEdit = -1;
        auto it = b.prog.find(n);
        if (it == b.prog.end()) return;
        if (scr.col() != 1) scr.write("\n");
        scr.write(std::to_string(n) + " " + it->second.text);
        int top, bot;
        logicalSpan(scr.row(), top, bot);
        scr.locate(top, 1);
        scr.flush();
    }

    void redrawRow(int r) {
        const string& row = scr.shadow[r - 1];
        for (int c = 1; c <= scr.W; c++)
            scr.putCell(r, c, (unsigned char)row[c - 1]);
    }

    void typeChar(unsigned char c) {
        int r = scr.row(), col = scr.col();
        if (insertMode) {
            string& row = scr.shadow[r - 1];
            row.insert((size_t)(col - 1), 1, (char)c);
            row.resize(scr.W, ' ');
            redrawRow(r);
            scr.locate(r, std::min(col + 1, scr.W));
        } else {
            scr.putByte(c);
        }
    }

    void deleteAtCursor() {
        int r = scr.row(), col = scr.col();
        string& row = scr.shadow[r - 1];
        row.erase((size_t)(col - 1), 1);
        row.push_back(' ');
        redrawRow(r);
        scr.locate(r, col);
    }

    void eraseLogicalLine() {
        int top, bot;
        logicalSpan(scr.row(), top, bot);
        for (int i = top; i <= bot; i++) {
            scr.shadow[i - 1].assign(scr.W, ' ');
            scr.wrapped[i - 1] = 0;
            redrawRow(i);
        }
        scr.locate(top, 1);
    }

    void pasteMacro(const string& m) {
        for (char c : m) {
            if (c == '\r') commitLine();
            else if ((unsigned char)c >= 32) typeChar((unsigned char)c);
        }
    }

    void handleKey(const KeyEvent& e) {
        int r = scr.row(), c = scr.col();
        if (e.ch == 13) { commitLine(); return; }
        if (e.ch == 3) {                            // Ctrl-C
            b.autoOn = false;
            scr.write("\n");
            scr.write("Ok\n");
            return;
        }
        if (e.ch == 27) { eraseLogicalLine(); return; }      // ESC clears line
        if (e.ch == 12) { scr.cls(); return; }               // Ctrl-L
        if (e.ch == 8 || e.ch == 127) {
            if (c > 1) {
                scr.locate(r, c - 1);
                deleteAtCursor();
            }
            return;
        }
        if (e.ch == 9) {
            do { typeChar(' '); } while ((scr.col() - 1) % 8 != 0 && scr.col() < scr.W);
            return;
        }
        if (e.ch == 0) {
            switch (e.scan) {
                case 72: if (r > 1) scr.locate(r - 1, c); break;
                case 80:
                    if (r < scr.H) scr.locate(r + 1, c);
                    else { scr.scrollUp(); scr.locate(r, c); }
                    break;
                case 75:
                    if (c > 1) scr.locate(r, c - 1);
                    else if (r > 1) scr.locate(r - 1, scr.W);
                    break;
                case 77:
                    if (c < scr.W) scr.locate(r, c + 1);
                    else if (r < scr.H) scr.locate(r + 1, 1);
                    break;
                case 71: scr.locate(1, 1); break;
                case 79: {                          // End: end of logical text
                    int top, bot;
                    logicalSpan(r, top, bot);
                    int lr = bot, lc = 1;
                    for (int i = bot; i >= top; i--) {
                        size_t e2 = scr.shadow[i - 1].find_last_not_of(' ');
                        if (e2 != string::npos) { lr = i; lc = (int)e2 + 2; break; }
                        lr = i;
                        lc = 1;
                    }
                    scr.locate(lr, std::min(lc, scr.W));
                    break;
                }
                case 82: insertMode = !insertMode; break;
                case 83: deleteAtCursor(); break;
                default:
                    if (e.scan >= 59 && e.scan <= 68)
                        pasteMacro(b.keyMacros[e.scan - 59]);
                    break;
            }
            scr.flush();
            return;
        }
        if (e.ch >= 32 && e.ch < 256) {
            typeChar((unsigned char)e.ch);
            scr.flush();
        }
    }

    void run() {
        banner();
        while (!b.exitRequested) {
            scr.flush();
            KeyEvent e = scr.waitKey();
            handleKey(e);
        }
    }
};

// ---------------------------------------------------------------------------
// Plain line-by-line REPL for piped stdin (testing, scripting).
// ---------------------------------------------------------------------------

static int replLoop(Basic& b) {
    string line;
    while (!b.exitRequested && std::getline(std::cin, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (b.autoOn) {
            // AUTO numbers incoming lines until a blank line ends it
            if (trim(line).empty()) {
                b.autoOn = false;
                continue;
            }
            b.handleLine(std::to_string(b.autoNext) + " " + line);
            b.autoNext += b.autoInc;
            fflush(stdout);
            continue;
        }
        bool ok = b.handleLine(line);
        if (b.exitRequested) break;
        if (ok) fputs("Ok\n", stdout);
        b.pendingEdit = -1;
        fflush(stdout);
    }
    return 0;
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

static void usage(const char* prog) {
    fprintf(stderr,
            "Usage: %s [-r] [file.bas]\n"
            "  (no args)    full-screen editor\n"
            "  file.bas     editor with the program loaded\n"
            "  -r file.bas  run the program without curses and exit\n",
            prog);
}

int main(int argc, char** argv) {
    setlocale(LC_ALL, "");
    bool batch = false;
    string file;
    for (int i = 1; i < argc; i++) {
        string a = argv[i];
        if (a == "-r" || a == "--run") batch = true;
        else if (a == "-h" || a == "--help") { usage(argv[0]); return 0; }
        else if (!a.empty() && a[0] == '-') { usage(argv[0]); return 2; }
        else file = a;
    }
    signal(SIGINT, sigintHandler);

    if (batch) {
        if (file.empty()) { usage(argv[0]); return 2; }
        PlainScreen scr;
        Basic b(scr, false);
        try {
            b.loadProgramFile(file);
        } catch (BasicError& e) {
            fprintf(stderr, "%s: %s\n", file.c_str(), errMessage(e.code).c_str());
            return 1;
        }
        return b.runBatch();
    }

    if (!isatty(0) || !isatty(1)) {
        PlainScreen scr;
        Basic b(scr, false);
        if (!file.empty()) {
            try {
                b.loadProgramFile(file);
            } catch (BasicError& e) {
                fprintf(stderr, "%s: %s\n", file.c_str(), errMessage(e.code).c_str());
                return 1;
            }
        }
        return replLoop(b);
    }

    CursesScreen scr;
    Basic b(scr, true);
    if (!file.empty()) {
        try {
            b.loadProgramFile(file);
        } catch (BasicError& e) {
            scr.write(file + ": " + errMessage(e.code) + "\n");
        }
    }
    Editor ed(scr, b);
    ed.run();
    return 0;
}
