/*
** Copyright 2002-2006 KVASER AB, Sweden.  All rights reserved.
*/

#include "osif_functions_user.h"

#if LINUX
#   include <sys/ioctl.h>
#   include <unistd.h>
#   include <asm/io.h>
#else

#endif


// common
#include <stdio.h>





//////////////////////////////////////////////////////////////////////
// os_if_ioctl_read
// let subsystem fill out the buffer
// IOCTL
//////////////////////////////////////////////////////////////////////
int os_if_ioctl_read(   OS_IF_FILE_HANDLE  fd,
                        unsigned int       ioctl_code,
                        void               *in_buffer,
                        unsigned int       in_bufsize)
{
#if LINUX
    return ioctl(fd, ioctl_code, in_buffer);
#else
    BOOL ret;
    unsigned int ret_bytes;
    ret = DeviceIoControl(fd, ioctl_code, in_buffer, in_bufsize, NULL, 0, &ret_bytes, NULL);
    if (ret == FALSE)
        return -1;
    else
        return ret_bytes;
#endif
}


//////////////////////////////////////////////////////////////////////
// os_if_ioctl_write
// send something to the subsystem
// IOCTL
//////////////////////////////////////////////////////////////////////
int os_if_ioctl_write(  OS_IF_FILE_HANDLE  fd,
                        unsigned int       ioctl_code,
//                        void               *in_buffer,
//                        unsigned int       in_bufsize,
                        void               *out_buffer,
                        unsigned int       out_bufsize)
{
#if LINUX
    return ioctl(fd, ioctl_code, out_buffer);
#else
    BOOL ret;
    unsigned int ret_bytes;
    
    ret = DeviceIoControl(fd, ioctl_code, NULL, 0, out_buffer, out_bufsize, &ret_bytes, NULL);
    if (ret == FALSE)
        return -1;
    else
        return ret_bytes;
#endif
}


//////////////////////////////////////////////////////////////////////
// os_if_access
//
//////////////////////////////////////////////////////////////////////

int os_if_access(char * fileName, int code)
{
    if (code != F_OK) return -1;
    else {
#if LINUX
        return access(fileName, code);
#else
        HANDLE fd;
        WCHAR tmpFileName[MAX_PATH];
        // qqq will this work?
        wsprintf(tmpFileName,"%S", fileName);
        fd = CreateFile(tmpFileName, GENERIC_READ, 0, NULL, OPEN_EXISTING, 0, NULL);
        if (fd != INVALID_HANDLE_VALUE) {
            CloseHandle(fd);
            return 0;
        }
        else
            return -2;
#endif
    }
}




















#if LINUX


/*
// in linux the outbuffer is ignored
int OS_IF_IOCTL(OS_IF_FILE_HANDLE fd, unsigned int ioctl_code, void* in_buffer, unsigned int in_bufsize,
                void * out_buffer, unsigned int out_bufsize)
{
    return ioctl(fd, ioctl_code, in_buffer);
}


#else

int OS_IF_IOCTL(OS_IF_FILE_HANDLE fd, unsigned int ioctl_code, void* in_buffer,
                unsigned int in_bufsize, void * out_buffer, unsigned int out_bufsize)
{
    BOOL ret;
    unsigned int ret_bytes;
    ret = DeviceIoControl(fd, ioctl_code, in_buffer, in_bufsize, out_buffer, out_bufsize, &ret_bytes, NULL);
    if (ret == FALSE)
        return -1;
    else
        return ret_bytes;
}
*/

#endif
