/*
** Copyright 2002-2006 KVASER AB, Sweden.  All rights reserved.
*/

/* getargs.h

   Mats èkerblom  1995-11-28
   Senast Ñndrat  1994-11-28

*/

#ifndef __GETARGS_H  // __UTIL_h definieras i TurboVisions util.h!
#define __GETARGS_H

//#pragma interface

#include <dir.h>
#include <fstream.h>

void _OutOfMemory_(char *file, int line);
#define OutOfMemory() _OutOfMemory_(__FILE__, __LINE__)

#define CompilerAssert(e) \
  extern char _Dummy[(e)?1:0]
#define FileExists(f) (!access(f,0))
typedef int (*fptr)(const void *, const void *);  // AnvÑndbar vid qsort().


extern char ProgName[MAXFILE];     // Namnet.
extern char ProgPath[MAXPATH];     // Biblioteket programmet finns i.
extern char ProgNameLong[MAXPATH]; // argv[0]

typedef unsigned char uchar;
typedef unsigned char byte;
typedef unsigned long ulong;


void FixProgPath(char *Prog);
char *SearchDir(char *d, char *f);
char *AddDir(char *d, char *f);
char *sprf(char *fmt, ...);
void ErrorText(char *fmt, ...)
  #ifdef __GNUC__
    __attribute__((format(printf, 1,2)))
  #endif
  ;
void ErrorExit(char *fmt, ...)
  #ifdef __GNUC__
    __attribute__((format(printf, 1,2)))
  #endif
  ;

/*
#if __BORLANDC__ < 0x460
  // These templates are defined in Borlandc++ 4.52
template <class T> inline const T min(const T& a, const T& b) {
  if (a < b)
    return a;
  else
    return b;
}

template <class T> inline const T max(const T& a, const T& b) {
  if (a > b)
    return a;
  else
    return b;
}
#endif

#define minmax(n, a, b) max(a, min(b, n))
*/
   
int MyIsspace(int c);
int MyIsalnum(int c);
int MyIseoln(int c);
int MyIsdigit(int c);

long MyAtol(char *s);
int MyAtoi(char *s);
char *MyLtoa(long n, int w = 0);
char *i2bin(unsigned int i, int n);
long atoiR(char *s, long min, long max);
int Empty(const char *s);
int Empty(long n);

void PadSpaces(char *s, int len);
int DelSpaces(char *s);
int DelTspaces(char *s);
int DelBTSpaces(char *s);
int FillSpaces(char *s, int n);

void InitBreak();
void CheckCtrlC(void);
void MyClreol(void);
int CheckAbort(int reset = 0);
int MySystem(char *cmd);

class logC {
  int indent;
  int col;
  int sp;
  int istack[16];

  ofstream *logFile;
  char *lBuf;  // NULL if inactive.
  int lBufLen; 
  int lBufPos; // Where to put the next character in the buf 

  void fixIndent(void);
public:
  logC();
  ~logC() {if (lBufLen) {nl(); delete[](lBuf);}}
  void setFile(ofstream* s);
  void reset(int keep = 0);   // Bîrjar om vid radens bîrjan. Raderar den om !keep.
  void setIndent(int i) {indent = i;}
  int getIndent(void) {return indent;}
  void addIndent(int n) {pushIndent(indent+n);}
  void pushIndent(int n);
  void popIndent(void);
  void put(char *s);  // Skriv ut en text.
  void putB(char *s); // Skriv ut en text men flytta ej fram markîren.
  int putf(char *fmt, ...);  // Skriv formatterad text.
  int putfB(char *fmt, ...);  // Skriv formatterad text, flytta ej markîren.
  void nl();

  logC& operator << (char *s);
  logC& operator << (int n);
  logC& operator << (long n);
  logC& operator << (char c);
};

extern logC cLog;
#define cerrLog cLog

class getArgsC {
  int argc;      // Antalet kvarvarande element i argv.
  char **argv;   // NÑsta oanvÑnda argument.
  char *p;       // NÑsta oanvÑnda option.
  char *argSave; // Ett vÑntande argument (inlÑst i fîrvÑg).
  int optEnd;    // Flaggar att inga mer optioner finns kvar (men kanske vanliga argument).
  char *argFileName;
  ifstream argFile;
  int argFileOpenF;

  char *argBuf;

  static const int argBufMaxLen;
  char *nextArgItem(void);
  void putbackArgItem(char *s);

public:
  getArgsC() {argc = 0; optEnd = 1; argSave = argBuf = NULL; argFileOpenF = 0;}
  getArgsC(int argcI, char **argvI);
  ~getArgsC() {if (argBuf) delete[] argBuf;}
  int nextOpt(void);
  int peekOpt(int nextArg = 1);
  char *optArg();
  char *optArgP();
  char *testOptArg(void);
  char *nextArg(void);
  char *nextArgP(void);
  int moreQ(void);
  int argQ(void);
};


#endif
