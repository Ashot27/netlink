#include "linux_route.h"

linux_route linux_route::parse_route_from_nl_resp_hdr(nlmsghdr *nlh)
{
    linux_route route;
    rtmsg *route_entry = (struct rtmsg *)NLMSG_DATA(nlh);
    route.mask_len     = route_entry->rtm_dst_len;
    route.rt_number    = route_entry->rtm_table;  // could be overwriten if there are RTA_TABLE attribute
    route.proto        = route_entry->rtm_protocol;

    // if ( route_entry->rtm_table != RT_TABLE_MAIN )
    //     continue;

    /* Get attributes of route_entry */
    rtattr *route_attribute = (struct rtattr *)RTM_RTA(route_entry);

    /* Get the route atttibutes len */
    int route_attribute_len = RTM_PAYLOAD(nlh);
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

        if ( route_attribute->rta_type == RTA_TABLE ) {
            route.rt_number = *(uint32_t *)RTA_DATA(route_attribute);
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
    return route;

    // std::cout << route << endl;
}

int linux_routing_table::update(const linux_route &route, const bool need_to_check)
{
    std::pair<bool, size_t> index = {false, 0};
    if ( need_to_check ) {
        index = find(route);  // O(n)
    }
    if ( (not index.first) and route.status == linux_route::e_status::NEW ) {
        _table.push_back(route);
        return 0;
    }
    if ( index.first and route.status == linux_route::e_status::DELETE ) {
        _table.erase(_table.begin() + index.second);
        return 0;
    }

    return -1;
};

std::pair<bool, size_t> linux_routing_table::find(const linux_route &route) const
{
    for ( size_t i = 0, n = _table.size(); i < n; ++i ) {
        if ( _table[i] == route ) {
            return {true, i};
        }
    }
    return {false, 0};
};

void linux_routing_table::change_vrf_name(const std::string &name)
{
    const_cast<std::string &>(_vrf_name) = name;
    return;
}

std::vector<linux_route> *linux_routing_table::get()
{
    return &_table;
};

uint32_t linux_routing_table::size() const
{
    return _table.size();
}

uint32_t linux_routing_table::get_routes_from_nl_resp(const char *nl_sock_resp_buf, ssize_t msg_size)
{
    size_t counter = 0;
    nlmsghdr *nlh  = (nlmsghdr *)nl_sock_resp_buf;
    for ( ; NLMSG_OK(nlh, msg_size); nlh = NLMSG_NEXT(nlh, msg_size) ) {
        // Get the route data
        linux_route route = linux_route::parse_route_from_nl_resp_hdr(nlh);
        update(route, false);  // no need to check. The kernel stores unique routes
        ++counter;
    }
    return counter;
}

int linux_rt_manager::open_nl_socket()
{
    memset(&nl_addr, 0, sizeof(nl_addr));
    nl_addr.nl_family = AF_NETLINK;
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

void linux_rt_manager::stop()
{
    if ( nl_socket > 0 ) {
        close(nl_socket);
    }
};

int linux_rt_manager::get_nl_fd() const
{
    return nl_socket;
};

/**
 * @brief 
 * add to the m_tables the rt_number and empty routing_table as <key,value> 
 * @param rt_number 
 */
void linux_rt_manager::follow_rt(const uint32_t rt_number, std::string vrf_name)
{
    add_name(rt_number, vrf_name);
    followed_rt_list.push_back(rt_number);
    m_tables[rt_number] = linux_routing_table(rt_number, get_name(rt_number));
    return;
}

std::vector<uint32_t> linux_rt_manager::get_follow_list() const
 {
    return followed_rt_list;
};

/**
 * @brief 
 * return true if the manager has been subscribed on a linux routin table 
 * @param rt_number number of linux routing tabel
 * @return true 
 * @return false 
 */
bool linux_rt_manager::rt_number_is_followed(const uint32_t rt_number) const
{
    return m_tables.find(rt_number) != m_tables.end();
};

/**
 * @brief 
 * update routing tabel if rt_number is followed by the manager
 * @param table linux_routing_tabel containes all the routes
 * @returns int;
 * @return 0 - succsess; 
 * @return -1 - rt_number is not followed
 */
int linux_rt_manager::update(const linux_routing_table &table)
{
    if (not rt_number_is_followed(table._rt_number)){
        return -1;
    };
    _was_changed               = true;
    m_tables[table._rt_number] = table;
    return 0;
};

int linux_rt_manager::update(const linux_route &route, const bool need_to_check)
{
    auto pos = m_tables.find(route.rt_number);
    if ( pos == m_tables.end() ) {
        return -1;
    }
    auto _table = &(pos->second);

    if ( _table->update(route, need_to_check) < 0 ) {
        return -1;
    }
    _was_changed = true;
    return 0;
};

/**
  * @brief 
  * Send NL response to update all the routing table 
  * 
  * @param rt_number - number of linux routing table
  * @return int - counts of routes into the routing table
  */
int linux_rt_manager::update(const uint32_t rt_number, const std::string &vrf_name)
{
    
    if (not rt_number_is_followed(rt_number)){
        follow_rt(rt_number,vrf_name);
    };
    char *result     = nullptr;
    size_t dump_size = 0;

    result    = new char[BUF_SIZE];
    dump_size = nl_socket_handler::request_get_route_list(nl_socket, rt_number, result, BUF_SIZE);

    linux_routing_table rt(rt_number, get_name(rt_number));
    auto count = rt.get_routes_from_nl_resp(result, dump_size);
    update(rt); // _was_changed = true ;

    delete[] result;
    return count;
};

/**
 * @brief 
 * Finding a route into saved routing tables
 * @param route 
 * @return std::pair<bool,size_t> bool - was the route found or not; size_t - index if it was
 */
std::pair<bool,size_t> linux_rt_manager::find(const linux_route &route) const
{

    auto pos = m_tables.find(route.rt_number);
    if ( pos == m_tables.end() ) {
        return {false, 0};
    }

    auto _table = &(pos->second);
    return _table->find(route);
};

/**
 * @brief 
 * Chech if at least one table was changed
 * 
 * @return true - was 
 * @return false -wasn't
 */
bool linux_rt_manager::was_changed() const
{
    return _was_changed;
};

/**
 * @brief 
 * Cast linux routing tabel number to an id wich is uses for keeping tabels into FIB
 * @param rt_number linux routing tabel number 
 * @param fib_table_size size/count of FIB tabels (uses if we can't find existed reference)
 * @return size_t 
 */
ssize_t linux_rt_manager::get_FIB_id(const uint32_t rt_number)
{
    auto pos = m_id.find(rt_number);
    if ( pos != m_id.end() ) {
        return pos->second;
    }
    return -1;
};

void linux_rt_manager::add_FIB_id(const uint32_t rt_number, const size_t fib_rt_num)
{
    m_id[rt_number] = fib_rt_num;
    return;
}

/**
 * @brief 
 * Cast linux routing tabel number to a name wich is used linux VRF
 * @param rt_number linux routing tabel number 
 * @param name name of vrf
 * @return size_t 
 */
int linux_rt_manager::add_name(const uint32_t rt_number, const std::string &name)
{
    auto pos = m_name.find(rt_number);
    if ( pos != m_name.end() ) {
        return pos->first;
    }
    m_name[rt_number] = name;
    return rt_number;
}

/**
 * @brief 
 * Get the VRF name by the linux routing tabel number 
 * @param rt_number inux routing tabel number 
 * 
 * @returns std::string 
 * @return "" if there is no matching;
 * @return VRF name
 */
std::string linux_rt_manager::get_name(const uint32_t rt_number) const
{
    auto pos = m_name.find(rt_number);
    if ( pos != m_name.end() ) {
        return pos->second;
    }
    return "";
}

std::vector<linux_route> *linux_rt_manager::get(const uint32_t rt_number)
{
    _was_changed = false;

    auto pos = m_tables.find(rt_number);
    if ( pos == m_tables.end() ) {
        return nullptr;
    }

    auto _table = &(pos->second);
    return _table->get();
};

uint32_t linux_rt_manager::get_routes_from_nl_resp(const uint32_t rt_number, const char *nl_sock_resp_buf, ssize_t msg_size)
{
    _was_changed = true;

    auto pos = m_tables.find(rt_number);
    if ( pos == m_tables.end() ) {
        m_tables[rt_number] = linux_routing_table(rt_number, get_name(rt_number));
    }
    auto _table = &(pos->second);

    return _table->get_routes_from_nl_resp(nl_sock_resp_buf, msg_size);
}

bool linux_rt_manager::catch_route_update_notification(linux_route &route)
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

    for ( ; NLMSG_OK(nlh, msg_size);
          nlh = NLMSG_NEXT(nlh, msg_size) ) {

        route = linux_route::parse_route_from_nl_resp_hdr(nlh);
        if (route.status != linux_route::e_status::EMPTY){
            return true;
        }
    }
    return false;
}
