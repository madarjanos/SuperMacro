/*
 * main.c
 * Loads a macro text file and runs it step by step.
 *
 * Usage:  macro.exe <macrofile.txt>
 */
 
#include "macro.h"
 
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
 
#define MAX_LINES    4096
#define MAX_LINE_LEN 1024
 
int main(int argc, char *argv[])
{
	FILE	*f;
	char	 buf[MAX_LINE_LEN];
	char	*lines[MAX_LINES];
	int	 lineCount = 0;
	char	 res[512];
 
	if (argc < 2) {
		fprintf(stderr, "Usage: %s <macrofile.txt>\n", argv[0]);
		return 1;
	}
 
	// Open the text file
	f = fopen(argv[1], "r");
	if (!f) {
		fprintf(stderr, "Error: cannot open file '%s'\n", argv[1]);
		return 1;
	}
 
	// Read lines into memory
	while (lineCount < MAX_LINES && fgets(buf, sizeof(buf), f)) {
		// Strip trailing newline / carriage return
		int len = (int)strlen(buf);
		while (len > 0 && (buf[len-1] == '\n' || buf[len-1] == '\r'))
			buf[--len] = '\0';
 
		lines[lineCount] = _strdup(buf);
		if (!lines[lineCount]) {
			fprintf(stderr, "Error: out of memory\n");
			fclose(f);
			return 1;
		}
		lineCount++;
	}
	fclose(f);
 
	printf("Loaded %d lines from '%s'\n", lineCount, argv[1]);
 
	// --- WARNING BLOCK ---
	printf("\n");
	printf("!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n");
	printf("!!!              EXTREMELY IMPORTANT WARNING                !!!\n");
	printf("!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n");
	printf("!!!\n");
	printf("!!!  You are about to run an AUTOMATED MACRO SCRIPT.\n");
	printf("!!!\n");
	printf("!!!  The macro can perform ANY action a human could do with\n");
	printf("!!!  a keyboard and mouse, including but not limited to:\n");
	printf("!!!\n");
	printf("!!!    - Deleting, moving or overwriting files and folders\n");
	printf("!!!    - Sending emails, messages or web requests\n");
	printf("!!!    - Installing or uninstalling software\n");
	printf("!!!    - Modifying system settings or the registry\n");
	printf("!!!    - Clicking through confirmation dialogs automatically\n");
	printf("!!!\n");
	printf("!!!  ONLY run macros from sources you fully trust.\n");
	printf("!!!  A malicious or buggy script can cause IRREVERSIBLE DAMAGE.\n");
	printf("!!!\n");
	printf("!!!  [STOP]  Press ESC at any time to abort execution.\n");
	printf("!!!          The script will halt within the current step.\n");
	printf("!!!\n");
	printf("!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n");
	printf("\n");
	printf("  Macro file : %s\n", argv[1]);
	printf("  Lines      : %d\n", lineCount);
	printf("\n");
	printf("Type  Y  and press Enter to confirm and start, anything else to abort: ");
	char answ[100];
	scanf("%99s", answ);
	// Flush the rest of the line (including the Enter) so READ commands
	// don't pick up this leftover newline as their first input.
	{ int c; while ((c = getchar()) != '\n' && c != EOF); }
	if (strcmp(answ, "Y") != 0) {
		printf("Aborted. No macro was executed.\n");
		return 0;
	}
 
	// Initialise and load the macro (after confirmation)
	MacroInit();
	LoadMacro((const char **)lines, lineCount);
 
	printf("\nStarting macro execution. Press ESC to stop at any time.\n\n");
 
	// Run the macro step by step
	while (StepMacro(res, sizeof(res))) 
	{
		// write info
		if (res[0] != '\0')
		{
			printf("%s", res);
			res[0] = '\0';
		}
		// a little sleep before next step
		Sleep(1);
	}
 
	printf("Macro finished.\n");

	MacroShutdown();

	// Free memory (loaded lines)
	for (int i = 0; i < lineCount; i++)
		free(lines[i]);
 
	return 0;
}