#ifndef OSIF_KERNEL_H_
#define OSIF_KERNEL_H_

/*
** Copyright 2002-2006 KVASER AB, Sweden.  All rights reserved.
*/

#if LINUX
//###############################################################

#   include <linux/types.h>
#   include <linux/tty.h>
#   include <linux/module.h>
#   include <linux/spinlock.h>
#   if LINUX_2_6
#       include <linux/workqueue.h>
#   else
#       include <linux/tqueue.h>
#       include <pcmcia/driver_ops.h>
#       include <pcmcia/bus_ops.h>
#   endif

// DEFINES
#   define OS_IF_TIMEOUT 0
#   define OS_IF_TICK_COUNT jiffies
#if LINUX_2_6
#   define OS_IF_MODE_INC_USE_COUNT ;
#   define OS_IF_MODE_DEC_USE_COUNT ;
#else
#   define OS_IF_MODE_INC_USE_COUNT MOD_INC_USE_COUNT;
#   define OS_IF_MODE_DEC_USE_COUNT MOD_DEC_USE_COUNT;
#   define IRQ_NONE
#   define IRQ_HANDLED
#   define IRQ_RETVAL(x)

#endif
 
    // TYPEDEFS
    typedef struct timeval        OS_IF_TIME_VAL;
    typedef off_t                 OS_IF_OFFSET;
    typedef unsigned long         OS_IF_SIZE;
    typedef wait_queue_head_t     OS_IF_WAITQUEUE_HEAD;
    typedef wait_queue_t          OS_IF_WAITQUEUE;    
    typedef spinlock_t            OS_IF_LOCK;
    
    #if LINUX_2_6
       typedef struct work_struct      OS_IF_TASK_QUEUE_HANDLE;
       typedef struct workqueue_struct OS_IF_WQUEUE;
       typedef struct completion       OS_IF_SEMAPHORE;
    #else
       typedef struct tq_struct        OS_IF_TASK_QUEUE_HANDLE;
       typedef int                     OS_IF_WQUEUE;
       typedef struct semaphore        OS_IF_SEMAPHORE;
    #endif
    
    

    
#else // end LINUX
    
//###############################################################    
// win32 defines

    // INCLUDES
#   include "LapcanTypes.h"
#   include <windows.h>
#   include <cardserv.h>    

    
    // DEFINES
#   define OS_IF_TIMEOUT WAIT_TIMEOUT
#   define HZ 1000
#   define OS_IF_TICK_COUNT GetTickCount()

    // TYPEDEFS
    typedef LPCRITICAL_SECTION  OS_IF_LOCK;
    
    typedef struct timeval {
        long tv_sec; /* seconds */
        long tv_usec; /* microseconds */
    };
    typedef struct timeval      OS_IF_TIME_VAL;
    typedef long off_t;
    typedef off_t               OS_IF_OFFSET;
    typedef char*               OS_IF_DEVICE_CONTEXT_NODE;
    typedef PCARD               OS_IF_CARD_CONTEXT;
    typedef HANDLE              OS_IF_SEMAPHORE;
    typedef CARD_SOCKET_HANDLE  OS_IF_EVENT_PARAM;
    typedef size_t              OS_IF_SIZE;
    typedef HANDLE              OS_IF_TASK_QUEUE_HANDLE;
    typedef HANDLE              OS_IF_WAITQUEUE_HEAD;
    typedef int                 OS_IF_WAITQUEUE;   
    typedef CARD_EVENT          OS_IF_EVENT;
    
#endif // win32


#endif //OSIF_KERNEL_H_
