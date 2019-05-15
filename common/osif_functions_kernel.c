/*
** Copyright 2002-2006 KVASER AB, Sweden.  All rights reserved.
*/

#if LINUX
#   include <linux/sched.h>
#   include <linux/interrupt.h>
#   include <asm/io.h>
#   include <asm/system.h>
#   include <asm/bitops.h>
#   include <asm/uaccess.h>
#   if LINUX_2_6
#       include <linux/workqueue.h>
#       include <linux/wait.h>
#       include <linux/completion.h>
#   else
#       include <linux/tqueue.h>
#   endif
#else
#   include <windows.h>
#   include <ceddk.h>
#   include <Winbase.h>
#endif

// common
#include "osif_kernel.h"
#include "osif_functions_kernel.h"

//////////////////////////////////////////////////////////////////////
// os_if_write_port
// write to port
//////////////////////////////////////////////////////////////////////
void os_if_write_port(unsigned regist, unsigned portAddr)
{
#if LINUX
    outb(regist,portAddr); 
#else
    WRITE_PORT_UCHAR((PUCHAR)portAddr, (UCHAR)regist);
#endif
}

//////////////////////////////////////////////////////////////////////
// os_if_read_port
// read from port
//////////////////////////////////////////////////////////////////////
unsigned int os_if_read_port(unsigned portAddr)
{
#if LINUX
    return inb(portAddr);
#else
    return READ_PORT_UCHAR((PUCHAR)portAddr);
#endif
}

//////////////////////////////////////////////////////////////////////
// os_if_queue_task
// put task to queue/set event
//////////////////////////////////////////////////////////////////////
int os_if_queue_task(OS_IF_TASK_QUEUE_HANDLE *hnd)
{
#if LINUX
    #if LINUX_2_6
       return schedule_work(hnd); // ret nonzero if ok
    #else
       int ret = queue_task(hnd, &tq_immediate);
       mark_bh(IMMEDIATE_BH);
       return ret;
    #endif 
#else
#           pragma message("qqq os_if_queue_task...")
    SetEvent(hnd); // fifo empty event/ txTaskQ
#endif
}

//////////////////////////////////////////////////////////////////////
// os_if_queue_task
// put task to queue/set event
//////////////////////////////////////////////////////////////////////
int os_if_queue_task_not_default_queue(OS_IF_WQUEUE *wq, OS_IF_TASK_QUEUE_HANDLE *hnd)
{
#if LINUX
    #if LINUX_2_6
       return queue_work(wq, hnd); // ret nonzero if ok
    #else
        int ret = queue_task(hnd, &tq_immediate);
        mark_bh(IMMEDIATE_BH);
       return ret;
    #endif 
#else
#           pragma message("qqq os_if_queue_task...")
    SetEvent(hnd); // fifo empty event/ txTaskQ
#endif
}



//////////////////////////////////////////////////////////////////////
// os_if_init_waitqueue_head
// 
//////////////////////////////////////////////////////////////////////
void os_if_init_waitqueue_head(OS_IF_WAITQUEUE_HEAD *handle)
{
#if LINUX
    init_waitqueue_head(handle);
#else
    *handle = CreateEvent(NULL, TRUE, FALSE, NULL); 
#endif
}

//////////////////////////////////////////////////////////////////////
// os_if_init_waitqueue_entry
// 
//////////////////////////////////////////////////////////////////////
void os_if_init_waitqueue_entry(OS_IF_WAITQUEUE *wait)
{
#if LINUX
    #if LINUX_2_6
        init_waitqueue_entry(wait, current);
    #else        
        init_waitqueue_entry(wait, current);
    #endif
#else

#endif
}


//////////////////////////////////////////////////////////////////////
// os_if_add_wait_queue
// 
//////////////////////////////////////////////////////////////////////
void os_if_add_wait_queue(OS_IF_WAITQUEUE_HEAD *waitQ, OS_IF_WAITQUEUE *wait)
{
#if LINUX
    #if LINUX_2_6
       add_wait_queue(waitQ, wait);
    #else
       add_wait_queue(waitQ, wait);
    #endif
#else
#endif
}


//////////////////////////////////////////////////////////////////////
// os_if_remove_wait_queue
// 
//////////////////////////////////////////////////////////////////////
void os_if_remove_wait_queue(OS_IF_WAITQUEUE_HEAD *waitQ, OS_IF_WAITQUEUE *wait)
{
#if LINUX
    #if LINUX_2_6
        remove_wait_queue(waitQ, wait);
    #else
        remove_wait_queue(waitQ, wait);
    #endif
#else
#endif
}


//////////////////////////////////////////////////////////////////////
// os_if_wait_for_event_timeout
// 
//////////////////////////////////////////////////////////////////////
signed long os_if_wait_for_event_timeout(signed long timeout, OS_IF_WAITQUEUE *handle)
{
#if LINUX
    #if LINUX_2_6
        //return wait_event_interruptible_timeout(handle, condition, timeout);
        return schedule_timeout(timeout);
    #else
        return schedule_timeout(timeout);
    #endif
#else
    if(handle != 0) {
        WaitForSingleObject(handle, timeout);
        return 0; // qqq
    }
    // do not wakeup
    else {
        return 0;
    }
#endif
}

//////////////////////////////////////////////////////////////////////
// os_if_schedule_timeout_simple
// 
//////////////////////////////////////////////////////////////////////
signed long os_if_wait_for_event_timeout_simple(signed long timeout)
{
#if LINUX
    #if LINUX_2_6
        return schedule_timeout(timeout);
    #else
        return schedule_timeout(timeout);
    #endif
#else
    return 0;
#endif
}

//////////////////////////////////////////////////////////////////////
// os_if_schedule
// 
//////////////////////////////////////////////////////////////////////
void os_if_wait_for_event(OS_IF_WAITQUEUE_HEAD handle)
{
#if LINUX
    schedule();
#else
    // qqq
    WaitForSingleObject(handle, INFINITE);
#endif
}

//////////////////////////////////////////////////////////////////////
// os_if_wake_up_interruptible
// 
//////////////////////////////////////////////////////////////////////
void os_if_wake_up_interruptible(OS_IF_WAITQUEUE_HEAD *handle)
{
#if LINUX
    wake_up_interruptible(handle);
#else
    SetEvent(*handle);
#endif
}

//////////////////////////////////////////////////////////////////////
// os_if_up_sema
// 
//////////////////////////////////////////////////////////////////////
void os_if_up_sema(OS_IF_SEMAPHORE *var)
{
#if LINUX
#   if LINUX_2_6
        complete(var);
        //os_if_wake_up_interruptible(var);
#   else
        up(var);
#   endif
#else
        // WINCE
#endif
}

//////////////////////////////////////////////////////////////////////
// os_if_up_sema
// 
//////////////////////////////////////////////////////////////////////
void os_if_down_sema(OS_IF_SEMAPHORE *var)
{
#if LINUX
#   if LINUX_2_6
    wait_for_completion(var);
    //interruptible_sleep_on(var);
#   else
    down(var);
#   endif
#else
        // WINCE
#endif
}

//////////////////////////////////////////////////////////////////////
// os_if_init_sema
// 
//////////////////////////////////////////////////////////////////////
void os_if_init_sema(OS_IF_SEMAPHORE *var)
{
#if LINUX
#   if LINUX_2_6
    init_completion(var);
    //os_if_init_waitqueue_head(var);
#   else
    sema_init(var, 0);
#   endif
#else
        // WINCE
#endif
}

//////////////////////////////////////////////////////////////////////
// os_if_set_task_interruptible
// 
//////////////////////////////////////////////////////////////////////
void os_if_set_task_interruptible()
{
#if LINUX
    set_current_state(TASK_INTERRUPTIBLE);
#else
#endif
}

//////////////////////////////////////////////////////////////////////
// os_if_set_task_uninterruptible
// 
//////////////////////////////////////////////////////////////////////
void os_if_set_task_uninterruptible()
{
#if LINUX
    set_current_state(TASK_UNINTERRUPTIBLE);

#else
#endif
}
//////////////////////////////////////////////////////////////////////
// os_if_set_task_running
// 
//////////////////////////////////////////////////////////////////////
void os_if_set_task_running()
{
#if LINUX
    set_current_state(TASK_RUNNING);
#else
#endif
}

//////////////////////////////////////////////////////////////////////
// os_if_get_timeout_time
// 
//////////////////////////////////////////////////////////////////////
unsigned long os_if_get_timeout_time()
{
#if LINUX
    return (jiffies + 1 * HZ);
#else
    return (GetTickCount() + 10000);
#endif
}

//////////////////////////////////////////////////////////////////////
// os_if_do_get_time_of_day
// 
//////////////////////////////////////////////////////////////////////
void os_if_do_get_time_of_day(OS_IF_TIME_VAL *tv)
{
#if LINUX
    do_gettimeofday(tv);
#else
    long tmpTime = GetTickCount();
    tv->tv_sec  = tmpTime/1000;
    tv->tv_usec = tmpTime%1000*1000;
#endif
}

//////////////////////////////////////////////////////////////////////
// os_if_is_rec_busy
// 
//////////////////////////////////////////////////////////////////////
int os_if_is_rec_busy(int nr, volatile unsigned long* addr)
{
#if LINUX
    return test_and_set_bit(nr, addr);
#else
    // qqq
    return 1;
#endif
}

//////////////////////////////////////////////////////////////////////
// os_if_rec_not_busy
// 
//////////////////////////////////////////////////////////////////////
void os_if_rec_not_busy(int nr, volatile unsigned long* addr)
{
#if LINUX
    return clear_bit(nr, addr);
#else
    // qqq
#endif  
}

//////////////////////////////////////////////////////////////////////
// os_if_spin_lock
// 
//////////////////////////////////////////////////////////////////////
void os_if_spin_lock(OS_IF_LOCK* lock)
{
#if LINUX
    spin_lock(lock);
#else
#endif
}

//////////////////////////////////////////////////////////////////////
// os_if_spin_unlock
// 
//////////////////////////////////////////////////////////////////////
void os_if_spin_unlock(OS_IF_LOCK* lock)
{
#if LINUX
    spin_unlock(lock);
#else
#endif
}

//////////////////////////////////////////////////////////////////////
// os_if_irq_disable
// 
//////////////////////////////////////////////////////////////////////
void os_if_irq_disable(OS_IF_LOCK *lock)
{
// qqq should work for 2_4 too!
#if LINUX
    #if LINUX_2_6
        spin_lock_irq(lock);
    #else
        cli();
    #endif
#else
#endif
}

//////////////////////////////////////////////////////////////////////
// os_if_irq_enable
// 
//////////////////////////////////////////////////////////////////////
void os_if_irq_enable(OS_IF_LOCK *lock)
{
#if LINUX
    // qqq should work for 2_4 too!
    #if LINUX_2_6
        spin_unlock_irq(lock);
    #else
        sti();
    #endif
#else
#endif
}

//////////////////////////////////////////////////////////////////////
// os_if_irq_save
// 
//////////////////////////////////////////////////////////////////////
void os_if_irq_save(OS_IF_LOCK *lock, unsigned long flags)
{
#if LINUX
    // not needed in 2_4
    #if LINUX_2_6
        spin_lock_irqsave(lock, flags);
    #endif
#else
#endif

}

//////////////////////////////////////////////////////////////////////
// os_if_irq_restore
// 
//////////////////////////////////////////////////////////////////////
void os_if_irq_restore(OS_IF_LOCK *lock, unsigned long flags)
{
#if LINUX
    // not needed in 2_4
    #if LINUX_2_6
        spin_unlock_irqrestore(lock, flags);
    #endif
#else
#endif

}

//////////////////////////////////////////////////////////////////////
// os_if_get_user_data
// 
//////////////////////////////////////////////////////////////////////
int os_if_get_user_data(void * to, const void * from, OS_IF_SIZE n)
{
#if LINUX
    return copy_to_user(to, from, n);
#else
    memcpy(to, from, n);
    return 0;
#endif
}

//////////////////////////////////////////////////////////////////////
// os_if_set_user_data
// 
//////////////////////////////////////////////////////////////////////
int os_if_set_user_data(void * to, const void * from, OS_IF_SIZE n)
{
#if LINUX
    return copy_from_user(to, from, n);
#else
    memcpy(to, from, n);
    return 0;
#endif
}

//////////////////////////////////////////////////////////////////////
// os_if_set_int
// 
//////////////////////////////////////////////////////////////////////
int os_if_set_int(int val, int* dest)
{
#if LINUX
    return put_user(val, dest); 
#else
    return 0;
#endif
}

//////////////////////////////////////////////////////////////////////
// os_if_get_int
// 
//////////////////////////////////////////////////////////////////////
int os_if_get_int(int* val, int* dest)
{
#if LINUX
    return get_user(*val, dest);    
#else
    return 0;
#endif
}

//////////////////////////////////////////////////////////////////////
// os_if_get_long
// 
//////////////////////////////////////////////////////////////////////
int os_if_get_long(long* val, long* dest)
{
#if LINUX
    return get_user(*val, dest);    
#else
    return 0;
#endif
}



//////////////////////////////////////////////////////////////////////
// os_if_declare_task
// 
//////////////////////////////////////////////////////////////////////
OS_IF_WQUEUE* os_if_declare_task(char * name)
{
#if LINUX
#   if LINUX_2_6
        return create_workqueue(name);
#   else
        return 0;
#   endif
#else
        // wince
#endif
}

//////////////////////////////////////////////////////////////////////
// os_if_destroy_task
// 
//////////////////////////////////////////////////////////////////////
void os_if_destroy_task(OS_IF_WQUEUE *wQueue)
{
#if LINUX
#   if LINUX_2_6
        destroy_workqueue(wQueue);
#   else
#   endif
#else
        // wince
#endif
}

//////////////////////////////////////////////////////////////////////
// os_if_init_task
// 
//////////////////////////////////////////////////////////////////////
void os_if_init_task(OS_IF_TASK_QUEUE_HANDLE *taskQ, void *function, void *data)
{
#if LINUX
    #if LINUX_2_6
        INIT_WORK(taskQ, function, data);
    #else
        taskQ->routine  = function;
        taskQ->data     = data;
    #endif
#else
#endif
}

//////////////////////////////////////////////////////////////////////
// os_if_read_lock
// 
//////////////////////////////////////////////////////////////////////

void os_if_read_lock(rwlock_t *rw_lock, unsigned long flags)
{
#if LINUX
    // do nothing
#if LINUX_2_6
    read_lock_irqsave(rw_lock, flags);
#else
#endif
#else
#endif    
}

//////////////////////////////////////////////////////////////////////
// os_if_read_lock
// 
//////////////////////////////////////////////////////////////////////

void os_if_read_unlock(rwlock_t *rw_lock, unsigned long flags)
{
#if LINUX
    // do nothing
#if LINUX_2_6
    read_unlock_irqrestore(rw_lock, flags);
#else
#endif
#else
#endif    
}

//////////////////////////////////////////////////////////////////////
// os_if_write_lock
// 
//////////////////////////////////////////////////////////////////////

void os_if_write_lock(rwlock_t *rw_lock, unsigned long flags)
{
#if LINUX
    // do nothing
    #if LINUX_2_6
        write_lock_irqsave(rw_lock, flags);
    #endif
#else
#endif
}

//////////////////////////////////////////////////////////////////////
// os_if_write_lock
// 
//////////////////////////////////////////////////////////////////////

void os_if_write_unlock(rwlock_t *rw_lock, unsigned long flags)
{
#if LINUX
    // do nothing
    #if LINUX_2_6
        write_unlock_irqrestore(rw_lock, flags);
    #endif
#else
#endif
}
