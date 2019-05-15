/*
** Copyright 2002-2006 KVASER AB, Sweden.  All rights reserved.
*/

#ifndef DEBUGPRN_H
#    define DEBUGPRN_H
/***************************************************************************
* COPYRIGHT:    KVASER AB
* DESIGNED BY:  Lasse Kinnunen
* DESCRIPTION:  Debug print macros.
***************************************************************************/


/***************************************************************************
*   INCLUDES
***************************************************************************/
#ifndef NDEBUG
#    include <stdio.h>
#    if (defined( __BORLANDC__) || defined(_MSC_VER)) && defined( TIMESTAMPED_DEBUGPRINT)
#        include <time.h>
#    endif
#endif


/***************************************************************************
*   DEFINES
***************************************************************************/
#ifndef NDEBUG
#    if (defined( __BORLANDC__) || defined(_MSC_VER)) && defined( TIMESTAMPED_DEBUGPRINT)
#        define POSPRINTF( args)                    \
          {                                         \
              struct tm *time_now;                  \
              time_t secs_now;                      \
              char timestr[80];                     \
              (void) time( &secs_now);              \
              time_now = localtime( &secs_now);     \
              (void) strftime( timestr, 80, "%y-%m-%d %H:%M.%S",    \
                      time_now);                    \
              (void) printf( "%s %d, %s: ",         \
                      __FILE__, __LINE__, timestr); \
          }                             \
          (void) printf args

#        define POSPUTS( str)                       \
          {                                         \
              struct tm *time_now;                  \
              time_t secs_now;                      \
              char timestr[ 80];                    \
              (void) time( &secs_now);              \
              time_now = localtime( &secs_now);     \
              (void) strftime( timestr, 80, "%y-%m-%d %H:%M.%S",    \
                      time_now);                    \
              (void) printf( "%s %d, %s: %s\n",             \
                    __FILE__, __LINE__, timestr, str);      \
          }

#    else

#        define POSPRINTF( args)    (void) printf( "%s %d: ",   \
                                                   __FILE__,    \
                                                   __LINE__);   \
                                    (void) printf args

#        define POSPUTS( str)   (void) printf( "%s %d: %s\n",   \
                                                   __FILE__,    \
                                                   __LINE__,    \
                                                str)

#    endif

#    define PRINTF( args)   (void) printf args
#    define PUTS( str)      (void) puts( str)
#    define PUTCHAR( ch)    (void) putchar( ch)

#    define TRACEBOX(x)     traceBox x
#    define BEEP(freq, duration) Beep((freq), (duration))
#    define MESSAGEBEEP     MessageBeep(0xffff)
extern void traceBox(char *s, ...);

#else

#    define POSPRINTF( args)
#    define POSPUTS( str)
#    define PRINTF( args)
#    define PUTS( str)
#    define PUTCHAR( ch)

#    define TRACEBOX(x)
#    define BEEP(x, y)
#    define MESSAGEBEEP

#endif


/***************************************************************************
*   CONSTANTS
***************************************************************************/
// extern const char * stdErrStr;


#endif  /* #ifndef DEBUGPRN_H */
