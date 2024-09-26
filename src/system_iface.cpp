#include "system_iface.h"

int system_iface::allocate_tun()
{
    struct ifreq ifreq;
    if ( (iface_fd = open("/dev/net/tun", O_RDWR)) == -1 ) {
        std::cerr << "Can't open /dev/net/tun" << strerror(errno) << std::endl;
        return -1;
    }

    std::cout << "create " << name << " interface" << std::endl;

    memset(&ifreq, 0, sizeof(ifreq));
    ifreq.ifr_flags = IFF_TUN | IFF_NO_PI;

    strncpy(ifreq.ifr_name, name.c_str(), IFNAMSIZ);

    ifreq.ifr_flags |= IFF_MASTER;

    if ( ioctl(iface_fd, TUNSETIFF, &ifreq) == -1 ) {
        std::cerr << "ioctl TUNSETIFF" << strerror(errno) << std::endl;
        return -1;
    }

    return 1;
}

int system_iface::allocate_tap()
{
    struct ifreq ifreq;
    if ( (iface_fd = open("/dev/net/tun", O_RDWR)) == -1 ) {
        std::cerr << "Can't open /dev/net/tun" << strerror(errno) << std::endl;
        return -1;
    }

    std::cout << "create " << name << " interface" << std::endl;

    memset(&ifreq, 0, sizeof(ifreq));
    ifreq.ifr_flags = IFF_TAP | IFF_NO_PI;

    strncpy(ifreq.ifr_name, name.c_str(), IFNAMSIZ);

    ifreq.ifr_flags |= IFF_MASTER;

    if ( ioctl(iface_fd, TUNSETIFF, &ifreq) == -1 ) {
        std::cerr << "ioctl TUNSETIFF" << strerror(errno) << std::endl;
        return -1;
    }

    return 1;
}

int system_iface ::create_nl_socket()
{
    memset(&nl_addr, 0, sizeof(nl_addr));
    nl_addr.nl_family = AF_NETLINK;
    nl_addr.nl_groups = RTMGRP_LINK;
    nl_addr.nl_groups |= RTMGRP_IPV4_IFADDR;
    nl_addr.nl_groups |= RTMGRP_IPV6_IFADDR;
    nl_addr.nl_groups |= RTMGRP_IPV4_ROUTE;
    nl_addr.nl_groups |= RTMGRP_IPV6_ROUTE;

    // nl_addr.nl_pid = getpid();

    nl_socket = socket(AF_NETLINK, SOCK_RAW | SOCK_CLOEXEC | SOCK_NONBLOCK, NETLINK_ROUTE);
    if ( nl_socket == -1 ) {
        std::cerr << "Failed to create NL socket: " << strerror(errno) << std::endl;
        return -1;
    }
    // struct timeval tv;
    // tv.tv_sec = 1;
    // tv.tv_usec = 0;

    // Enable kernel filtering
    int optval = 1;
    if ( setsockopt(nl_socket, SOL_NETLINK, NETLINK_GET_STRICT_CHK, &optval, sizeof(optval)) < 0 ) {
        fprintf(stderr, "Netlink set socket option \"NETLINK_GET_STRICT_CHK\" failed: %s\n", strerror(errno));
        return EXIT_FAILURE;
    }

    if ( bind(nl_socket, (struct sockaddr *)&nl_addr, sizeof(nl_addr)) < 0 ) {
        std::cerr << "Failed to bind NL socket: " << strerror(errno) << std::endl;
        return -1;
    };
    return 1;
}

int system_iface::set_ip_addr(const std::string &addr, const std::string &mask)
{
    sockaddr_nl sa;
    memset(&sa, 0, sizeof(sockaddr_nl));

    sa.nl_family = AF_NETLINK;
    sa.nl_groups = RTMGRP_IPV4_IFADDR;

    int rc = nl_socket_handler::request_add_ip_addr(nl_socket, addr, mask, linux_interface_id);
    if ( rc < 0 ) {
        std::cout << "Failed to add IP: " << addr << " to interface " << name << "\t"
                  << nl_socket_handler::rc_to_string(rc) << ", rc = " << rc << std::endl;
    }
    return rc;
};

int system_iface::add_route(const std::string &dst_addr, const std::string &gw,  //
                            const uint8_t masklen, const uint32_t metric, const uint32_t rt_number, const uint8_t proto)
{
#ifdef _DEBUG
    std::cout << "try add route" << std::endl;
    std::cout << "\tdst: " << dst_addr << "/" << masklen << std::endl;
    std::cout << "\tgateway: " << gw << std::endl;
    std::cout << "\tmetric: " << metric << std::endl;
    std::cout << "\toif id: " << linux_interface_id << std::endl;
#endif
    int rc =
        nl_socket_handler::request_add_route(nl_socket, dst_addr, gw, masklen, metric, linux_interface_id, rt_number, proto);
    if ( rc ) {
        std::cout << nl_socket_handler::rc_to_string(rc) << std::endl;
        return -1;
    }
    return rc;
}

uint32_t system_iface::search_iface()
{
    linux_interface_id = search_iface(nl_socket, name);
    if ( not linux_interface_id ) {
        std::cout << "Can't find interface " << name << " into LINUX system" << std::endl;
    }
    return linux_interface_id;
}

uint32_t system_iface::search_iface(const int fd, const std::string& name){
    return nl_socket_handler::search_iface(fd, name);
}

bool system_iface::update_routes(linux_route &route)
{
    ssize_t msg_size = 0;
    char nl_sock_resp_buf[BUF_SIZE] = {0};

    struct nlmsghdr *nlh;
    while ( 1 ) {
        msg_size = recv(nl_socket, nl_sock_resp_buf, BUF_SIZE, 0);
        if ( msg_size <= 0 ) {
            break;
        }
        nlh = (struct nlmsghdr *)nl_sock_resp_buf;
        // If we received all data
        if ( nlh->nlmsg_type == NLMSG_DONE ) {
            break;
        }
        if ( nlh->nlmsg_type == RTM_DELROUTE ) {
            break;
        }
        if ( nlh->nlmsg_type == RTM_NEWROUTE ) {
            break;
        }
        if ( nl_addr.nl_groups == RTMGRP_IPV4_ROUTE ) {
            break;
        }
        if ( nl_addr.nl_groups == RTMGRP_IPV6_ROUTE ) {
            break;
        }
    }

    struct rtmsg *route_entry;
    struct rtattr *route_attribute;
    int route_attribute_len = 0;

    for ( ; NLMSG_OK(nlh, msg_size);
          nlh = NLMSG_NEXT(nlh, msg_size) ) {
        // Get the route data
        route_entry     = (struct rtmsg *)NLMSG_DATA(nlh);
        route.mask_len  = route_entry->rtm_dst_len;
        route.rt_number = route_entry->rtm_table;
        route.proto     = route_entry->rtm_protocol;

        // if ( route_entry->rtm_table != RT_TABLE_MAIN )
        //     continue;

        /* Get attributes of route_entry */
        route_attribute = (struct rtattr *)RTM_RTA(route_entry);

        /* Get the route atttibutes len */
        route_attribute_len = RTM_PAYLOAD(nlh);
        /* Loop through all attributes */
        for ( ; RTA_OK(route_attribute, route_attribute_len);
              route_attribute = RTA_NEXT(route_attribute, route_attribute_len) ) {
            /* Get the destination address */
            if ( route_attribute->rta_type == RTA_DST ) {
                if ( route_entry->rtm_family == AF_INET ) {

                    auto _dest      = (struct sockaddr_in *)&(route.dest);
                    _dest->sin_addr = *((in_addr *)RTA_DATA(route_attribute));

                } else {
                    auto _dest       = (struct sockaddr_in6 *)&(route.dest);
                    _dest->sin6_addr = *((in6_addr *)RTA_DATA(route_attribute));
                }

                route.dest.ss_family = route_entry->rtm_family;
                continue;
            }
            /* Get the gateway (Next hop) */
            if ( route_attribute->rta_type == RTA_GATEWAY ) {
                if ( route_entry->rtm_family == AF_INET ) {
                    auto _gw      = (struct sockaddr_in *)&(route.gw);
                    _gw->sin_addr = *((in_addr *)RTA_DATA(route_attribute));
                } else {
                    auto _gw       = (struct sockaddr_in6 *)&(route.gw);
                    _gw->sin6_addr = *((in6_addr *)RTA_DATA(route_attribute));
                }

                route.gw.ss_family = route_entry->rtm_family;
                continue;
            }

            if ( route_attribute->rta_type == RTA_PRIORITY ) {
                route.priority = *(uint8_t *)RTA_DATA(route_attribute);
                continue;
            }

            if ( route_attribute->rta_type == RTA_METRICS ) {
                route.metrics = *(uint8_t *)RTA_DATA(route_attribute);
                continue;
            }

            if ( route_attribute->rta_type == RTA_OIF ) {
                route.iface_id = *(uint8_t *)RTA_DATA(route_attribute);
                continue;
            }
        }

        if ( nlh->nlmsg_type == RTM_NEWROUTE ) {
            route.status = linux_route::e_status::NEW;
        };
        if ( nlh->nlmsg_type == RTM_DELROUTE ) {
            route.status = linux_route::e_status::DELETE;
        };
        if ( route.status == linux_route::e_status::EMPTY ) {
            continue;
        }

        // std::cout << route << std::endl;
    }

    return false;
}


int system_iface::set_iface_state(const bool up)
{
    std::cout << "request state changing to " << (up ? "UP" : "DOWN")
              << " for iface " << name << std::endl;
    return nl_socket_handler::request_updown(nl_socket, linux_interface_id, up);
}

int system_iface::add_to_vrf(const std::string &linux_vrf_name)
{
    int rc = -1;

    if ( not linux_interface_id ) {
        search_iface();
    }

    if ( vrf_index ) {  // already in vrf
        linux_rt_number = 0;
        vrf_index       = NO_SYSTEM_ID;
        vrf_name        = NONE;

        rc = nl_socket_handler::request_del_iface_from_vrf(nl_socket, linux_interface_id);
        if ( rc ) {
            std::cout << "Can't delete interface " << name << " from VRF " << vrf_name << std::endl;
            return -1;
        }
    }

    vrf_name = linux_vrf_name;
    vrf_index = search_iface(nl_socket, vrf_name);  // check the VRF exists
    if ( not vrf_index ) {
        std::cout << "Can't find VRF: " << vrf_name << " into linux system" << std::endl;
        return -1;
    }
    nl_socket_handler::request_updown(nl_socket, vrf_index, true); // vrf has to be up
    linux_rt_number = nl_socket_handler::get_rt_number_from_vrf_name(nl_socket, vrf_name); 
    rc = nl_socket_handler::request_add_iface_to_vrf(nl_socket, vrf_index, linux_interface_id);
    if ( rc ) {
        std::cout << "Can't add interface " << name << " to VRF " << vrf_name << std::endl;
        return -1;
    }

    return 0;
}

void system_iface::stop()
{
    if (created_vrf && vrf_index){
        nl_socket_handler::request_del_vrf(nl_socket, vrf_index);
    }
    if ( iface_fd > 0 ) {
        std::cout << "delete " << name << " interface" << std::endl;
        close(iface_fd);
    }
}
