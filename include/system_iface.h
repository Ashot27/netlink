#ifndef PROJECT_SYS_IFACE_H
#define PROJECT_SYS_IFACE_H

#include <fcntl.h> /* O_RDWR */
#include <linux/if_tun.h>
#include <linux/rtnetlink.h>
#include <sys/ioctl.h>
#include <unistd.h> /* close */
#include <iostream>

#include "nl_socket_handler.h"
#include "linux_route.h"

struct system_iface
{
    bool ip_is_set               = false;
    bool set_as_default_gw       = false;
    std::string name             = NONE;
    std::string ip_addr          = NONE;
    uint32_t linux_interface_id  = NO_SYSTEM_ID;
    uint32_t id_in_config        = -1;
    bool up                      = false;
    struct sockaddr_nl nl_addr;

    std::string vrf_name     = NONE;
    uint32_t vrf_index       = NO_SYSTEM_ID;
    bool created_vrf         = false;  // true if the program create VRF;
    uint32_t linux_rt_number = 0;

    int iface_fd  = 0;
    int nl_socket = 0;

    system_iface(const std::string name, const std::string ip_addr, const bool already_allocated = true, const bool is_tap = true)
        : name(name), ip_addr(ip_addr)
    {
        if ( not already_allocated ) {
            if ( is_tap ) {
                allocate_tap();
            } else {
                allocate_tun();
            }
        }
        create_nl_socket();
    };

    ~system_iface()
    {
        std::cout<< "Stop iface: " << name << std::endl;
        stop();
    }

    void operator= (system_iface const &) = delete; // we won't copy file descripors for allocated interface and nl socket
    system_iface(system_iface const &)    = delete;

    int allocate_tun ();
    int allocate_tap ();
    int create_nl_socket ();

    int set_iface_state (const bool up = true);
    int set_ip_addr (const std::string &addr, const std::string &mask);
    int add_route (const std::string &dst_addr, const std::string &gw, const uint8_t masklen, const uint32_t metric, const uint32_t rt_number, const uint8_t proto = RTPROT_STATIC);
    uint32_t search_iface ();
    static uint32_t search_iface (const int fd, const std ::string &name);
    bool update_routes (linux_route &route);
    int add_to_vrf (const std::string &linux_vrf_name);

    void stop ();
};

#endif  //PROJECT_SYS_IFACE_H