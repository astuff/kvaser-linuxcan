/*
** Copyright 2002-2006 KVASER AB, Sweden.  All rights reserved.
*/

/*  Kvaser Linux Canlib */

#include <canlib.h>

#include <lapcan_data.h>
#include <lapcan_ioctl.h>
#include <canlib_data.h>
#include <canlib_util.h>
#include <osif_user.h>
#include <osif_functions_user.h>

#if LINUX
#   include <sys/types.h>
#   include <sys/stat.h>
#   include <fcntl.h>
#   include <sys/ioctl.h>
#   include <unistd.h>
#   include <lapcmds.h>
#   include <stdio.h>
#   include <errno.h>
#   include <signal.h>
#   include <pthread.h>
#   include <string.h>
#   include <sys/stat.h>
#else
#   include <stdio.h>
#endif

#if LINUX
#   if DEBUG
#      define DEBUGPRINT(args...) printf(args)
#   else
#      define DEBUGPRINT(args...)
#   endif
#else
#   if DEBUG
#      define DEBUGPRINT(i, args) printf(i, args)
#   else
#      define DEBUGPRINT(i, args)
#   endif
#endif




#include <VCanFunctions.h>

static HandleList *handleList;

#if LINUX
static const char *errorStrings [] = {

    [-canOK]                    = "No error",
    [-canERR_PARAM]             = "Error in parameter",
    [-canERR_NOMSG]             = "No messages available",
    [-canERR_NOTFOUND]          = "Specified device not found",
    [-canERR_NOMEM]             = "Out of memory",
    [-canERR_NOCHANNELS]        = "No channels avaliable",
    [-canERR_INTERRUPTED]       = "Interrupted by signal",
    [-canERR_TIMEOUT]           = "Timeout occurred",
    [-canERR_NOTINITIALIZED]    = "Library not initialized",
    [-canERR_NOHANDLES]         = "No more handles",
    [-canERR_INVHANDLE]         = "Handle is invalid",
    [-canERR_DRIVER]            = "CAN driver type not supported",
    [-canERR_TXBUFOFL]          = "Transmit buffer overflow",
    [-canERR_HARDWARE]          = "A hardware error was detected",
    [-canERR_DRIVERLOAD]        = "Can not load or open the device driver",
    [-canERR_NOCARD]            = "Card not found"
};
#else
static const char *errorStrings[] = {
    "No error",
    "Error in parameter",
    "No messages available",
    "Specified device not found",
    "Out of memory",
    "No channels avaliable",
    "Interrupted by signal",
    "Timeout occurred",
    "Library not initialized",
    "No more handles",
    "Handle is invalid",
    "",
    "CAN driver type not supported",
    "Transmit buffer overflow",
    "XXX",
    "A hardware error was detected",
    "",
    "",
    "",
    "",
    "",
    "",
    "",
    "Can not load or open the device driver",
    "",
    "",
    "Card not found"
            "",
            "Config not found",
            "",
            "",
            "",
};


#endif


//******************************************************
// Find out channel specific data                       
//******************************************************
canStatus getDevParams (int channel, char devName[], int *devChannel, CANOps **canOps, char officialName[])
{

#if LINUX
    int         chanCounter = 0;
    int         devCounter  = 0;
    int         found       = 0;
    struct stat stbuf;

    // add here when new driver and in cangetchannelnr

    //  ------- PCICAN -------
    for (devCounter = 0; !found; (devCounter++, chanCounter++)){
        snprintf(devName, DEVICE_NAME_LEN, "/dev/pcican%d", devCounter);
        if (stat(devName, &stbuf) == -1) {
            break;
        }
        if (chanCounter == channel) {
            *devChannel = channel;
            *canOps = &vCanOps;
            sprintf(officialName, "KVASER PCIcan channel %d", devCounter);
            found = 1;
        }       
    }

    //  ------- PCICANII -------
    for (devCounter = 0; !found; (devCounter++, chanCounter++)){
        snprintf(devName, DEVICE_NAME_LEN, "/dev/pcicanII%d", devCounter);
        if (stat(devName, &stbuf) == -1) {
            break;
        }
        if (chanCounter == channel) {
            *devChannel = channel;
            *canOps = &vCanOps;
            sprintf(officialName, "KVASER PCIcanII channel %d", devCounter);
            found = 1;
        }       
    }

    //  ------- LAPCAN -------
    for (devCounter = 0; !found; (devCounter++, chanCounter++)){
        snprintf(devName, DEVICE_NAME_LEN, "/dev/lapcan%d", devCounter);    
        if (stat(devName, &stbuf) == -1) {
            break;
        }
        if (chanCounter == channel) {
            *devChannel = channel;
            *canOps = &vCanOps;
            sprintf(officialName, "KVASER LAPcan channel %d", devCounter);
            found = 1;
        }
    }

    //  ------- USBCANII -------
    for (devCounter = 0; !found; (devCounter++, chanCounter++)){
        
        snprintf(devName, DEVICE_NAME_LEN, "/dev/usbcanII%d", devCounter);   
        //printf("looking for '%s' dev: %d| ch: %d\n",devName , devCounter, chanCounter); 
        if (stat(devName, &stbuf) == -1) {
            break;
        }
        if (chanCounter == channel) {
            *devChannel = channel;
            *canOps = &vCanOps;
            sprintf(officialName, "KVASER USBcanII channel %d", devCounter);
            found = 1;
        }
    }

    
    if (found) {
        return canOK;
    } else {
        DEBUGPRINT("return canERR_NOTFOUND\n");
        devName[0]  = 0;
        *devChannel = -1;
        *canOps      = NULL;
        return canERR_NOTFOUND;
    }

#else
    return canOK;
#endif
}


//******************************************************
// compare handles                       
//******************************************************
OS_IF_INLINE static int hndCmp (const void * hData1, const void * hData2) 
{
    return ((HandleData*)(hData1)) -> handle == 
        ((HandleData*)(hData2)) -> handle;
}


//******************************************************
// Find handle in list
//******************************************************
OS_IF_INLINE static HandleData * findHandle (HandleList **handleList, CanHandle hnd)
{
    HandleData dummyHandleData;
    dummyHandleData.handle = hnd; 
    return listFind(handleList, &dummyHandleData, &hndCmp);
}


//******************************************************
// remove handle from list
//******************************************************
OS_IF_INLINE static HandleData * removeHandle (HandleList **handleList, CanHandle hnd)
{
    HandleData dummyHandleData;
    dummyHandleData.handle = hnd; 
    return listRemove(handleList, &dummyHandleData, &hndCmp);
}




//
// API FUNCTIONS
//

//******************************************************
// opens a can channel
//******************************************************
CanHandle canOpenChannel (int channel, int flags)
{
    canStatus          status;
    static CanHandle   hnd       = 0;
    HandleData         *hData;
    
    hData = (HandleData*) malloc(sizeof(HandleData));
    if (hData == NULL) {
        DEBUGPRINT("ERROR: cannot allocate memory:\n");
        return canERR_NOMEM; 
    }

    memset(hData, 0, sizeof(HandleData));

    hData->readIsBlock      = 1;
    hData->writeIsBlock     = 1;
    hData->handle           = hnd;
    hData->isExtended       = flags & canWANT_EXTENDED;
    hData->wantExclusive    = flags & canWANT_EXCLUSIVE;
    
    status = getDevParams(channel, 
                          hData->deviceName,    
                          &hData->channelNr,    
                          &hData->canOps,
                          hData->deviceOfficialName);
    
    if (status < 0) {
        DEBUGPRINT("getDevParams ret %d\n", status);
        free(hData);
        return status;
    }
    
    status = hData->canOps->openChannel(hData);

    if (status < 0) {
        DEBUGPRINT("openChannel ret %d\n", status);
        free(hData);
        return status;
    }
    listInsertFirst(&handleList, hData);
    
    return hnd++;
}


//******************************************************
// close can channel
//******************************************************
int canClose (const CanHandle hnd)
{
    HandleData *hData;
    hData = removeHandle (&handleList, hnd);
    if (hData == NULL) return canERR_INVHANDLE;
    canSetNotify(hnd, NULL, 0, NULL);  
    if (OS_IF_IS_CLOSE_ERROR(OS_IF_CLOSE_HANDLE(hData -> fd))) return canERR_INVHANDLE;
    OS_IF_FREE_MEM(hData);
    return canOK;
}


//******************************************************
// go on bus
//******************************************************
canStatus canBusOn (const CanHandle hnd)
{
    HandleData *hData;
    hData = findHandle (&handleList, hnd);
    if (hData == NULL)  return canERR_INVHANDLE;
    return  (hData->canOps->busOn(hData));
}


//******************************************************
// go bus off
//******************************************************
canStatus canBusOff (const CanHandle hnd)
{
    HandleData *hData;

    hData = findHandle (&handleList, hnd);
    if (hData == NULL) return canERR_INVHANDLE;
    return hData->canOps->busOff(hData);
}


//******************************************************
// set bus parameters
//******************************************************
canStatus canSetBusParams (const CanHandle hnd, long freq, unsigned int tseg1,
                           unsigned int tseg2, unsigned int sjw, unsigned int noSamp,
                           unsigned int syncmode)
{
    canStatus ret; 
    HandleData *hData;

    hData = findHandle (&handleList, hnd);
    if (hData == NULL) return canERR_INVHANDLE;
    if (freq < 0) {
      ret = canTranslateBaud(&freq, &tseg1, &tseg2, &sjw, &noSamp, &syncmode);
      if (ret != canOK) return ret;
    }
    return hData->canOps->setBusParams(hData, freq, tseg1, tseg2, sjw, noSamp, syncmode);
}


//******************************************************
// get bus parameters
//******************************************************
canStatus canGetBusParams(const CanHandle hnd, long * freq, unsigned int * tseg1,
                          unsigned int * tseg2, unsigned int * sjw,
                          unsigned int * noSamp, unsigned int * syncmode)
{
    HandleData *hData;
  
    hData = findHandle (&handleList, hnd);
    if (hData == NULL) return canERR_INVHANDLE;
    return hData->canOps->getBusParams(hData, freq, tseg1, tseg2, sjw, noSamp, syncmode); 
}


//******************************************************
// set bus output control (silent/normal)
//******************************************************
canStatus canSetBusOutputControl (const CanHandle hnd, const unsigned int drivertype)
{
    HandleData *hData;

    hData = findHandle (&handleList, hnd);
    if (hData == NULL) return canERR_INVHANDLE;
    if (drivertype != canDRIVER_NORMAL && drivertype != canDRIVER_OFF &&
        drivertype != canDRIVER_SILENT && drivertype != canDRIVER_SELFRECEPTION) {
        return canERR_PARAM;
    }
    return hData->canOps->setBusOutputControl(hData, drivertype);
}


//******************************************************
// get bus output control (silent/normal)
//******************************************************
canStatus canGetBusOutputControl(const CanHandle hnd, unsigned int * drivertype)
{
    HandleData *hData;

    hData = findHandle (&handleList, hnd);
    if (hData == NULL) return canERR_INVHANDLE;
    return hData->canOps->getBusOutputControl(hData, drivertype);
}


//******************************************************
// set filters
//******************************************************
canStatus canAccept(const CanHandle hnd,
                    const long envelope,
                    const unsigned int flag)
{
    HandleData *hData;

    hData = findHandle (&handleList, hnd);
    if (hData == NULL) return canERR_INVHANDLE;
    return hData->canOps->accept(hData, envelope, flag);
}


//******************************************************
// read bus status
//******************************************************
canStatus canReadStatus(const CanHandle hnd,
                        unsigned long * const flags)
{
    HandleData *hData;
    hData = findHandle (&handleList, hnd);
    if (hData == NULL) return canERR_INVHANDLE;
  
    return hData->canOps->readStatus(hData, flags);
}


//******************************************************
// read the error counters
//******************************************************
canStatus canReadErrorCounters(const CanHandle hnd,
                               unsigned int * txErr,
                               unsigned int * rxErr,
                               unsigned int * ovErr)
{
    HandleData *hData;
    hData = findHandle (&handleList, hnd);
    if (hData == NULL) return canERR_INVHANDLE;
  
    return hData->canOps->readErrorCounters(hData, txErr, rxErr, ovErr);
}


//******************************************************
// write can message
//******************************************************
canStatus canWrite (const CanHandle hnd, long id, void *msgPtr,
                    unsigned int dlc, unsigned int flag)
{
    HandleData *hData;

    hData = findHandle (&handleList, hnd);
    if (hData == NULL) return canERR_INVHANDLE;
    return hData->canOps->write(hData, id, msgPtr, dlc, flag);
}


//******************************************************
// write can message and wait 
//******************************************************
canStatus canWriteWait (CanHandle hnd, long id, void *msgPtr,
                        unsigned int dlc, unsigned int flag, long timeout)
{
    HandleData *hData;

    hData = findHandle (&handleList, hnd);
    if (hData == NULL) return canERR_INVHANDLE;
    return hData->canOps->writeWait(hData, id, msgPtr, 
                                               dlc, flag, timeout);
}


//******************************************************
// read can message
//******************************************************
canStatus canRead (const CanHandle hnd, long * id, void * msgPtr, unsigned int * dlc,
                   unsigned int * flag, unsigned long * time)
{
    HandleData *hData;

    hData = findHandle (&handleList, hnd);
    if (hData == NULL) return canERR_INVHANDLE;
    return hData->canOps->read(hData, id, msgPtr, dlc, flag, time);
}


//*********************************************************
// read can message or wait until one appears or timeout
//*********************************************************
canStatus canReadWait (const CanHandle hnd, long * id, void * msgPtr, unsigned int * dlc,
                       unsigned int * flag, unsigned long * time, long timeout)
{
    HandleData *hData;

    hData = findHandle (&handleList, hnd);
    if (hData == NULL) return canERR_INVHANDLE;
    return hData->canOps->readWait(hData, id, msgPtr, dlc, flag, time, timeout);
}


//****************************************************************
// wait until all can messages on a circuit are sent or timeout
//****************************************************************
canStatus canWriteSync(const CanHandle hnd, unsigned long timeout)
{
    HandleData *hData;

    hData = findHandle (&handleList, hnd);
    if (hData == NULL) return canERR_INVHANDLE;

    return hData->canOps->writeSync(hData, timeout);
}


//******************************************************
// IOCTL
//******************************************************
canStatus canIoCtl(const CanHandle hnd, unsigned int func, void * buf, unsigned int buflen)
{
    HandleData *hData;
    hData = findHandle (&handleList, hnd);
    if (hData == NULL) return canERR_INVHANDLE;

    return hData->canOps->ioCtl(hData, func, buf, buflen);
}


//******************************************************
// read the time from hw
//******************************************************
canStatus canReadTimer(CanHandle hnd, unsigned long *time)
{
    HandleData *hData;
    hData = findHandle (&handleList, hnd);
    if (hData == NULL) return canERR_INVHANDLE;
  
    return hData->canOps->readTimer(hData, time);
}


//******************************************************
// translate from baud macro to bus params
//******************************************************
canStatus canTranslateBaud(long * const freq,
                           unsigned int * const tseg1,
                           unsigned int * const tseg2,
                           unsigned int * const sjw,
                           unsigned int * const nosamp,
                           unsigned int * const syncMode)
{
    switch (*freq) {
    case BAUD_1M:
        *freq     = 1000000L;
        *tseg1    = 4;
        *tseg2    = 3;
        *sjw      = 1;
        *nosamp   = 1;
        *syncMode = 0;
        break;
    case BAUD_500K:
        *freq     = 500000L;
        *tseg1    = 4;
        *tseg2    = 3;
        *sjw      = 1;
        *nosamp   = 1;
        *syncMode = 0;
        break;
    case BAUD_250K:
        *freq     = 250000L;
        *tseg1    = 4;
        *tseg2    = 3;
        *sjw      = 1;
        *nosamp   = 1;
        *syncMode = 0;
        break;
    case BAUD_125K:
        *freq     = 125000L;
        *tseg1    = 10;
        *tseg2    = 5;
        *sjw      = 1;
        *nosamp   = 1;
        *syncMode = 0;
        break;
    case BAUD_100K:
        *freq     = 100000L;
        *tseg1    = 10;
        *tseg2    = 5;
        *sjw      = 1;
        *nosamp   = 1;
        *syncMode = 0;
        break;
    case BAUD_62K:
        *freq     = 62500L;
        *tseg1    = 10;
        *tseg2    = 5;
        *sjw      = 1;
        *nosamp   = 1;
        *syncMode = 0;
        break;
    case BAUD_50K:
        *freq     = 50000L;
        *tseg1    = 10;
        *tseg2    = 5;
        *sjw      = 1;
        *nosamp   = 1;
        *syncMode = 0;
        break;
    default:
        return canERR_PARAM;
    }
    return canOK;
}


//******************************************************
// get error text
//******************************************************
canStatus canGetErrorText(canStatus err,
                          char * buf,
                          unsigned int bufsiz)
{
    signed char code;

    code = (signed char)(err & 0xFF);

    if (!buf || bufsiz == 0) return canOK;
    if ((code <= 0) && (-code < sizeof(errorStrings)/sizeof(char*))) {
        if (errorStrings [-code] == NULL){
            snprintf(buf, bufsiz, "Unknown error (%d)", (int)code);
        } else {
            strncpy(buf, errorStrings[-code], bufsiz);  
        }
    } else {
        strncpy(buf, "This is not an error code", bufsiz);
    }
    buf[bufsiz-1] = '\0';  
    return canOK;
}

    
//******************************************************
// get library version
//******************************************************
unsigned short canGetVersion (void)
{
    return (CANLIB_MAJOR_VERSION<<8) + CANLIB_MINOR_VERSION;
}

/*
unsigned int canGetVersionEx (unsigned int itemCode)
{
    unsigned int version = 0;
    
    switch (itemCode) {
        // build number
        case 0:
            //version = BUILD_NUMBER;
        break;
        
        default:

        break;
    }
    
    return version;
}
*/


//******************************************************
// get the total number of channels
//******************************************************
canStatus canGetNumberOfChannels (int *channelCount)
{
    //
     // This function has to be modified if we have other cards than
     // LAPcan, pcican, pcicanII and usbcanII
     // For now, we just count the number of /dev/lapcan%d, /dev/pcican%d, 
     // /dev/pcicanII%d, /dev/usbcanII%d files, where %d is numbers between
     // 0 and 255
    // 

    int tmpCount = 0;
    int cardNr;
    char filename[DEVICE_NAME_LEN+1];

    // add here when new driver maybe we can read the dev names
    // from some struct? THIS IS SLOW
    for(cardNr = 0; cardNr <= 255; cardNr++) { // There are 256 minor inode numbers  
        snprintf(filename,  DEVICE_NAME_LEN, "/dev/lapcan%d", cardNr);
        if (os_if_access(filename, F_OK) == 0) {
            tmpCount++;  // Check for existance 
        }
        else {
            // this implies that there can be no gaps in the numbering. 
            break;
        }
    }
    for(cardNr = 0; cardNr <= 255; cardNr++) { // There are 256 minor inode numbers 
        snprintf(filename,  DEVICE_NAME_LEN, "/dev/pcican%d", cardNr);
        if (os_if_access(filename, F_OK) == 0) {
            tmpCount++;  // Check for existance 
        }
        else {
            // this implies that there can be no gaps in the numbering. 
            break;
        }
    }
    for(cardNr = 0; cardNr <= 255; cardNr++) { // There are 256 minor inode numbers 
        snprintf(filename,  DEVICE_NAME_LEN, "/dev/pcicanII%d", cardNr);
        if (os_if_access(filename, F_OK) == 0) {
            tmpCount++;  // Check for existance 
        }
        else {
            // this implies that there can be no gaps in the numbering. 
            break;
        }
    }
    for(cardNr = 0; cardNr <= 255; cardNr++) { // There are 256 minor inode numbers    
        snprintf(filename,  DEVICE_NAME_LEN, "/dev/usbcanII%d", cardNr);
        if (os_if_access(filename, F_OK) == 0) {
            tmpCount++;  // Check for existance 
        }
        else {
            // this implies that there can be no gaps in the numbering. 

            // qqq, What happens if there are several usbcanIIs connected and the
            // user disconnect one with low numbers. Does hotplug reenumerate the
            // usbcanIIs and give them new numbers?
            break;
        }
    }
    
    *channelCount = tmpCount;
    return canOK;
}


//******************************************************
// Find out channel specific data                       
//******************************************************
canStatus canGetChannelData (int channel, int item, void *buffer, size_t bufsize)
{
    canStatus status;
    HandleData hData;
    status = getDevParams(channel, 
                          hData.deviceName,    
                          &hData.channelNr,    
                          &hData.canOps,
                          hData.deviceOfficialName);
    
    if (status < 0) return status;
      
    switch(item) {
        case canCHANNELDATA_CHANNEL_NAME:
        strcpy(buffer, hData.deviceOfficialName);
        bufsize = strlen(hData.deviceOfficialName);
        return canOK;
    default:

        return hData.canOps->getChannelData(channel, item, buffer, bufsize);
    }
}


//******************************************************
// set notification callback
//******************************************************
canStatus canSetNotify (const CanHandle hnd, void (*callback) (canNotifyData *), 
                        unsigned int notifyFlags, void * tag)
     // 
      // Notification is done by filtering out interresting messages and
      // doing a blocked read from a thread
     //
{
    HandleData *hData;

    hData = findHandle (&handleList, hnd);
    if (hData == NULL) return canERR_INVHANDLE;
    if (notifyFlags == 0 || callback == NULL) {
        // We want to shut off notification, close file and clear callback 
#if LINUX
        pthread_cancel((pthread_t)hData -> notifyThread);

        // Wait for thread to finish 
        pthread_join((pthread_t)hData -> notifyThread, NULL);
#else
        ExitThread(hData -> notifyThread);
#endif
        if (hData -> notifyFd != 0) {
            OS_IF_CLOSE_HANDLE(hData -> notifyFd);
        }
        hData -> notifyFd = 0;
        callback = NULL;
        return canOK;
    }
    hData -> notifyData.tag = tag;
    return hData->canOps->setNotify(hData, callback, notifyFlags);
}


//******************************************************
// initialize library
//******************************************************
void canInitializeLibrary()
{
    return;
}


