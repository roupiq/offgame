/*********************************************************/
/* lrsnash is driver for computing all nash equilibria   */
/* for two person games given by mxn payoff matrices A,B */
/* Usage: lrsnash [options] game [output file]           */
/*                                                       */
/* game is a file containing:                            */
/* m n                                                   */
/* A          (row by row)                               */
/* B          (row by row)                               */
/*                                                       */
/* Derived from nash.c in lrslib-060 by                  */ 
/* Terje Lensberg, October 26, 2015:                     */
/* Simplified API via lrs_solve_nash(game *g)            */
/*********************************************************/
/* 
Compile:
gcc -O3 -o lrsnash lrsnash.c lrsnashlib.c lrslib.c lrsgmp.c -lgmp -DGMP
*/

char Usage[] =
"usage (standard): %s [options...] <input_file...>\n"
"usage (legacy):   %s <input_file1> <input_file2> [<output_file>]\n"
"type %s -h for more information\n\n";

char Helptext[] =
"\nusage (standard): %s [options...] <input_file...>\n"
"  Input file structure: Input files to setupnash\n"
"  Input files can be specified separately, or by using wildcards, as in 'game*'\n"
"  Options:\n"
"    -v, --verbose         Prints a trace of the solution process\n"
"    -d, --debug           Dumps lots of information for debugging\n"
"    -p, --printgame       Prints the payoff matrix for the game\n"
"    -s, --standard        Promise that input files have standard structure\n"
"    -o, --outfile <file>  Send output to <file>\n"
"    -h, --help            Prints this text\n"
"     Short options can be grouped, as in '-ps' and '-do out.txt'\n"
"usage (legacy): %s <input_file1> <input_file2> [<output_file>]\n"
"  Input file structure: Output files from setupnash\n"
"  Passing options with legacy input files produces an error\n"
"  (options must be specified in the input files)\n\n";

char LegacyMsg[] =
"\nProcessing legacy input files. Alternatively, you may skip\n"
"setupnash and pass its input file to this program.\n";


#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include <getopt.h>
#include <sys/stat.h>
#include "lrsdriver.h"
#include "lrslib.h"
#include "lrsnashlib.h"

//========================================================================
// Games
//========================================================================




//----------------------------------------------------------------------------------------//
// Reading games
//----------------------------------------------------------------------------------------//

char *Outfile = NULL;

#ifdef LRSLONG
#ifdef B128
char *name="*lrsnash2";
#else
char *name="*lrsnash1";
#endif
#else
char *name="*lrsnash";
#endif
//----------------------------------------------------------------------------------------//
int openIO(void) {
  if (!lrs_init(name))
    return FALSE;
  fprintf(stderr, "\n");
	if(Outfile != NULL) {
	  if ((lrs_ofp = fopen(Outfile, "w")) == NULL) {
	    fprintf(stderr, "\nBad output file name\n");
	    return FALSE;
	  }
	}
	return TRUE;
}

void closeIO(void) {
	if(lrs_ofp != stdout)
		fprintf(stdout, "\n");
  lrs_close("lrsnash:");
}

#define RECWARN(filename) fprintf(stderr, "\nWarning: Excess data in file %s.\n", filename);
#define ERREXIT if(lrs_ofp != NULL) closeIO(); exit(1);
#define FILEERROR(name) {fprintf(stderr, "\nError: Cannot find input file '%s'. \
  Execution halted\n", name); ERREXIT}
#define READERROR(name) {fprintf(stderr, "\nError: Premature end of input file '%s'. \
  Execution halted\n", name); ERREXIT}


void allocateGameStorage(game *g, const char *infile) {
	int i, pos;
	struct stat st;

/* Get size of input file and use it to allocate storage for payoff strings. */
/* This provides enough space for adding string terminators ('\0'), because  */
/* there is at least one whitespace character for every payoff number.       */

	stat(infile, &st);
	g->pstore = (char *) calloc(st.st_size, sizeof(char));

/* Initialize payoff matrices */

	for(pos=0; pos<2; pos++) {
		g->payoff[pos] = (char ***) calloc(g->nstrats[ROW], sizeof(char **));
  	for (i=0;i<g->nstrats[ROW];i++) 
			g->payoff[pos][i] = (char **) calloc(g->nstrats[COL], sizeof(char *));
	}
}

void freeGameStorage(game *g) {
	int i, pos;

	for(pos=0; pos<2; pos++) {
  	for (i=0; i<g->nstrats[ROW]; i++) 
			free(g->payoff[pos][i]);
		free(g->payoff[pos]);
	}
	free(g->pstore);
}


//----------------------------------------------------------------------------------------//
int readGame(game * g, const char *filename)
{
	FILE *IN;
  long pos, s, t, nr, nc;
	char *p;
	g->name = (char *) calloc(strlen(filename)+1, sizeof(char));
	strcpy(g->name, filename);
  if ((IN = fopen(filename, "r")) == NULL) 
		FILEERROR(filename);
  if (fscanf(IN, "%ld %ld", &nr, &nc) < 2) 
		READERROR(filename);

/* get game dimensions */

  g->nstrats[ROW] = nr;
  g->nstrats[COL] = nc;

	allocateGameStorage(g, filename);

/* Read payoff matrices */

	p = g->pstore;
  for (pos = 0; pos < 2; pos++) {
		for (s=0; s<g->nstrats[ROW]; s++) {
		  for (t=0; t<g->nstrats[COL]; t++) {
		    if(fscanf(IN, "%s", p) < 1) 
					READERROR(filename);
				g->payoff[pos][s][t] = p;
		    p += strlen(p)+1; 
			}
		}
	}

	if (fscanf(IN, "%1s", p) == 1)  // Too many payoff entries
		RECWARN(filename);

  fclose(IN);
  return TRUE;
}

//----------------------------------------------------------------------------------------//


//========================================================================
// Command line processing
//========================================================================

// Flags to be set from command line options
static long Print_game_flag;
static long Standard_input_flag;

void printUsage(const char *progname) {
	fprintf(stderr, Usage, progname, progname, progname);
}

void printInfo(const char *progname) {
	fprintf(stderr, Helptext, progname, progname);
}


//----------------------------------------------------------------------------------------//
// Collects flags and reads list of games
int getArgs(int argc, char **argv)
{
  int c, error = FALSE;
	const char shortOptions[] = ":vdpsho:";

  if (argc < 2) {
		printUsage(argv[0]);
    return FALSE;
  }

  while (1) {
    static struct option long_options[] = {
      {"verbose", no_argument, 0, 'v'},
      {"debug", no_argument, 0, 'd'},
      {"printgame", no_argument, 0, 'p'},
      {"standard", no_argument, 0, 's'},
      {"outfile",  required_argument, 0, 'o'},
      {"help", no_argument, 0, 'h'}
//      {0, 0, 0, 0}
    };
    /* getopt_long stores the option index here. */
    int option_index = 0;

    c = getopt_long(argc, argv, shortOptions, long_options, &option_index);

    /* Detect the end of the options. */
    if (c == -1)
      break;

    switch (c) {
    case '?':
			fprintf(stderr, "\nError: Unknown option '-%c'.\n", optopt);
			error = TRUE;
      break;

    case ':':
			fprintf(stderr, "\nError: Missing argument to option '-%c'.\n", optopt);
			error = TRUE;
      break;

    case 'v':
      Verbose_flag = TRUE;
      break;

    case 'd':
      Debug_flag = TRUE;
      break;

    case 'p':
      Print_game_flag = TRUE;
      break;

    case 's':
      Standard_input_flag = TRUE;
      break;

    case 'h':
			printInfo(argv[0]);
			return FALSE;
      break;

    case 'o':
			Outfile = optarg;
      break;

    default:
      abort();
    }
  }
	if(error) {
		fprintf(stderr, "Execution halted\n"); 
		return FALSE;
	}
	return TRUE;
}


//----------------------------------------------------------------------------------------//
// Checks if an input file is legacy (contains letters)
int isLegacy(const char *filename) {
	FILE *fp;
	int i, n, foundLetter = FALSE;
	char buf[100];
	
	if((fp = fopen(filename, "r")) == NULL) FILEERROR(filename);

	n = fread(buf, sizeof(char), 100, fp); 
	for(i=0;i<n;i++) 
		if(isalpha(buf[i])) {
			foundLetter = TRUE;
			break;
		}
	fclose(fp);
  return foundLetter;
}




//========================================================================
// Main()
//========================================================================
int main(int argc, char **argv)
{
  game Game;                            // Storage for one game
  game *g = &Game;

  if(!getArgs(argc, argv))              // Read options and input file names. When we get here:
		return 1;                           // optind is a global integer supplied by getopt, and
                                        // argv[optind] is the first non-option argument in argv
	if(Standard_input_flag                // Assume standard input files if user set the flag,
      || optind == argc - 1             // or if only one input file,
      || !isLegacy(argv[optind]))       // or if the first input file is not legacy
  {
		if(!openIO())
			return 1;
		while (optind < argc) {             // Handle standard input file[s]
		  if(readGame(g, argv[optind++])) { // Game storage is allocated here
				if(Print_game_flag)
		    	printGame(g);
		    lrs_solve_nash(g);
		  }
		}
  	closeIO();
		freeGameStorage(g);
	}
	else 
	{                                     // Handle legacy input files      
  	fprintf (stderr, "%s", LegacyMsg);  // Print a message to user
		lrs_solve_nash_legacy(argc, argv);
	}
	return 0;
}


