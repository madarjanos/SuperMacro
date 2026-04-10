#ifndef MACRO_H
#define MACRO_H

/* Limits — also defined internally in macro.c; kept here for callers that
 * need to know the valid ranges (e.g. to validate script arguments). */
#define MAX_VARS	100	/* variable indices $0 .. $99         */
#define MAX_SUBS	100	/* subroutine indices 0 .. 100           */

/*
 * MacroInit — initialise all module-level state.
 * Call once at program startup before LoadMacro / StepMacro.
 */
void MacroInit(void);

/*
 * MacroShutdown — release resources acquired by MacroInit (GDI+ etc.).
 * Call once at program exit.
 */
void MacroShutdown(void);

/*
 * LoadMacro — parse an array of text lines and build the macro instruction array.
 *
 *   lines      array of C strings, one per line
 *   lineCount  number of elements in lines[]
 */
void LoadMacro(const char **lines, int lineCount);

/*
 * StepMacro — execute the next pending macro instruction.
 *
 *   res / resSize  output buffer; WRITE, WRITECHAR and SCRGRAB write here.
 *   returns        1 while the macro is still running,
 *                  0 when finished or interrupted by the user (ESC).
 */
int StepMacro(char *res, int resSize);

#endif /* MACRO_H */
