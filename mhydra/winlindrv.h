
#define KSPIN_LOCK              spinlock_t
#define KIRQL                   unsigned long //irqflags
#define INIT_SPINLOCK(x)        spin_lock_init((x))
#define ACQUIRE_SPINLOCK(x, y)  spin_lock_irqsave((x), (y))
#define RELEASE_SPINLOCK(x, y)  spin_unlock_irqrestore((x), (y))

#define MHYDRA_SEND_WAIT(card, cmd, reply, cmdNr, transId, timeout) \
        mhydra_send_and_wait_reply((card), (cmd), (reply), (cmdNr), (transId), SKIP_ERROR_EVENT)

#define MALLOC(x)               kmalloc((x), GFP_KERNEL)
#define FREE(x)                 kfree((x))
#define SHORT_TIMEOUT           //does not matter for now
#define LONG_TIMEOUT
#define CopyMemory(x, y, z)     memcpy((x), (y), (z))
#define FillMemory(x, y, z)     memset((x), (y), (z))

typedef int DRV_STATUS;
#define DRV_STATUS_NOT_IMPLEMENTED            VCAN_STAT_NOT_IMPLEMENTED
#define DRV_STATUS_INVALID_PARAMETER          VCAN_STAT_BAD_PARAMETER
#define DRV_STATUS_NO_MEMORY                  VCAN_STAT_NO_MEMORY
#define DRV_STATUS_UNSUCCESSFUL               VCAN_STAT_FAIL
#define DRV_STATUS_SUCCESS                    VCAN_STAT_OK


//wrapper for copy_to_user and copy_from_user
int copy_memory_to   (void* to, void* from, unsigned int n);
int copy_memory_from (void* to, void* from, unsigned int n);
