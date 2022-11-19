/*
   This file contains the source code for the EU2 modding tool Alun's Empire
   (Enhanced Modification of Province Information with Randomizing Events.)

   Copyright (C) 2006 alun

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License along
   with this program; if not, write to the Free Software Foundation, Inc.,
   51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <limits.h>

#define LOW_CHANCE_THRESHOLD	50 // if chance is below this threshold, create events with "convert" as the second option, not the first

#define MAX_TAGS                100
#define MAX_STRINGS             100
#define MAX_TAG_LENGTH          50
#define MAX_STRING_LENGTH       2048
#define MAX_EVENT_DATA          100
#define MAX_PROVINCES           3000
#define MAX_PROVINCENAME_LENGTH 100

// Defines for keyword tags.
#define TAG_FILE_ID         0
#define TAG_RNGC            1
#define TAG_EVENT_ID_PREFIX 2
#define TAG_OUTPUT_FILE     3
#define TAG_SET_STRING      4
#define TAG_TARGET_STRING   5
#define TAG_START_CONDITION 6
#define TAG_EVENT_DATA      7
#define TAG_MODIFICATION    8
#define TAG_END_OF_DATA     9
#define TAG_OUTPUT_FILE_MOD 10
#define TAG_OUTPUT_FILE_MOD_HEADER 11
#define TAG_FIRST_USER_TAG  12

static FILE *ifp, *ofp, *omfp;
static int LineNumber, NumErrors = 0, NumWarnings = 0;
static int TagIndex = TAG_FIRST_USER_TAG, StringIndex = 0;
static char TagArray[MAX_TAGS][MAX_TAG_LENGTH + 1]; // + 1 for null termination.
static char StringArray[MAX_STRINGS][MAX_STRING_LENGTH + 1]; // + 1 for null termination.
static char LatestString[MAX_STRING_LENGTH + 1]; // + 1 for null termination.

static int RNGCTag = INT_MAX;
static int EventIDPrefix = INT_MAX;
static int UserStringsIndexArray[MAX_STRINGS];
static int LargestProvinceID = 0;
static int EventData[MAX_EVENT_DATA][4]; // Tag, NameStr, DescStr, CommandStr
static int EventDataIndex = 0;
static char ProvinceEventIndex[MAX_EVENT_DATA][MAX_PROVINCES]; // The running event ID for each province.
static char ProvinceNames[MAX_PROVINCES][MAX_PROVINCENAME_LENGTH + 1]; // + 1 for null termination.
static char OutputFileModHeader[MAX_STRING_LENGTH + 1];

// Helper functions for file parsing.
// Externals used: FILE *ifp, int LineNumber, int NumErrors, int NumWarnings,
// char TagArray[][], int TagIndex, char LatestString[]

int IsWhitespace(int c)
{
    return(c >= 0 && c <= 32);
}

int GetChar()
{
    int c = fgetc(ifp);
    if ((char)c == '\n' || (char)c == '\r') {
        LineNumber++;
    }
    return(c);
}

void UnGetChar(int c)
{
    // Don't bother ungetting whitespace, will be scanned away next anyway
    // (ungetting newlines would screw up the line counting).
    if (!IsWhitespace(c)) {
        ungetc(c, ifp);
    }
}

void SkipRestOfLine()
{
    int c;

    do {
        c = GetChar();
    } while ((char)c != '\n' && (char)c != '\r' && c != EOF);
}

void SkipWhitespacesAndComments()
{
    int c = GetChar();

    while ((char)c == '#' || IsWhitespace(c)) {
        if ((char)c == '#') {
            SkipRestOfLine();
            c = GetChar();
        }
        while (IsWhitespace(c)) {
            c = GetChar();
        }
    }
    UnGetChar(c);
}

void Error(char *s, int c)
{
    if (c == 0) {
        // Semantic, rather than syntax error, only print the message.
        fprintf(stderr, "Error: line %d: %s\n", LineNumber, s);
    } else {
        // Syntax error, print the offending character as well as the message.
        if (c == EOF) {
            fprintf(stderr, "Error: line %d: %s, found EOF\n", LineNumber, s);
        } else {
            fprintf(stderr, "Error: line %d: %s, found '%c'\n", LineNumber, s, (char)c);
        }
    }
    NumErrors++;
}

void Warning(char *s, int c)
{
    if (c == 0) {
        // Semantic, rather than syntax warning, only print the message.
        fprintf(stderr, "Warning: line %d: %s\n", LineNumber, s);
    } else {
        // Syntax warning, print the offending character as well as the message.
        if (c == EOF) {
            fprintf(stderr, "Warning: line %d: %s, found EOF\n", LineNumber, s);
        } else {
            fprintf(stderr, "Warning: line %d: %s, found '%c'\n", LineNumber, s, c);
        }
    }
    NumWarnings++;
}

void VerifyListStart()
{
    int c;

    SkipWhitespacesAndComments();
    c = GetChar();
    if ((char)c != '(') {
        Error("expected '('", c);
        UnGetChar(c);
        return;
    }
    return;
}

void VerifyListEnd()
{
    int c;

    SkipWhitespacesAndComments();
    c = GetChar();
    if ((char)c != ')') {
        Error("expected ')'", c);
        UnGetChar(c);
        return;
    }
    return;
}

int GetNum()
{
    int Num, c;

    SkipWhitespacesAndComments();
    if (fscanf(ifp, "%d", &Num) != 1) {
        c = GetChar();
        Error("expected a number", c);
        UnGetChar(c);
        return(INT_MAX);
    }
    return(Num);
}

int GetDate()
{
    int y, m, d, c;

    y = GetNum();
    c = GetChar();
    if (c != '-') {
        Error("expected '-'", c);
        UnGetChar(c);
    }
    m = GetNum();
    c = GetChar();
    if (c != '-') {
        Error("expected '-'", c);
        UnGetChar(c);
    }
    d = GetNum();
    return(y * 10000 + m * 100 + d);
}

// Check if d is a valid date on yyyymmdd form (allow less y:s too).
// Not sure exactly what the EU II engine requires, but for simplicity
// check for 1 <= year <= 9999, 1 <= month <= 12, 1 <= day <= 30.
// (Day 31 is not used at any rate, feb 29 and 30 is also not used but
// I think february is still 30 days for span calculations, so allow them
// anyway (will be converted to march 1 later on).)
int VerifyDate(int date) {
    int year, month, day;

    day = date % 100;
    date = date / 100;
    month = date % 100;
    year = date / 100;
    if (year < 1 || year > 9999) {
        return(0);
    }
    if (month < 1 || month > 12) {
        return(0);
    }
    if (day < 1 || day > 30) {
        return(0);
    }
    return(1);
}

int GetTag()
{
    int c, i = 0;
    char Tag[MAX_TAG_LENGTH + 1]; // + 1 for null termination.

    SkipWhitespacesAndComments();
    c = GetChar();
    // The first character should be a letter.
    if (((char)c >= 'a' && (char)c <= 'z') ||
        ((char)c >= 'A' && (char)c <= 'Z')) {
        Tag[i++] = (char)c;
    } else {
        Error("expected a tag (starting with a letter)", c);
        UnGetChar(c);
        return(INT_MAX);
    }
    // The remaining characters should be alphanumeric.
    c = GetChar();
    while (((char)c >= 'a' && (char)c <= 'z') ||
           ((char)c >= 'A' && (char)c <= 'Z') ||
           ((char)c >= '0' && (char)c <= '9')) {
        if (i < MAX_TAG_LENGTH) {
            Tag[i++] = (char)c;
        } else if (i == MAX_TAG_LENGTH) { // Don't warn for every extra character...
            Warning("tag too long, truncating", 0);
        }
        c = GetChar();
    }
    UnGetChar(c);
    // Null terminate.
    Tag[i] = 0;
    // Check if it's an already known tag.
    for (i=0; i<TagIndex; i++) {
        if (strcmp(TagArray[i], Tag) == 0) {
            // Already known tag.
            return(i);
        }
    }

    // New tag, insert if possible.
    if (TagIndex >= MAX_TAGS) {
        Error("Too many tags", 0);
        return(INT_MAX);
    }
    strcpy(TagArray[TagIndex], Tag);
    TagIndex++;
    return(TagIndex - 1);
}

int GetString()
{
    int c, i = 0;

    SkipWhitespacesAndComments();
    c = GetChar();
    // The first character should be a '"'.
    if ((char)c != '"') {
        Error("expected a string (within '\"' characters)", c);
        UnGetChar(c);
        return(INT_MAX);
    }
    // Copy all characters until the next '"' if possible.
    c = GetChar();
    while ((char)c != '"') {
        if (c == EOF) {
            Error("expected string termination ('\"')", c);
            return(INT_MAX);
        }
        if (i < MAX_STRING_LENGTH) {
            LatestString[i++] = (char)c;
        } else if (i == MAX_STRING_LENGTH) { // Don't warn for every extra character...
            Warning("string too long, truncating", 0);
        }
        c = GetChar();
    }
    // Don't unget the terminating '"'.
    // Null terminate.
    LatestString[i] = 0;
    return(0);
}

void Quit(int HaltOnExit)
{
    fprintf(stderr, "Execution completed with %d errors and %d warnings\n", NumErrors, NumWarnings);
    if (ifp != NULL) {
        fclose(ifp);
    }
    if (ofp != NULL) {
        fclose(ofp);
    }
	if (omfp != NULL) {
		fclose(omfp);
	}
    if (HaltOnExit > 0) {
        if (HaltOnExit > 1 || NumErrors > 0 || NumWarnings > 0) {
            fprintf(stderr, "\nPress return to continue...\n");
            (void)getchar();
        }
    }
    exit(NumErrors);
}


// Helper functions for generating the output events.
// Externals used: FILE *ofp, char TagArray[][], char StringArray[][],
// int RNGCTag, int EventIDPrefix, int EventData[][], ProvinceEventIndex[],
// char ProvinceNames[][]

// Not sure exactly how this works in the EU II engine, but I think each
// month is 30 days (even february somehow) and each year thus 360 days.
// Calculate using that assumption and subtract a few days to be on the
// safe side regarding spans starting or ending in february. (This is used
// for calculating the event offset, making it slightly too small is no
// problem, making it too large may result in CTDs.)
int CalcDateSpan(int Start, int End)
{
    int y1, y2, m1, m2, d1, d2;

    d1 = Start % 100;
    Start = Start / 100;
    m1 = Start % 100;
    y1 = Start / 100;
    Start = y1 * 360 + (m1 - 1) * 30 + (d1 - 1);
    d2 = End % 100;
    End = End / 100;
    m2 = End % 100;
    y2 = End / 100;
    End = y2 * 360 + (m2 - 1) * 30 + (d2 - 1);
    return(End - Start - 6);
}

// Event IDs are built up by concatenating the prefix number + a four digit
// number for the province ID + a two digit running number for the events
// used for that province. Example: with the prefix = 717 (as in the original
// mod), the first event used for province 302 (Hinterpommern in vanilla)
// would be 717030200, the next 717030201 etc.
int GenerateEventID(int ProvinceID, int Event)
{
    int ID;

    if (ProvinceEventIndex[Event][ProvinceID] > 99) {
        Error("too many events generated", 0);
        return(INT_MAX);
    }
    ID = EventIDPrefix * 1000000 + ProvinceID * 100 + Event * 10 + ProvinceEventIndex[Event][ProvinceID];
    ProvinceEventIndex[Event][ProvinceID]++;
    return(ID);
}

char *ModIDFormat = "\
event = {\n\
	id = %d\n\
	random = no\n\
	province = %d\n\
	name = \"EVENTNAME%d\" #%s\n\
	desc = \"%s\"\n\
	action = {\n\
		name = \"OK\"\n\
		command = { %s } #%s\n\
	}\n\
}\n\n";

char *GenFormat = "\
# %s\n\
event = {\n\
	id = %d\n\
	trigger = {\n\
%s\
%s\
	}\n\
	random = no\n\
	country = %s\n\
	name = \"AI_EVENT\"\n\
	desc = \"%d\"\n\
	date = { %s }\n\
	offset = %d\n\
	deathdate = { %s }\n\
	action = {\n\
		name = \"OK\"\n\
		ai_chance = %d\n\
		command = { type = trigger which = %d }\n\
	}\n\
	action = {\n\
		name = \"OK\"\n\
		ai_chance = %d\n\
		command = { }\n\
	}\n\
}\n\n";

// When the user selects AI event choices as Historical, the AI always chooses the first option.
// To be prepared for that case, provinces with a lower chance of conversion should have the Convert option second.
char *GenLowChanceFormat = "\
# %s\n\
event = {\n\
	id = %d\n\
	trigger = {\n\
%s\
%s\
	}\n\
	random = no\n\
	country = %s\n\
	name = \"AI_EVENT\"\n\
	desc = \"%d\"\n\
	date = { %s }\n\
	offset = %d\n\
	deathdate = { %s }\n\
	action = {\n\
		name = \"OK\"\n\
		ai_chance = %d\n\
		command = { }\n\
	}\n\
	action = {\n\
		name = \"OK\"\n\
		ai_chance = %d\n\
		command = { type = trigger which = %d }\n\
	}\n\
}\n\n";

char *Gen100pFormat = "\
# %s\n\
event = {\n\
    id = %d\n\
    trigger = {\n\
%s\
%s\
    }\n\
    random = no\n\
    country = %s\n\
    name = \"AI_EVENT\"\n\
    desc = \"%d\"\n\
    date = { %s }\n\
    offset = %d\n\
    deathdate = { %s }\n\
    action = {\n\
        name = \"OK\"\n\
        # Convert\n\
        command = { type = trigger which = %d }\n\
    }\n\
}\n\n";

const char *StrMonth[] = {
    NULL, "january", "february", "march", "april", "may", "june", "july",
    "august", "september", "october", "november", "december"
};

// Temporary arrays used during event generation.
// Make these a bit larger than the base strings in order to allow for some
// reasonable increase when doing the '%d' and '%s' expansions. (Say, a long
// province name multiplied a few times.)
static char StrExpName[MAX_STRING_LENGTH + 3 * MAX_PROVINCENAME_LENGTH];
static char StrExpDesc[MAX_STRING_LENGTH + 3 * MAX_PROVINCENAME_LENGTH];
static char StrExpCommand[MAX_STRING_LENGTH + 3 * MAX_PROVINCENAME_LENGTH];
static char StrExpTrigger[MAX_STRING_LENGTH + 40];
static char StrSmallFlag[MAX_TAG_LENGTH + 30];
static char StrNormalFlag[MAX_TAG_LENGTH + 30];
static char StrLargeFlag[MAX_TAG_LENGTH + 30];
static char StrNotSmallFlag[MAX_TAG_LENGTH + 40];
static char StrNotNormalFlag[MAX_TAG_LENGTH + 40];
static char StrNotLargeFlag[MAX_TAG_LENGTH + 50];
static char StrStartDate[40];
static char StrEndDate[40];

char *PickFlagStr(int Small, int Normal, int Large, int Target)
{
    int NumS = 0, NumN = 0, NumL = 0;

    // Shared only if different versions have the same probability.
    if (Small == Target) {
        NumS = 1;
    }
    if (Normal == Target) {
        NumN = 1;
    }
    if (Large == Target) {
        NumL = 1;
    }

    if (NumS > 0 && NumN > 0 && NumL > 0) {
        // All, no flag needed here.
        return("");
    }
    if (NumS > 0 && NumN > 0) {
        // All except Large.
        return(StrNotLargeFlag);
    }
    if (NumS > 0 && NumL > 0) {
        // All except Normal.
        return(StrNotNormalFlag);
    }
    if (NumN > 0 && NumL > 0) {
        // All except Small.
        return(StrNotSmallFlag);
    }
    if (NumS > 0) {
        // Only small.
        return(StrSmallFlag);
    }
    if (NumN > 0) {
        // Only Normal.
        return(StrNormalFlag);
    }
    if (NumL > 0) {
        // Only Large.
        return(StrLargeFlag);
    }
    // No events for this target.
    return(NULL);
}

// Function for outputting the events for a Modification.
// Currently we only support a limited set of probability numbers.
// Note that some numbers will generate a larger number of events than
// others, and it might be a good idea to keep the total number of events
// to a reasonable level... (That said, nothing prevents you from generating
// several modifications for the same EventData.)
// The currently supported probabilities are shown in the table below.
// The events line refers to the implementation: 5p = an event with 5%
// probability etc, '*' = multiple independent events, '->' = cascading events.
// Index:       0 1   2   3    4     5     6         7           8    9   10  11  12
// Probability: 0 5  10  15   28    39    48        61          72    85  90  95  100
// Events       - 5p 10p 15p 2*15p 3*15p 4*15p 85p->85p->85p 85p->85p 85p 90p 95p 100p
void OutputEvents(int ProvinceID, int Event, int Trigger, int StartDate, int EndDate,
                  int Small, int Normal, int Large)
{
    int ModID, ID1, ID2, ID3, Target;
    char *FlagStr;

    // Check that we actually have something to do...
    if (Small == 0 && Normal == 0 && Large == 0) {
        return;
    }
    // Do the '%s' and '%d' replacements on the argument strings.
    // Name: allow a maximum of three instances of '%s' (replaced by province name).
    sprintf(StrExpName, StringArray[EventData[Event][1]], ProvinceNames[ProvinceID],
            ProvinceNames[ProvinceID], ProvinceNames[ProvinceID]);
    // Description: allow a maximum of three instances of '%s' (replaced by province name).
    sprintf(StrExpDesc, StringArray[EventData[Event][2]], ProvinceNames[ProvinceID],
            ProvinceNames[ProvinceID], ProvinceNames[ProvinceID]);
    // Command: allow a maximum of three instances of '%d' (replaced by province id number).
    sprintf(StrExpCommand, StringArray[EventData[Event][3]], ProvinceID, ProvinceID, ProvinceID);
    // Trigger: allow a maximum of 10 instances of '%d' (replaced by province id number).
    sprintf(StrExpTrigger, StringArray[Trigger], ProvinceID, ProvinceID,
            ProvinceID, ProvinceID, ProvinceID, ProvinceID, ProvinceID,
            ProvinceID, ProvinceID, ProvinceID);
    // Change any occurance of feb 29 or feb 30 to mar 1.
    if (StartDate % 10000 == 229 || StartDate % 10000 == 230) {
        StartDate = (StartDate / 10000) * 10000 + 301;
    }
    if (EndDate % 10000 == 229 || EndDate % 10000 == 230) {
        EndDate = (EndDate / 10000) * 10000 + 301;
    }
    // Convert the dates to EU II format event date strings.
    sprintf(StrStartDate, "year = %d month = %s day = %d", StartDate / 10000,
            StrMonth[(StartDate / 100) % 100], StartDate % 100);
    sprintf(StrEndDate, "year = %d month = %s day = %d", EndDate / 10000,
            StrMonth[(EndDate / 100) % 100], EndDate % 100);
    // Generate the actual modification event.
    ModID = GenerateEventID(ProvinceID, Event);
    fprintf(omfp, ModIDFormat,
            ModID, ProvinceID, ModID, StrExpName, StrExpDesc, StrExpCommand, ProvinceNames[ProvinceID]);
    // Generate Small/Normal/Large flag strings.
    sprintf(StrSmallFlag,     "\t\tflag = Small%s\n",  TagArray[EventData[Event][0]]);
    sprintf(StrNormalFlag,    "\t\tflag = Normal%s\n", TagArray[EventData[Event][0]]);
    sprintf(StrLargeFlag,     "\t\tflag = Large%s\n",  TagArray[EventData[Event][0]]);
    sprintf(StrNotSmallFlag,  "\t\tNOT = { flag = Small%s }\n",  TagArray[EventData[Event][0]]);
    sprintf(StrNotNormalFlag, "\t\tNOT = { flag = Normal%s }\n", TagArray[EventData[Event][0]]);
    sprintf(StrNotLargeFlag,  "\t\tNOT = { flag = Large%s }\n",  TagArray[EventData[Event][0]]);
    // Generate the RNGC events.
    for (Target=1; Target<=100; Target++) {
        FlagStr = PickFlagStr(Small, Normal, Large, Target);
        if (FlagStr == NULL) {
            // Nothing for this target.
            continue;
        }
        ID1 = GenerateEventID(ProvinceID, Event);
		if (Target == 100)
			fprintf(ofp, Gen100pFormat, ProvinceNames[ProvinceID], ID1,
					StrExpTrigger, FlagStr, TagArray[RNGCTag], ID1, StrStartDate,
					CalcDateSpan(StartDate, EndDate), StrEndDate, ModID);
		else if (Target <= LOW_CHANCE_THRESHOLD)
			fprintf(ofp, GenLowChanceFormat, ProvinceNames[ProvinceID], ID1,
				StrExpTrigger, FlagStr, TagArray[RNGCTag], ID1, StrStartDate,
				CalcDateSpan(StartDate, EndDate), StrEndDate, 100 - Target, Target, ModID);
		else
			fprintf(ofp, GenFormat, ProvinceNames[ProvinceID], ID1,
					StrExpTrigger, FlagStr, TagArray[RNGCTag], ID1, StrStartDate,
					CalcDateSpan(StartDate, EndDate), StrEndDate, Target, ModID, 100-Target);
    }
}

int main(int argc, char* argv[])
{
    int ProvinceFileIndex = -1, DataFileIndex = -1, HaltOnExit = 0;
    int Char, Ret, i, j;
    int Num, Num2, Num3, Num4, Num5, Num6;
    int TagID, TagID2, TagID3, TagID4, TagID5;
    int Str, Str2, Str3, Str4;
    
    // Initialize keyword tags.
    strcpy(TagArray[TAG_FILE_ID],        "ProvinceModificationDataFile");
    strcpy(TagArray[TAG_RNGC],           "RNGCTag");
    strcpy(TagArray[TAG_EVENT_ID_PREFIX],"EventIDPrefix");
    strcpy(TagArray[TAG_OUTPUT_FILE],    "OutputFile");
    strcpy(TagArray[TAG_SET_STRING],     "SetString");
    strcpy(TagArray[TAG_TARGET_STRING],  "TargetString");
    strcpy(TagArray[TAG_START_CONDITION],"StartCondition");
    strcpy(TagArray[TAG_EVENT_DATA],     "EventData");
    strcpy(TagArray[TAG_MODIFICATION],   "Modification");
    strcpy(TagArray[TAG_END_OF_DATA],    "EndOfData");
	strcpy(TagArray[TAG_OUTPUT_FILE_MOD],"OutputFileMod");
	strcpy(TagArray[TAG_OUTPUT_FILE_MOD_HEADER], "OutputFileModHeader");
    // Parse the arguments.
    for (i=1; i<argc; i++) {
        if (argv[i][0] == '-') {
            if (argv[i][1] == 'h') {
                // Lazy: consider any option beginning with '-h' as '-h'.
                HaltOnExit = 1;
            } else if (argv[i][1] == 'H') {
                // Lazy: consider any option beginning with '-H' as '-H'.
                HaltOnExit = 2;
            } else {
                // Unknown option.
                fprintf(stderr, "Usage: %s [-h|H] <province file> <data file>\n", argv[0]);
                NumErrors++;
                Quit(HaltOnExit);
            }
        } else {
            // Not an option.
            if (ProvinceFileIndex < 0) {
                // First non-option argument should be the province file.
                ProvinceFileIndex = i;
            } else if (DataFileIndex < 0) {
                // Second non-option argument should be the data file.
                DataFileIndex = i;
            } else {
                // Too many non-option arguments.
                fprintf(stderr, "Usage: %s [-h|H] <province file> <data file>\n", argv[0]);
                NumErrors++;
                Quit(HaltOnExit);
            }
        }
    }
    // Check for the required arguments.
    if (ProvinceFileIndex < 0 || DataFileIndex < 0) {
        fprintf(stderr, "Usage: %s [-h|H] <province file> <data file>\n", argv[0]);
        NumErrors++;
        Quit(HaltOnExit);
    }
    
    // Open the province file.
    ifp = fopen(argv[ProvinceFileIndex], "r");
    if (ifp == NULL) {
        fprintf(stderr, "Failed to open province file %s\n", argv[ProvinceFileIndex]);
        NumErrors++;
        Quit(HaltOnExit);
    }
    LineNumber = 1;
    // Start parsing province file.
    fprintf(stderr, "Parsing province file %s\n", argv[ProvinceFileIndex]);
    if (GetChar() == 'I' && GetChar() == 'd' && GetChar() == ';' &&
        GetChar() == 'N' && GetChar() == 'a' && GetChar() == 'm' &&
        GetChar() == 'e' && GetChar() == ';') {
        // Looks like a province.csv file.
        SkipRestOfLine();
        while (1) {
            Num = GetNum();
            if (Num == -1) {
                // End marker.
                break;
            }
            if (Num < 0 || Num >= MAX_PROVINCES) {
                Error("province ID out of range, aborting", 0);
                Quit(HaltOnExit);
            }
            Char = GetChar();
            if ((char)Char != ';') {
                Error("expected ';'", Char);
                UnGetChar(Char);
            }
            i = 0;
            while (1) {
                Char = GetChar();
                if ((char)Char == ';') {
                    // End of field.
                    break;
                }
                if (i >= MAX_PROVINCENAME_LENGTH) {
                    Warning("province name too long, truncating", 0);
                    break;
                } else {
                    ProvinceNames[Num][i] = (char)Char;
                    i++;
                }
            }
            // Null terminate.
            ProvinceNames[Num][i] = 0;
            if (Num > LargestProvinceID) {
                if (Num >= MAX_PROVINCES) {
                    Error("too high province ID", 0);
                    Quit(HaltOnExit);
                }
                LargestProvinceID = Num;
            }
            SkipRestOfLine();
        }
    } else {
        fprintf(stderr, "the province file doesn't look like an EU II province.csv file");
        NumErrors++;
        Quit(HaltOnExit);
    }
    // All done with the province file.
    fclose(ifp);

    // Open data file.
    ifp = fopen(argv[DataFileIndex], "r");
    if (ifp == NULL) {
        fprintf(stderr, "Failed to open data file %s\n", argv[DataFileIndex]);
        NumErrors++;
        Quit(HaltOnExit);
    }
    LineNumber = 1;
    // Start parsing data file.
    fprintf(stderr, "Parsing data file %s\n", argv[DataFileIndex]);
    TagID = GetTag();
    // Verify the file ID tag.
    if (TagID != TAG_FILE_ID) {
        Error("expected the ProvinceModificationDataFile tag", 0);
        Quit(HaltOnExit);
    }
    TagID = GetTag();
    while (1) {
        switch (TagID) {
            case TAG_FILE_ID:
                Warning("spurious ProvinceModificationDataFile tag", 0);
                break;
            case TAG_RNGC:
                VerifyListStart();
                RNGCTag = GetTag();
                VerifyListEnd();
                // We ought to verify that it's a valid EU II country tag.
                break;
            case TAG_EVENT_ID_PREFIX:
                VerifyListStart();
                Num = GetNum();
                VerifyListEnd();
                // Not sure what the exact requirements for the event numbers
                // are, but to be on the safe side, make them positive numbers
                // fitting a signed 32-bit integer, and stay clear of the
                // lowest range. With the event numbering scheme used, we
                // need six digits for ourselves.
                if (Num < 1 || Num > 2146) {
                    Error("EventIDPrefix argument outside [1..2146]", 0);
                } else {
                    EventIDPrefix = Num;
                }
                break;
            case TAG_OUTPUT_FILE:
                VerifyListStart();
                Ret = GetString();
                VerifyListEnd();
                ofp = NULL;
                if (Ret == 0) {
                    // Close the old one, if any.
                    if (ofp != NULL) {
                        fclose(ofp);
                    }
                    // Try to open the file for writing.
                    ofp = fopen(LatestString, "w+");
                }
                if (ofp == NULL) {
                    Error("can't open the output file", 0);
                }
                break;
			case TAG_OUTPUT_FILE_MOD:
                VerifyListStart();
                Ret = GetString();
                VerifyListEnd();
                omfp = NULL;
                if (Ret == 0) {
                    // Close the old one, if any.
                    if (omfp != NULL) {
                        fclose(omfp);
                    }
                    // Try to open the file for writing.
                    omfp = fopen(LatestString, "w+");
					// Write the header
					fprintf(omfp, "%s", OutputFileModHeader);
                }
                if (omfp == NULL) {
                    Error("can't open the output file", 0);
                }
                break;
			case TAG_OUTPUT_FILE_MOD_HEADER:
                VerifyListStart();
                Ret = GetString();
                VerifyListEnd();
                if (Ret == 0) {
					strcpy(OutputFileModHeader, LatestString);
                } else {
                    Error("no valid string to set as header", 0);
                }
                break;
            case TAG_SET_STRING:
                VerifyListStart();
                TagID2 = GetTag();
                Ret = GetString();
                VerifyListEnd();
                if (TagID2 >= 0 && TagID2 < TAG_FIRST_USER_TAG) {
                    Error("can't SetString a keyword tag", 0);
                }
                if (TagID2 >= TAG_FIRST_USER_TAG && TagID2 < TagIndex && Ret == 0) {
                    if (StringIndex >= MAX_STRINGS) {
                        Error("too many strings defined", 0);
                    } else {
                        // Valid user tag and string.
                        strcpy(StringArray[StringIndex], LatestString);
                        UserStringsIndexArray[StringIndex] = TagID2;
                        StringIndex++;
                    }
                }
                break;
            case TAG_TARGET_STRING:
                VerifyListStart();
                Ret = GetString();
                VerifyListEnd();
                if (Ret == 0) {
                    if (ofp != NULL) {
                        fprintf(ofp, "%s", LatestString);
                    } else {
                        Error("no valid output file", 0);
                    }
                } else {
                    Error("no valid string to output", 0);
                }
                break;
            case TAG_START_CONDITION:
                VerifyListStart();
                Num = GetNum();
                TagID2 = GetTag();
                VerifyListEnd();
                // Verify it for being a valid province (based on province.csv).
                if (Num <= 0 || Num > LargestProvinceID) {
                    Error("not a valid province", 0);
                }
                // Check that the tag refers to a string set by the user.
                for (Str=0; Str<StringIndex; Str++) {
                    if (UserStringsIndexArray[Str] == TagID2) {
                        // Found it.
                        break;
                    }
                }
                if (Str >= StringIndex) {
                    Error("undefined tag", 0);
                }
                if (ofp != NULL) {
                    if (Num > 0 && Num <= LargestProvinceID && Str < StringIndex) {
                        fprintf(ofp, "province = { id = %d %s }\n", Num, StringArray[Str]);
                    }
                } else {
                    Error("no valid output file", 0);
                }
                break;
            case TAG_EVENT_DATA:
                VerifyListStart();
                TagID2 = GetTag();
                TagID3 = GetTag();
                TagID4 = GetTag();
                TagID5 = GetTag();
                VerifyListEnd();
                if (TagID2 >= 0 && TagID2 < TAG_FIRST_USER_TAG) {
                    Error("can't define a keyword tag", 0);
                }
                // Check that the tag doesn't refer to a string set by the user.
                for (Str=0; Str<StringIndex; Str++) {
                    if (UserStringsIndexArray[Str] == TagID2) {
                        // Found it.
                        break;
                    }
                }
                if (Str < StringIndex) {
                    Error("tag already used for a string", 0);
                }
                // Check that the tag refers to a string set by the user.
                for (Str2=0; Str2<StringIndex; Str2++) {
                    if (UserStringsIndexArray[Str2] == TagID3) {
                        // Found it.
                        break;
                    }
                }
                if (Str2 >= StringIndex) {
                    Error("undefined name tag", 0);
                }
                // Check that the tag refers to a string set by the user.
                for (Str3=0; Str3<StringIndex; Str3++) {
                    if (UserStringsIndexArray[Str3] == TagID4) {
                        // Found it.
                        break;
                    }
                }
                if (Str3 >= StringIndex) {
                    Error("undefined description tag", 0);
                }
                // Check that the tag refers to a string set by the user.
                for (Str4=0; Str4<StringIndex; Str4++) {
                    if (UserStringsIndexArray[Str4] == TagID5) {
                        // Found it.
                        break;
                    }
                }
                if (Str4 >= StringIndex) {
                    Error("undefined command tag", 0);
                }
                if (TagID2 >= TAG_FIRST_USER_TAG && TagID2 < TagIndex &&
                    Str >= StringIndex && Str2 < StringIndex &&
                    Str3 < StringIndex && Str4 < StringIndex) {
                    if (EventDataIndex < MAX_EVENT_DATA) {
                        EventData[EventDataIndex][0] = TagID2;
                        EventData[EventDataIndex][1] = Str2;
                        EventData[EventDataIndex][2] = Str3;
                        EventData[EventDataIndex][3] = Str4;
                        EventDataIndex++;
                    } else {
                        Error("too many EventData definitions", 0);
                    }
                }
                break;
            case TAG_MODIFICATION:
                VerifyListStart();
                Num = GetNum();
                TagID2 = GetTag();
                TagID3 = GetTag();
                Num2 = GetDate();
                Num3 = GetDate();
                Num4 = GetNum();
                Num5 = GetNum();
                Num6 = GetNum();
                VerifyListEnd();
                // Verify it for being a valid province (based on province.csv).
                if (Num <= 0 || Num > LargestProvinceID) {
                    Error("not a valid province", 0);
                }
                // Check that we have a valid EventData.
                for (i=0; i<EventDataIndex; i++) {
                    if (EventData[i][0] == TagID2) {
                        // Found it.
                        break;
                    }
                }
                if (i >= EventDataIndex) {
                    Error("not a valid EventData", 0);
                }
                // Check that the tag refers to a string set by the user.
                for (j=0; j<StringIndex; j++) {
                    if (UserStringsIndexArray[j] == TagID3) {
                        // Found it.
                        break;
                    }
                }
                if (j >= StringIndex) {
                    Error("undefined trigger tag", 0);
                }
                if (!VerifyDate(Num2)) {
                    Error("invalid start date", 0);
                }
                if (!VerifyDate(Num3)) {
                    Error("invalid end date", 0);
                }
                if (Num2 > Num3) {
                    Error("start date larger than end date", 0);
                }
                if (Num4 < 0 || Num4 > 100 ||
                    Num5 < 0 || Num5 > 100 ||
                    Num6 < 0 || Num6 > 100) {
                    Error("probability outside [0..100]", 0);
                }
                if (RNGCTag == INT_MAX) {
                    Error("undefined RNGCTag, aborting", 0);
                    Quit(HaltOnExit);
                }
                if (EventIDPrefix == INT_MAX) {
                    Error("undefined EventIDPrefix, aborting", 0);
                    Quit(HaltOnExit);
                }
                if (ofp != NULL) {
                    if (Num > 0 && Num <= LargestProvinceID &&
                        i < EventDataIndex && j < StringIndex &&
                        VerifyDate(Num2) && VerifyDate(Num3) && Num2 <= Num3 &&
                        Num4 >= 0 && Num4 <= 100 &&
                        Num5 >= 0 && Num5 <= 100 &&
                        Num6 >= 0 && Num6 <= 100) {
                        OutputEvents(Num, i, j, Num2, Num3, Num4, Num5, Num6);
                    }
                } else {
                    Error("no valid output file", 0);
                }
                break;
            case TAG_END_OF_DATA:
                // All done, but check for any spurios data.
                SkipWhitespacesAndComments();
                Char = GetChar();
                if (Char != EOF) {
                    Warning("ignoring spurious data after EndOfData tag", Char);
                }
                Quit(HaltOnExit);
                break;
            case INT_MAX:
                // Not a tag.
                Error("not a valid tag", 0);
                Char = GetChar();
                if (Char == EOF) {
                    Error("end of file before EndOfdata tag", 0);
                    Quit(HaltOnExit);
                } else {
                    UnGetChar(Char);
                }
                break;
            default:
                // User tag.
                Error("not a keyword tag", 0);
                break;
        }
        if (NumErrors > 50) {
            Error("too many errors, aborting", 0);
            Quit(HaltOnExit);
        }
        TagID = GetTag();
    }
}
