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
#   include <pcmcia/cs_types.h>
#   include <pcmcia/cs.h>
#   include <pcmcia/cistpl.h>
#   include <pcmcia/cisreg.h>
#   include <pcmcia/cs_types.h>
#   include <pcmcia/ds.h>


#else
#   include <windows.h>
#   include <ceddk.h>
#   include <Winbase.h>
#endif



// common
#include "osif_kernel.h"
#include "osif_functions_kernel.h"
#include "osif_functions_pcmcia.h"




//////////////////////////////////////////////////////////////////////
// os_if_serv_register_client
// 
//////////////////////////////////////////////////////////////////////
int os_if_serv_register_client(OS_IF_CLIENT_HND *hnd, OS_IF_CLIENT_REG *reg) {
#if LINUX
    #if LINUX_2_6
        return pcmcia_register_client(hnd, reg);
    #else
        return CardServices(RegisterClient, hnd, reg);
    #endif
#endif
}

//////////////////////////////////////////////////////////////////////
// os_if_serv_deregister_client
// 
//////////////////////////////////////////////////////////////////////
int os_if_serv_deregister_client(OS_IF_CLIENT_HND hnd) {
#if LINUX
#if LINUX_2_6
    return pcmcia_deregister_client(hnd);
#else
    return CardServices(DeregisterClient, hnd);
#endif
#endif
}

//////////////////////////////////////////////////////////////////////
// os_if_serv_release_window
// 
//////////////////////////////////////////////////////////////////////
int os_if_serv_release_window(OS_IF_WIN_HND hnd) {
#if LINUX
#if LINUX_2_6
    return pcmcia_release_window(hnd);
#else
    return CardServices(ReleaseWindow, hnd);
#endif
#endif
}

//////////////////////////////////////////////////////////////////////
// os_if_serv_request_window
// 
//////////////////////////////////////////////////////////////////////
int os_if_serv_request_window(OS_IF_CLIENT_HND *hnd, OS_IF_WIN_REQ *req, OS_IF_WIN_HND *wh) {
#if LINUX
#if LINUX_2_6
    return pcmcia_request_window(hnd, req, wh);
#else
    return CardServices(RequestWindow, hnd, req);
#endif
#endif
}

//////////////////////////////////////////////////////////////////////
// os_if_serv_release_configuration
// 
//////////////////////////////////////////////////////////////////////
int os_if_serv_release_configuration(OS_IF_CLIENT_HND hnd) {
#if LINUX
#if LINUX_2_6
    return pcmcia_release_configuration(hnd);
#else
    return CardServices(ReleaseConfiguration, hnd);
#endif
#endif
}

//////////////////////////////////////////////////////////////////////
// os_if_serv_release_io
// 
//////////////////////////////////////////////////////////////////////
int os_if_serv_release_io(OS_IF_CLIENT_HND hnd, OS_IF_IO_REQ *req) {
#if LINUX
#if LINUX_2_6
    return pcmcia_release_io(hnd, req);
#else
    return CardServices(ReleaseIO, hnd, req);
#endif
#endif
}

//////////////////////////////////////////////////////////////////////
// os_if_serv_request_io
// 
//////////////////////////////////////////////////////////////////////
int os_if_serv_request_io(OS_IF_CLIENT_HND hnd, OS_IF_IO_REQ *req) {
#if LINUX
#if LINUX_2_6
    return pcmcia_request_io(hnd, req);
#else
    return CardServices(RequestIO, hnd, req);
#endif
#endif
}

//////////////////////////////////////////////////////////////////////
// os_if_serv_release_irq
// 
//////////////////////////////////////////////////////////////////////
int os_if_serv_release_irq(OS_IF_CLIENT_HND hnd, OS_IF_IRQ_REQ *req) {
#if LINUX
#if LINUX_2_6
    return pcmcia_release_irq(hnd, req);
#else
    return CardServices(ReleaseIRQ, hnd, req);
#endif
#endif
}

//////////////////////////////////////////////////////////////////////
// os_if_serv_request_irq
// 
//////////////////////////////////////////////////////////////////////
int os_if_serv_request_irq(OS_IF_CLIENT_HND hnd, OS_IF_IRQ_REQ *req) {
#if LINUX
#if LINUX_2_6
    return pcmcia_request_irq(hnd, req);
#else
    return CardServices(RequestIRQ, hnd, req);
#endif
#endif
}

//////////////////////////////////////////////////////////////////////
// os_if_serv_request_configuration
// 
//////////////////////////////////////////////////////////////////////
int os_if_serv_request_configuration(OS_IF_CLIENT_HND hnd, OS_IF_CONFIG_REQ *req) {
#if LINUX
#if LINUX_2_6
    return pcmcia_request_configuration(hnd, req);
#else
    return CardServices(RequestConfiguration, hnd, req);
#endif
#endif
}

//////////////////////////////////////////////////////////////////////
// os_if_serv_get_card_services_info
// 
//////////////////////////////////////////////////////////////////////
int os_if_serv_get_card_services_info(OS_IF_SERVINFO *serv) {
#if LINUX
#if LINUX_2_6
    return pcmcia_get_card_services_info(serv);
#else
    return CardServices(GetCardServicesInfo, serv);
#endif
#endif
}

//////////////////////////////////////////////////////////////////////
// os_if_serv_report_error
// 
//////////////////////////////////////////////////////////////////////
int os_if_serv_report_error(OS_IF_CLIENT_HND hnd, OS_IF_ERR_INFO *err) {
#if LINUX
#if LINUX_2_6
    return pcmcia_report_error(hnd, err);
#else
    return CardServices(ReportError, hnd, err);
#endif
#endif
}

//////////////////////////////////////////////////////////////////////
// os_if_get_configuration_info
// 
//////////////////////////////////////////////////////////////////////
int os_if_serv_get_configuration_info(OS_IF_CLIENT_HND hnd, OS_IF_CONFIG_INFO *conf) {
#if LINUX
#if LINUX_2_6
    return pcmcia_get_configuration_info(hnd, conf);
#else
    return CardServices(GetConfigurationInfo, hnd, conf);
#endif
#endif    
}

//////////////////////////////////////////////////////////////////////
// os_if_serv_get_first_tuple
// 
//////////////////////////////////////////////////////////////////////
int os_if_serv_get_first_tuple(OS_IF_CLIENT_HND hnd, OS_IF_TUPLE *tuple) {
#if LINUX
#if LINUX_2_6
    return pcmcia_get_first_tuple(hnd, tuple);
#else
    return CardServices(GetFirstTuple, hnd, tuple);
#endif
#endif 
}

//////////////////////////////////////////////////////////////////////
// os_if_get_next_tuple
// 
//////////////////////////////////////////////////////////////////////
int os_if_serv_get_next_tuple(OS_IF_CLIENT_HND hnd, OS_IF_TUPLE *tuple) {
#if LINUX
#if LINUX_2_6
    return pcmcia_get_next_tuple(hnd, tuple);
#else
    return CardServices(GetNextTuple, hnd, tuple);
#endif
#endif 
}

//////////////////////////////////////////////////////////////////////
// os_if_serv_get_tuple_data
// 
//////////////////////////////////////////////////////////////////////
int os_if_serv_get_tuple_data(OS_IF_CLIENT_HND hnd, OS_IF_TUPLE *tuple) {
#if LINUX
#if LINUX_2_6
    return pcmcia_get_tuple_data(hnd, tuple);
#else
    return CardServices(GetTupleData, hnd, tuple);
#endif
#endif 
}

//////////////////////////////////////////////////////////////////////
// os_if_serv_parse_tuple
// 
//////////////////////////////////////////////////////////////////////
int os_if_serv_parse_tuple(OS_IF_CLIENT_HND hnd, OS_IF_TUPLE *tuple, OS_IF_CISPARSE *parse) {
#if LINUX
#if LINUX_2_6
    return pcmcia_parse_tuple(hnd, tuple, parse);
#else
    return CardServices(ParseTuple, hnd, tuple, parse);
#endif
#endif 
}

//////////////////////////////////////////////////////////////////////
// os_if_serv_map_mem_page
// 
//////////////////////////////////////////////////////////////////////
int os_if_serv_map_mem_page(OS_IF_WIN_HND win, OS_IF_MEM_REQ *req) {
#if LINUX
#if LINUX_2_6
    return pcmcia_map_mem_page(win, req);
#else
    return CardServices(MapMemPage, win, req);
#endif
#endif 
}

//////////////////////////////////////////////////////////////////////
// os_if_register_driver
// 
//////////////////////////////////////////////////////////////////////
int os_if_register_driver(OS_IF_DEV_INFO *devInfo, void *atth, void *deatth)
{
#if LINUX
#   if LINUX_2_6
        return pcmcia_register_driver(devInfo);
#   else
        return register_pccard_driver(devInfo, atth, deatth);
#   endif
#else
    // wince
#endif
}

//////////////////////////////////////////////////////////////////////
// os_if_unregister_driver
// 
//////////////////////////////////////////////////////////////////////
void os_if_unregister_driver(OS_IF_DEV_INFO *devInfo)
{
#if LINUX
#   if LINUX_2_6
    pcmcia_unregister_driver(devInfo);
#   else
    unregister_pccard_driver(devInfo);
#   endif
#else
    // wince
#endif
}

