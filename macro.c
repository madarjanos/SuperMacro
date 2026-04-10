/*
 * macro.c
 * Converted from original uMacro.pas Pascal file, then refactored.
 *
 * --- Macro script syntax ---
 *
 *  Parameters can be literal integers or variable references.
 *  A variable reference is written as $N, where N is the variable index (0 .. MAX_VARS-1).
 *
 *  Examples:
 *    MOVE 640 480      -- move mouse to pixel (640, 480)
 *    MOVE $0 $1        -- move mouse to coordinates stored in var 0 and var 1
 *    MOVE $0 480       -- x from var 0, y literal 480
 *    SET $0 100        -- set var 0 to 100
 *    SET $0 $1         -- copy var 1 into var 0
 *    ADD $0 10         -- add 10 to var 0
 *    ADD $0 $1         -- add var 1 to var 0
 *
 *  Loop (no slot index needed - stack-based):
 *    LOOP 100          -- repeat 100 times
 *     LOOP $3           -- repeat as many times as stored in var 3
 *      ...
 *      LOOP 5          -- nested loops work naturally
 *        ...
 *      ENDLOOP
 *     ENDLOOP
 *    ENDLOOP
 *
 *  Subroutine (stack-based - subs can call other subs):
 *    SUB 0
 *      ...
 *      CALL 1          -- call another sub from within a sub
 *    ENDSUB
 *    SUB 1
 *      ...
 *    ENDSUB
 *    ...
 *    CALL 0            -- call subroutine 0
 *    CALL $1           -- call subroutine whose index is stored in var 1
 */

#include "macro.h"
#include "pngsave.h"

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* --------------------------- LIMITS / CONSTANTS -------------------------- */

#define MAX_VARS	100	/* number of integer memory variables ($0 .. $99)  */
#define MAX_SUBS	100	/* maximum number of distinct subroutines (SUB 0..99) */
#define MAX_MACRO	4096	/* maximum number of instructions in one script     */
#define STACK_SIZE	MAX_MACRO /* shared execution stack depth                */

/* --------------------------- DATA TYPES ---------------------------------- */

/* One decoded macro instruction */
typedef struct {
	int	cmd;		/* command index into commands[]                          */
	int	param1;		/* first parameter (literal value, or variable index)     */
	int	param2;		/* second parameter (literal value, or variable index)    */
	int	param1_is_var;	/* non-zero: param1 is a variable reference ($N)          */
	int	param2_is_var;	/* non-zero: param2 is a variable reference ($N)          */
	char	*strparam;	/* string parameter (WRITE command); heap-allocated or NULL */
} TMacro;

/* --------------------------- GLOBAL VARIABLES ---------------------------- */

static BYTE	key_halt;		/* virtual key code used to interrupt execution  */
static RECT	scr_rect;		/* desktop dimensions for mouse coordinate scaling */

static TMacro	macro[MAX_MACRO];	/* instruction array                     */
static int	macro_len;		/* number of loaded instructions             */
static int	macro_ix;		/* index of next instruction to execute (-1 = stopped) */

static int	stack[STACK_SIZE];	/* shared execution stack (loops and sub calls)    */
static int	sp;			/* stack pointer: number of values currently on stack */

static int	sub_beginix[MAX_SUBS];	/* instruction index of each SUB (set at load time)   */
static int	sub_endix[MAX_SUBS];	/* instruction index of each ENDSUB (set at load time) */

static int	vars[MAX_VARS];		/* integer memory variables                  */

static BYTE	held_key        = 0;	/* last key pressed via KEYDOWN (0 = none)   */
static int	held_leftbutton = 0;	/* non-zero if left mouse button is held down */

static RECT	grabrct;		/* screen-capture region                         */
static int	grabix;			/* auto-incrementing file name counter           */
static int	grabbpp;		/* capture colour depth (1,4,8,15,16,24,32)      */

/* Command name table - indices must match the switch in StepMacro */
static const char *commands[] = {
	/* 0*/  "LOOP",
	/* 1*/  "ENDLOOP",
	/* 2*/  "WAIT",
	/* 3*/  "MOVE",
	/* 4*/  "LEFTDOWN",
	/* 5*/  "LEFTUP",
	/* 6*/  "KEYDOWN",
	/* 7*/  "KEYUP",
	/* 8*/  "CLICK",
	/* 9*/  "KEYPRESS",
	/*10*/  "KEYPRESS_CTRL",
	/*11*/  "GRABX1Y1",
	/*12*/  "GRABX2Y2",
	/*13*/  "GRABSTARTIX",
	/*14*/  "GRABBPP",
	/*15*/  "SCRGRAB",
	/*16*/  "GETMOUSE",
	/*17*/  "SUB",
	/*18*/  "ENDSUB",
	/*19*/  "CALL",
	/*20*/  "SET",
	/*21*/  "WRITE",
	/*22*/  "WRITECHAR",
	/*23*/  "READ",
	/*24*/  "ADD",
	/*25*/  "WRITEVAR",
	/*26*/  "WRITELN",
	/*27*/  "WRITELNVAR",
};
static const int NUM_COMMANDS = sizeof(commands) / sizeof(commands[0]);

/* ----------------------------- HELPERS ----------------------------------- */

/*
 * ResolveParam - return the effective integer value of a parameter.
 * If is_var is non-zero, value is a variable index and vars[value] is returned.
 */
static int ResolveParam(int value, int is_var)
{
	if (is_var) {
		if ((value >= 0) && (value < MAX_VARS))
			return vars[value];
		return 0;
	}
	return value;
}

/* ----------------------------- HOTKEY ------------------------------------ */

/* Returns non-zero if the user pressed the interrupt hotkey (ESC) */
static int UserInterrupt(void)
{
	if (GetAsyncKeyState(key_halt) != 0) {
		/* Release any keyboard key that is still held down */
		if (held_key != 0) {
			keybd_event(held_key, 0, KEYEVENTF_KEYUP, 0);
			held_key = 0;
		}
		/* Release the left mouse button if it is still held down */
		if (held_leftbutton) {
			mouse_event(MOUSEEVENTF_LEFTUP, 0, 0, 0, 0);
			held_leftbutton = 0;
		}
		macro_ix = -1;
		return 1;
	}
	return 0;
}

/* ----------------------- INSTRUCTION HANDLERS ---------------------------- */

/* SET $var literal_or_var  -  assign a value (or copy another variable) */
static void SetVar(int var_ix, int value, int value_is_var)
{
	if ((var_ix < 0) || (var_ix >= MAX_VARS))
		return;
	vars[var_ix] = ResolveParam(value, value_is_var);
}

/* ADD $var literal_or_var  -  add a value (or another variable) to a variable */
static void AddVar(int var_ix, int value, int value_is_var)
{
	if ((var_ix < 0) || (var_ix >= MAX_VARS))
		return;
	vars[var_ix] += ResolveParam(value, value_is_var);
}

/* WAIT ms  -  block for the given number of milliseconds (clamped to 10 s) */
static void Wait(int interval_msec)
{
	if (interval_msec < 0)     interval_msec = 0;
	if (interval_msec > 10000) interval_msec = 10000;
	Sleep((DWORD)interval_msec);
}

/* --- STACK HELPERS --- */

static void stack_push(int val)
{
	if (sp < STACK_SIZE)
		stack[sp++] = val;
	/* silently ignore stack overflow (script error) */
}

static int stack_pop(void)
{
	if (sp > 0)
		return stack[--sp];
	return -1; /* stack underflow */
}

/* --- LOOP --- */

/*
 * LOOP count  -  push the LOOP instruction position and iteration counter
 *                onto the shared stack; no slot index needed.
 */
static void LoopBegin(int count, int count_is_var)
{
	stack_push(macro_ix);                              /* return-to position      */
	stack_push(ResolveParam(count, count_is_var) - 1); /* remaining iterations    */
}

/*
 * ENDLOOP  -  check the top-of-stack counter:
 *   > 0  decrement and jump back to the LOOP instruction (StepMacro adds 1)
 *   <= 0 pop both values and fall through
 */
static void LoopEnd(void)
{
	if (sp < 2)
		return; /* stack underflow - script error */
	if (stack[sp - 1] > 0) {
		stack[sp - 1]--;
		macro_ix = stack[sp - 2]; /* StepMacro adds 1, landing after LOOP */
	} else {
		sp -= 2; /* loop finished - discard counter and saved position */
	}
}

/* --- SUBROUTINES --- */

/*
 * SUB id  -  if reached by fallthrough (not via CALL), skip the body.
 *             sub_endix[] is still needed for this; it is filled at load time.
 */
static void SubBegin(int subix)
{
	if ((subix < 0) || (subix >= MAX_SUBS))
		return;
	/* Jump to ENDSUB; StepMacro adds 1, landing after ENDSUB */
	macro_ix = sub_endix[subix];
}

/*
 * ENDSUB  -  pop the return address and jump back.
 *             No index needed; the stack records exactly where to return.
 *             Because CALL pushes one value and ENDSUB pops one, nested
 *             sub calls work naturally.
 */
static void SubEnd(void)
{
	macro_ix = stack_pop(); /* StepMacro adds 1, resuming after CALL */
}

/*
 * CALL id_or_var  -  push the return address onto the stack and jump into
 *                    the subroutine body (instruction after SUB).
 */
static void CallSub(int subix, int subix_is_var)
{
	subix = ResolveParam(subix, subix_is_var);
	if ((subix < 0) || (subix >= MAX_SUBS))
		return;
	/* Guard: SUB not defined in the loaded script - ignore silently */
	if (sub_beginix[subix] == -1)
		return;
	stack_push(macro_ix);          /* save return address (StepMacro adds 1 on return) */
	macro_ix = sub_beginix[subix]; /* StepMacro adds 1, landing on instruction after SUB */
}

/* --- MOUSE --- */

/* MOVE x y  -  move cursor to pixel coordinates (either literal or from vars) */
static void MouseMove(int x, int x_is_var, int y, int y_is_var)
{
	WORD wx = (WORD)ResolveParam(x, x_is_var);
	WORD wy = (WORD)ResolveParam(y, y_is_var);
	wx = (WORD)((65536.0 / scr_rect.right)  * wx);
	wy = (WORD)((65536.0 / scr_rect.bottom) * wy);
	mouse_event(MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_MOVE, wx, wy, 0, 0);
	Sleep(1);
}

/* LEFTDOWN  -  press the left mouse button */
static void MouseLeftDown(void)
{
	held_leftbutton = 1;
	mouse_event(MOUSEEVENTF_LEFTDOWN, 0, 0, 0, 0);
	Sleep(1);
}

/* LEFTUP  -  release the left mouse button */
static void MouseLeftUp(void)
{
	mouse_event(MOUSEEVENTF_LEFTUP, 0, 0, 0, 0);
	held_leftbutton = 0;
	Sleep(1);
}

/* CLICK x y  -  move to coordinates then left-click */
static void Click(int x, int x_is_var, int y, int y_is_var)
{
	MouseMove(x, x_is_var, y, y_is_var);
	MouseLeftDown();
	MouseLeftUp();
}

/*
 * GETMOUSE $varx $vary  -  store current cursor position in two variables.
 * Both parameters MUST be variable references.
 */
static void GetMouseXY(int var_ix1, int var_ix2)
{
	POINT pos;
	if ((var_ix1 < 0) || (var_ix1 >= MAX_VARS) ||
	    (var_ix2 < 0) || (var_ix2 >= MAX_VARS) ||
	    (var_ix1 == var_ix2))
		return;
	GetCursorPos(&pos);
	vars[var_ix1] = pos.x;
	vars[var_ix2] = pos.y;
}

/* --- KEYBOARD --- */

/* KEYDOWN vcode  -  press a key (virtual key code, literal or from var) */
static void KeyDown(int vcode, int vcode_is_var)
{
	held_key = (BYTE)ResolveParam(vcode, vcode_is_var);
	keybd_event(held_key, 0, 0, 0);
	Sleep(1);
}

/* KEYUP vcode  -  release a key */
static void KeyUp(int vcode, int vcode_is_var)
{
	keybd_event((BYTE)ResolveParam(vcode, vcode_is_var), 0, KEYEVENTF_KEYUP, 0);
	held_key = 0;
	Sleep(1);
}

/* KEYPRESS vcode  -  press and release a key */
static void KeyPress(int vcode, int vcode_is_var)
{
	KeyDown(vcode, vcode_is_var);
	KeyUp(vcode, vcode_is_var);
}

/* KEYPRESS_CTRL vcode  -  Ctrl+key combo */
static void KeyPress_Ctrl(int vcode, int vcode_is_var)
{
	/* Small delays around the key press prevent timing issues on older Windows */
	keybd_event(VK_LCONTROL, 0, 0, 0);
	Sleep(10);
	KeyDown(vcode, vcode_is_var);
	KeyUp(vcode, vcode_is_var);
	Sleep(10);
	keybd_event(VK_LCONTROL, 0, KEYEVENTF_KEYUP, 0);
}

/* --- CONSOLE I/O --- */

/*
 * WRITE <text>
 * Copies the string stored in strparam to the output buffer (res).
 */
static void WriteStr(const char *strparam, char *res, int resSize)
{
	if (!strparam || !res || resSize <= 0)
		return;
	snprintf(res, resSize, "%s", strparam);
}

/*
 * WRITECHAR value_or_var
 * Writes a single character (given by its code) to the output buffer.
 */
static void WriteChar(int value, int value_is_var, char *res, int resSize)
{
	char ch;
	if (!res || resSize < 2)
		return;
	ch     = (char)ResolveParam(value, value_is_var);
	res[0] = ch;
	res[1] = '\0';
}

/*
 * WRITEVAR $var1 [$var2]
 * Prints the value of one or two variables separated by a space.
 * param1 MUST be a variable reference; param2 is optional (omit or non-var is ignored).
 */
static void WriteVar(int var_ix1, int var_ix2, int has_var2, char *res, int resSize)
{
	if (!res || resSize <= 0)
		return;
	if (has_var2)
		snprintf(res, resSize, "%d %d", vars[var_ix1], vars[var_ix2]);
	else
		snprintf(res, resSize, "%d", vars[var_ix1]);
}

/* WRITELN <text> - same as WRITE but appends a newline */
static void WriteLn(const char *strparam, char *res, int resSize)
{
	if (!strparam || !res || resSize <= 0)
		return;
	snprintf(res, resSize, "%s\n", strparam);
}

/* WRITELNVAR $var1 [$var2] - same as WRITEVAR but appends a newline */
static void WriteLnVar(int var_ix1, int var_ix2, int has_var2, char *res, int resSize)
{
	if (!res || resSize <= 0)
		return;
	if (has_var2)
		snprintf(res, resSize, "%d %d\n", vars[var_ix1], vars[var_ix2]);
	else
		snprintf(res, resSize, "%d\n", vars[var_ix1]);
}

/*
 * Reads a line from stdin, converts it to an integer, stores it in a variable.
 * The parameter MUST be a variable reference.
 */
static void ReadVar(int var_ix)
{
	char buf[64];
	long val;
	char *endp;

	if ((var_ix < 0) || (var_ix >= MAX_VARS))
		return;
	if (fgets(buf, sizeof(buf), stdin) == NULL)
		return;
	val = strtol(buf, &endp, 10);
	if (endp != buf)
		vars[var_ix] = (int)val;
}

/* --- SCREEN CAPTURE --- */

static void GrabX1Y1(int x1, int y1)
{
	grabrct.left = x1;
	grabrct.top  = y1;
}

static void GrabX2Y2(int x2, int y2)
{
	grabrct.right  = x2;
	grabrct.bottom = y2;
}

static void GrabStartIx(int ix)
{
	grabix = ix - 1;
}

static void GrabSetBPP(int bpp)
{
	grabbpp = bpp;
}

/*
 * SCRGRAB
 * Captures the defined screen region and saves it as a numbered PNG file.
 * Writes the saved file path into res.
 */
static void ScrGrab(char *res, int resSize)
{
	int	w, h;
	char	fname[MAX_PATH];
	char	numstr[8];
	HDC	scrDC, memDC;
	HBITMAP	hBmp, hOldBmp;
	BYTE	*bits = NULL;
	char	cwd[MAX_PATH];

	w = grabrct.right  - grabrct.left; if (w < 16) w = 16;
	h = grabrct.bottom - grabrct.top;  if (h < 16) h = 16;

	/* Capture the screen region into a 24-bpp DIB */
	scrDC = GetDC(GetDesktopWindow());
	memDC = CreateCompatibleDC(scrDC);

	{
		BITMAPINFO bmi;
		memset(&bmi, 0, sizeof(bmi));
		bmi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
		bmi.bmiHeader.biWidth       = w;
		bmi.bmiHeader.biHeight      = -h;
		bmi.bmiHeader.biPlanes      = 1;
		bmi.bmiHeader.biBitCount    = 24;
		bmi.bmiHeader.biCompression = BI_RGB;
		hBmp = CreateDIBSection(memDC, &bmi, DIB_RGB_COLORS, (void **)&bits, NULL, 0);
	}

	hOldBmp = (HBITMAP)SelectObject(memDC, hBmp);
	BitBlt(memDC, 0, 0, w, h, scrDC, grabrct.left, grabrct.top, SRCCOPY);
	SelectObject(memDC, hOldBmp);
	ReleaseDC(GetDesktopWindow(), scrDC);
	DeleteDC(memDC);

	/* For 1-bpp mode: convert each pixel to pure black or white by luminance */
	if (grabbpp == 1 && bits != NULL) {
		int stride = ((w * 3 + 3) & ~3);
		int x, y;
		for (y = 0; y < h; y++) {
			BYTE *row = bits + y * stride;
			for (x = 0; x < w; x++) {
				DWORD c = (DWORD)row[x*3] + row[x*3+1] + row[x*3+2];
				BYTE  v = (c < 384) ? 0 : 255;
				row[x*3] = row[x*3+1] = row[x*3+2] = v;
			}
		}
	}

	/* Build the output file name, e.g. "001.png" */
	grabix++;
	snprintf(numstr, sizeof(numstr), "%d", grabix);
	while ((int)strlen(numstr) < 3)
		memmove(numstr + 1, numstr, strlen(numstr) + 1), numstr[0] = '0';
	snprintf(fname, sizeof(fname), "%s.png", numstr);

	/* Save as PNG via GDI+ */
	pngsave_hbitmap(fname, hBmp, w, h);

	DeleteObject(hBmp);

	GetCurrentDirectoryA(MAX_PATH, cwd);
	if (res)
		snprintf(res, resSize, "Save -> %s\\%s\n", cwd, fname);
}

/* ----------------------------- MACRO EXECUTION --------------------------- */

/*
 * StepMacro - execute the next pending macro instruction.
 *
 *   res / resSize  output buffer written by WRITE, WRITECHAR, SCRGRAB, etc.
 *   returns        1 while the macro is still running,
 *                  0 when finished or interrupted by the user (ESC).
 */
int StepMacro(char *res, int resSize)
{
	TMacro *m;

	if (res && resSize > 0)
		res[0] = '\0';

	if ((macro_ix < 0) || (macro_len < 1) || UserInterrupt())
		return 0;

	if (macro_ix >= macro_len) {
		macro_ix = -1;
		return 0;
	}

	m = &macro[macro_ix];

	switch (m->cmd) {
	case 0:  LoopBegin    (m->param1, m->param1_is_var);                          break;
	case 1:  LoopEnd      ();                                                      break;
	case 2:  Wait         (ResolveParam(m->param1, m->param1_is_var));             break;
	case 3:  MouseMove    (m->param1, m->param1_is_var, m->param2, m->param2_is_var); break;
	case 4:  MouseLeftDown();                                                      break;
	case 5:  MouseLeftUp  ();                                                      break;
	case 6:  KeyDown      (m->param1, m->param1_is_var);                          break;
	case 7:  KeyUp        (m->param1, m->param1_is_var);                          break;
	case 8:  Click        (m->param1, m->param1_is_var, m->param2, m->param2_is_var); break;
	case 9:  KeyPress     (m->param1, m->param1_is_var);                          break;
	case 10: KeyPress_Ctrl(m->param1, m->param1_is_var);                          break;
	case 11: GrabX1Y1    (ResolveParam(m->param1, m->param1_is_var),
	                      ResolveParam(m->param2, m->param2_is_var));              break;
	case 12: GrabX2Y2    (ResolveParam(m->param1, m->param1_is_var),
	                      ResolveParam(m->param2, m->param2_is_var));              break;
	case 13: GrabStartIx (ResolveParam(m->param1, m->param1_is_var));             break;
	case 14: GrabSetBPP  (ResolveParam(m->param1, m->param1_is_var));             break;
	case 15: ScrGrab     (res, resSize);                                           break;
	case 16: GetMouseXY  (m->param1, m->param2); /* both params MUST be var refs */break;
	case 17: SubBegin    (m->param1);                                              break;
	case 18: SubEnd      ();                                                       break;
	case 19: CallSub     (m->param1, m->param1_is_var);                           break;
	case 20: SetVar      (m->param1, m->param2, m->param2_is_var);                break;
	case 21: WriteStr    (m->strparam, res, resSize);                              break;
	case 22: WriteChar   (m->param1, m->param1_is_var, res, resSize);             break;
	case 23: ReadVar     (m->param1); /* param MUST be a var ref */                break;
	case 24: AddVar      (m->param1, m->param2, m->param2_is_var);                break;
	case 25: WriteVar    (m->param1, m->param2, m->param2_is_var, res, resSize);  break;
	case 26: WriteLn     (m->strparam, res, resSize);                             break;
	case 27: WriteLnVar  (m->param1, m->param2, m->param2_is_var, res, resSize);  break;
	}

	macro_ix++;
	return 1;
}

/* ----------------------- MACRO TEXT PARSING ------------------------------ */

/*
 * CutNextWord - extract the next whitespace-delimited token from s[*pos..sLen],
 *               advance *pos past it, and copy the token (upper-cased) into out.
 */
static void CutNextWord(const char *s, int *pos, int sLen, char *out, int outSize)
{
	int i = *pos, j, len;

	while (i < sLen && (s[i] == ' ' || s[i] == '\t')) i++;
	j = i;
	while (j < sLen && s[j] != ' ' && s[j] != '\t') j++;

	len = j - i;
	if (len >= outSize) len = outSize - 1;
	memcpy(out, s + i, len);
	out[len] = '\0';

	for (int n = 0; n < len; n++)
		out[n] = (char)toupper((unsigned char)out[n]);

	*pos = j;
}

/*
 * ParseParam - parse one token as a variable reference ($N) or a literal integer.
 *
 *   word         the token string (may start with '$')
 *   out_value    receives the parsed number or variable index
 *   out_is_var   set to 1 if token started with '$', 0 otherwise
 *   returns      1 if parsing succeeded, 0 if the token was empty or invalid
 */
static int ParseParam(const char *word, int *out_value, int *out_is_var)
{
	char *endp;
	long  lval;

	if (word[0] == '\0')
		return 0;

	if (word[0] == '$') {
		lval = strtol(word + 1, &endp, 10);
		if (endp == word + 1)
			return 0; /* '$' not followed by a number */
		*out_is_var = 1;
		*out_value  = (int)lval;
		return 1;
	}

	lval = strtol(word, &endp, 10);
	if (endp == word)
		return 0;
	*out_is_var = 0;
	*out_value  = (int)lval;
	return 1;
}

/*
 * LoadMacro - parse an array of text lines and build the macro instruction array.
 *
 *   lines      array of C strings, one per line
 *   lineCount  number of elements in lines[]
 */
void LoadMacro(const char **lines, int lineCount)
{
	int	i, k, pos, sLen;
	char	word[64];

	/* Free any string parameters left over from a previous load */
	for (i = 0; i < macro_len; i++) {
		if (macro[i].strparam) {
			free(macro[i].strparam);
			macro[i].strparam = NULL;
		}
	}

	macro_len = 0;
	memset(vars,        0,  sizeof(vars));
	memset(sub_beginix, -1, sizeof(sub_beginix)); /* -1 = not defined; checked at runtime */
	memset(sub_endix,   0,  sizeof(sub_endix));

	int current_sub = -1; /* index of the currently open SUB block (-1 = none) */
	int loop_depth  =  0; /* nesting depth of LOOP blocks                       */

	for (i = 0; i < lineCount && macro_len < MAX_MACRO; i++) {
		const char *line = lines[i];
		char	s[1024];
		int	p;
		TMacro	*m;

		if (!line || line[0] == '\0')
			continue;

		strncpy(s, line, sizeof(s) - 1);
		s[sizeof(s) - 1] = '\0';
		sLen = (int)strlen(s);
		pos  = 0;

		/* Skip comment lines (starting with ; or //) */
		p = 0;
		while (p < sLen && (s[p] == ' ' || s[p] == '\t')) p++;
		if (s[p] == ';' || (s[p] == '/' && p + 1 < sLen && s[p+1] == '/'))
			continue;

		/* Extract and look up the command name */
		CutNextWord(s, &pos, sLen, word, sizeof(word));
		for (k = 0; k < NUM_COMMANDS; k++)
			if (strcmp(commands[k], word) == 0) break;
		if (k >= NUM_COMMANDS)
			continue; /* unknown token - skip line */

		/* --- Load-time structural validation --- */
		if (k == 0 /* LOOP */) {
			loop_depth++;
		}
		if (k == 1 /* ENDLOOP */) {
			if (loop_depth == 0) {
				fprintf(stderr, "LoadMacro: line %d: ENDLOOP without matching LOOP\n", i + 1);
				exit(1);
			}
			loop_depth--;
		}
		if (k == 17 /* SUB */) {
			/* Parse the index now so we can include it in the error message */
			int	new_subix = 0;
			int	dummy_is_var = 0;
			char	tmpword[64];
			int	tmppos = pos;
			CutNextWord(s, &tmppos, sLen, tmpword, sizeof(tmpword));
			ParseParam(tmpword, &new_subix, &dummy_is_var);
			if (current_sub != -1) {
				fprintf(stderr, "LoadMacro: line %d: nested SUB definition (SUB %d inside SUB %d)\n",
				        i + 1, new_subix, current_sub);
				exit(1);
			}
		}
		if (k == 18 /* ENDSUB */) {
			if (current_sub == -1) {
				fprintf(stderr, "LoadMacro: line %d: ENDSUB without matching SUB\n", i + 1);
				exit(1);
			}
		}

		/* Initialise the new instruction slot */
		m = &macro[macro_len];
		m->cmd           = k;
		m->param1        = 0;
		m->param2        = 0;
		m->param1_is_var = 0;
		m->param2_is_var = 0;
		m->strparam      = NULL;

		if (k == 21 /* WRITE */ || k == 26 /* WRITELN */) {
			/* String argument: everything after the command word (skip one whitespace) */
			if (pos < sLen && (s[pos] == ' ' || s[pos] == '\t')) pos++;
			m->strparam = _strdup(s + pos);
		} else {
			/* Up to two generic parameters */
			CutNextWord(s, &pos, sLen, word, sizeof(word));
			ParseParam(word, &m->param1, &m->param1_is_var);

			CutNextWord(s, &pos, sLen, word, sizeof(word));
			ParseParam(word, &m->param2, &m->param2_is_var);
		}

		/* Record SUB / ENDSUB positions for fast CALL dispatch and fallthrough skip.
		 * ENDSUB carries no index in the script, so current_sub tracks the open SUB. */
		if (k == 17 /* SUB */) {
			current_sub = m->param1;
			if ((current_sub >= 0) && (current_sub < MAX_SUBS))
				sub_beginix[current_sub] = macro_len;
		}
		if (k == 18 /* ENDSUB */) {
			if ((current_sub >= 0) && (current_sub < MAX_SUBS))
				sub_endix[current_sub] = macro_len;
			current_sub = -1;
		}

		macro_len++;
	}

	if (loop_depth > 0) {
		fprintf(stderr, "LoadMacro: %d LOOP(s) without matching ENDLOOP\n", loop_depth);
		exit(1);
	}

	if (current_sub != -1) {
		fprintf(stderr, "LoadMacro: SUB %d has no matching ENDSUB\n", current_sub);
		exit(1);
	}

	sp       = 0;
	macro_ix = 0;
}

/* --------------------------------- INIT ---------------------------------- */

/*
 * MacroInit - initialise all module-level state.
 * Call once at program startup before LoadMacro / StepMacro.
 */
void MacroInit(void)
{
	macro_ix = -1;
	sp       = 0;

	key_halt = VK_ESCAPE;
	GetAsyncKeyState(key_halt); /* flush any pending key state */

	GetWindowRect(GetDesktopWindow(), &scr_rect);

	memset(vars,        0,  sizeof(vars));
	memset(sub_beginix, -1, sizeof(sub_beginix)); /* -1 = not defined */
	memset(sub_endix,   0,  sizeof(sub_endix));

	grabix        = 0;
	grabbpp       = 24;
	grabrct.left  = 0;   grabrct.top    = 0;
	grabrct.right = 640; grabrct.bottom = 480;

	pngsave_init();
}

/*
 * MacroShutdown - clean up resources allocated by MacroInit.
 * Call once at program exit.
 */
void MacroShutdown(void)
{
	pngsave_shutdown();
}
