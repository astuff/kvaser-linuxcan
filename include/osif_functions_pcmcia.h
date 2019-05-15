#ifndef OSIF_FUNCTIONS_PCMCIA_H_
#define OSIF_FUNCTIONS_PCMCIA_H_

#   if LINUX_2_6
#   else
#       include <pcmcia/driver_ops.h>
#       include <pcmcia/bus_ops.h>
#   endif

/*
** Copyright 2002-2005 KVASER AB, Sweden.  All rights reserved.
*/

    typedef dev_node_t            OS_IF_DEVICE_CONTEXT_NODE;
    typedef dev_link_t            OS_IF_CARD_CONTEXT;
    typedef event_callback_args_t OS_IF_EVENT_PARAM;
    typedef client_handle_t       OS_IF_CLIENT_HND;
    typedef client_reg_t          OS_IF_CLIENT_REG;
    typedef window_handle_t       OS_IF_WIN_HND;
    typedef win_req_t             OS_IF_WIN_REQ;
    typedef io_req_t              OS_IF_IO_REQ;
    typedef irq_req_t             OS_IF_IRQ_REQ;
    typedef config_req_t          OS_IF_CONFIG_REQ;
    typedef config_info_t         OS_IF_CONFIG_INFO;
    typedef servinfo_t            OS_IF_SERVINFO;
    typedef error_info_t          OS_IF_ERR_INFO;
    typedef tuple_t               OS_IF_TUPLE;
    typedef cisparse_t            OS_IF_CISPARSE;
    typedef memreq_t              OS_IF_MEM_REQ;
    typedef event_t               OS_IF_EVENT;

#if LINUX_2_6
       typedef struct pcmcia_driver    OS_IF_DEV_INFO;
#else
       typedef dev_info_t              OS_IF_DEV_INFO;
#endif


//////////////////////////////////////////////////////////////////////
// pcmcia functions
int os_if_serv_register_client(OS_IF_CLIENT_HND *hnd, OS_IF_CLIENT_REG *reg);
int os_if_serv_deregister_client(OS_IF_CLIENT_HND hnd);
int os_if_serv_release_window(OS_IF_WIN_HND hnd);
int os_if_serv_request_window(OS_IF_CLIENT_HND *hnd, OS_IF_WIN_REQ *req, OS_IF_WIN_HND *wh);
int os_if_serv_release_configuration(OS_IF_CLIENT_HND hnd);
int os_if_serv_release_io(OS_IF_CLIENT_HND hnd, OS_IF_IO_REQ *req);
int os_if_serv_request_io(OS_IF_CLIENT_HND hnd, OS_IF_IO_REQ *req);
int os_if_serv_release_irq(OS_IF_CLIENT_HND hnd, OS_IF_IRQ_REQ *req);
int os_if_serv_request_irq(OS_IF_CLIENT_HND hnd, OS_IF_IRQ_REQ *req);
int os_if_serv_request_configuration(OS_IF_CLIENT_HND hnd, OS_IF_CONFIG_REQ *req);
int os_if_serv_get_card_services_info(OS_IF_SERVINFO *serv);
int os_if_serv_report_error(OS_IF_CLIENT_HND hnd, OS_IF_ERR_INFO *err);
int os_if_serv_get_configuration_info(OS_IF_CLIENT_HND hnd, OS_IF_CONFIG_INFO *conf);
int os_if_serv_get_first_tuple(OS_IF_CLIENT_HND hnd, OS_IF_TUPLE *tuple);
int os_if_serv_get_next_tuple(OS_IF_CLIENT_HND hnd, OS_IF_TUPLE *tuple);
int os_if_serv_get_tuple_data(OS_IF_CLIENT_HND hnd, OS_IF_TUPLE *tuple);
int os_if_serv_parse_tuple(OS_IF_CLIENT_HND hnd, OS_IF_TUPLE *tuple, OS_IF_CISPARSE *parse);
int os_if_serv_map_mem_page(OS_IF_WIN_HND win, OS_IF_MEM_REQ *req);
int os_if_register_driver(OS_IF_DEV_INFO *devInfo, void *atth, void *deatth);
void os_if_unregister_driver(OS_IF_DEV_INFO *devInfo);

#endif //OSIF_FUNCTIONS_PCMCIA_H_
