#ifndef PROJECT_NL_SOCK_HANDL_H
#define PROJECT_NL_SOCK_HANDL_H

#include <string.h>
#include <iostream>
#include <vector>
#include <atomic>

#include <asm/types.h>
#include <linux/if_link.h>
#include <linux/rtnetlink.h>
#include <linux/neighbour.h>
#include <sys/socket.h>

#include <arpa/inet.h>
#include <net/if.h>

#define BUF_SIZE 4096

#define WRONG_MSG -1
#define NO_ATTR_IN_MSG -2
#define NO_SYSTEM_ID 0
#define NONE ""

#define ALL -1u
namespace nl_socket_handler
{
    static std::atomic<uint32_t> a_seq_num = 0;

    enum return_code {
        nlmsg_type_err       = -1,
        rtattr_type_err      = -2,
        nlmsg_err            = -3,
        nlmsg_end_of_dump    = -4,
        unix_send_err        = -5,
        err_ip_addr_invalid  = -6,
        err_mask_invalid     = -7,
        err_iface_id_not_set = -8,
    };

    /**
     * @brief 
     * Filling in the attribute with specified type and data. A memory should already be allocated.
     * @tparam T type of data. if it's a nullptr the function fill in only the attribute header and return a pointer to attribute data beggining
     * @param msg_buf pointer to the beginning of the attribute
     * @param type attribute type 
     * @param data 
     * @param length data length (not required if the data is plain)
     * @return char* 
     * A pointer to the first byte after the attribute or to the first byte after the attribute header if the data is nullptr
     */
    template <typename T>
    char *fill_in_attr (char *msg_buf, const uint8_t& type, const T& data, uint64_t length = sizeof(T))
    {
        rtattr *attr   = (rtattr *)msg_buf;
        attr->rta_len  = RTA_SPACE(length);
        attr->rta_type = type;
        char *start_data_ptr = (char *)RTA_DATA(attr);
        if ( std::is_null_pointer<T>::value ) {
            return start_data_ptr;
        }
        memcpy(start_data_ptr, &data, length);
        msg_buf += attr->rta_len;
        return msg_buf;
    }

    inline unsigned int
    get_ifi_flags (char *response_buffer)
    {
        ifinfomsg *ptr = (ifinfomsg *)(response_buffer + sizeof(nlmsghdr));
        return ptr->ifi_flags;
    }

    inline unsigned int
    system_iface_id (char *response_buffer)
    {
        char *ptr        = response_buffer;
        nlmsghdr *header = (nlmsghdr *)ptr;
        switch ( header->nlmsg_type ) {
            case RTM_NEWLINK:
            case RTM_DELLINK:
            case RTM_GETLINK:
                return ((ifinfomsg *)(ptr + sizeof(nlmsghdr)))->ifi_index;
            case RTM_NEWADDR:
            case RTM_DELADDR:
            case RTM_GETADDR:
                return ((ifaddrmsg *)(ptr + sizeof(nlmsghdr)))->ifa_index;
            default:
                return 0;
        }
    }

    inline std::string
    rc_to_string (int rc)
    {
        switch ( rc ) {
            case return_code::nlmsg_err:
                return "NLMSG_ERR recieved. parse payload to get more info";
            case return_code::rtattr_type_err:
                return "message does not contain needed rtattr type";
            case return_code::nlmsg_type_err:
                return "message type recieved is not right";
            case return_code::unix_send_err:
                return "error while send() to unix socket. Check if it is opened";
            case return_code::nlmsg_end_of_dump:
                return "reached end of multipart message";
            case return_code::err_ip_addr_invalid:
                return "unable to convert ip addr";
            default:
                std::cout << rc << std::endl;
                return "no message for current return code";
        }
        return "rc_to_str_failed";
    }

    inline bool
    is_multipart_message (char *response_buf)
    {
        return (((nlmsghdr *)response_buf)->nlmsg_flags & NLM_F_MULTI);
    }

    inline char *
    get_next_multipart_message (char *response_buffer)
    {
        int msg_len = ((nlmsghdr *)response_buffer)->nlmsg_len;
        return (response_buffer + msg_len);
    }

    inline int
    get_nl_sock_data (char *response_buffer, int buf_size, int message_type, int attr_type, char **filled_iface_data)
    {
        char *ptr = response_buffer;
        if ( not(((nlmsghdr *)ptr)->nlmsg_type == message_type) ) {
            if ( ((nlmsghdr *)ptr)->nlmsg_type == NLMSG_ERROR ) {
                *filled_iface_data = ptr + sizeof(nlmsghdr);
                return return_code::nlmsg_err;
            }
            if ( ((nlmsghdr *)ptr)->nlmsg_type == NLMSG_DONE ) {
                *filled_iface_data = nullptr;
                return return_code::nlmsg_end_of_dump;
            }
            return return_code::nlmsg_type_err;
        }
        ptr += sizeof(nlmsghdr) + sizeof(ifinfomsg);
        while ( ptr < (response_buffer + buf_size) ) {
            if ( ((rtattr *)ptr)->rta_type == attr_type ) {
                *filled_iface_data = (char *)RTA_DATA((rtattr *)ptr);
                return (((rtattr *)ptr)->rta_len - 4);
            }
            ptr = ptr + RTA_ALIGN(((rtattr *)ptr)->rta_len);
        }
        return return_code::rtattr_type_err;
    }

    /**
     * @brief 
     * Send request to get info adout linux interfase
     * @param fd netlink socket fd
     * @param interface_name name of LINUX interface.
     *  @return "0" - error during sending request;
    * @return int - sequence number of the request
    */
    inline int
    ask_link_state (const int fd, std::string interface_name)
    {
        uint32_t seq_num = ++a_seq_num;
        size_t msg_size     = sizeof(nlmsghdr) + sizeof(ifinfomsg)  //
                       + RTA_SPACE(interface_name.size());       // IFLA_IFNAME

        char *msg_buf = new char[msg_size];
        char *begin   = msg_buf;
        memset(msg_buf, 0, msg_size);

        nlmsghdr message_header;
        message_header.nlmsg_type = RTM_GETLINK;
        message_header.nlmsg_len  = msg_size;
        message_header.nlmsg_flags = NLM_F_REQUEST;
        message_header.nlmsg_seq   = seq_num;
        memcpy(msg_buf, &message_header, sizeof(nlmsghdr));
        msg_buf += sizeof(nlmsghdr) + sizeof(ifinfomsg);

        msg_buf = fill_in_attr<const char(&)>(msg_buf, IFLA_IFNAME, *interface_name.c_str(), interface_name.size());

        int rc = send(fd, begin, msg_size, 0);
        delete[] begin;
        if ( rc == -1 ) {
            std::cout << "ERORR! netlink socket: send() failed, err: " << strerror(errno) << std::endl;
            return -1;
        }
        return seq_num;
    }

    inline int
    maskstr_to_prefixlen (std::string mask_as_string)
    {
        uint32_t mask_as_int;
        int rc = inet_pton(AF_INET, mask_as_string.c_str(), &mask_as_int);
        if ( rc != 1 ) {
            return return_code::err_ip_addr_invalid;
        }
        mask_as_int        = ntohl(mask_as_int);
        int number_of_ones = 0;
        bool first_zero    = false;
        for ( uint32_t one = (1u << 31); one > 0; one = one >> 1 ) {
            if ( (mask_as_int & one) > 0 ) {
                ++number_of_ones;
                if ( first_zero == true ) {
                    return return_code::err_mask_invalid;
                }
                continue;
            }
            first_zero = true;
        }
        return number_of_ones;
    }

    inline int
    request_updown (const int fd, unsigned int system_iface_id, bool up = true)
    {
        uint32_t seq_num = ++a_seq_num;
        if ( system_iface_id == NO_SYSTEM_ID ) {
            std::cout << "system id is not set" << std::endl;
            return -1;
        }
        size_t msg_size  = sizeof(nlmsghdr) + sizeof(ifinfomsg);
        char *msg_buf = new char[msg_size];
        memset(msg_buf, 0, msg_size * sizeof(char));
        char *begin = msg_buf;

        nlmsghdr *nl_message_header   = (nlmsghdr *)msg_buf;
        nl_message_header->nlmsg_len  = msg_size;
        nl_message_header->nlmsg_type = RTM_NEWLINK;
        nl_message_header->nlmsg_flags |= NLM_F_REQUEST;
        nl_message_header->nlmsg_flags |= NLM_F_ACK;
        nl_message_header->nlmsg_seq = seq_num;
        msg_buf += sizeof(nlmsghdr);

        ifinfomsg *info = (ifinfomsg *)msg_buf;
        memset(msg_buf, 0, sizeof(ifinfomsg));

        info->ifi_index = system_iface_id;
        info->ifi_flags  = (up ? IFF_UP | IFF_RUNNING : 0);
        info->ifi_change = IFF_UP | IFF_RUNNING;
        info->ifi_family = AF_UNSPEC;
        msg_buf += sizeof(ifinfomsg);

        int rc = send(fd, begin, msg_size, 0);
        delete[] begin;
        if ( rc == -1 ) {
            std::cout << "ERORR! send() failed, err: " << strerror(errno) << std::endl;
            return -1;
        }
        return 1;
    }

    /**
    * @brief 
    * recv response
    * @param {fd} netlink socket fd
    * @param {seq_num} sequence number of the request
    * 
    * @returns 
    * @return "0" - success;
    * @return "int" - error code absolute value;
    * @return "-1" - error during recv;
    */
    inline int
    recv_response (const int fd, uint32_t seq_num)
    {
        char nl_sock_resp_buf[BUF_SIZE] = {0};
        int msg_size                    = 0;
        char *iterator                  = nullptr;
        uint32_t _seq;
        // msg_size = recv(fd, nullptr, 0, MSG_PEEK | MSG_TRUNC);
        while ( 1 ) {
            msg_size = recv(fd, nl_sock_resp_buf, BUF_SIZE, 0);
            if ( msg_size < 0 ) {
                return -1;
            }
            _seq     = (((nlmsghdr *)nl_sock_resp_buf)->nlmsg_seq);
            iterator = nl_sock_resp_buf;
            if ( _seq == seq_num ) {
                if ( ((nlmsghdr *)iterator)->nlmsg_type == NLMSG_ERROR ) {
                    int code = *(int *)(iterator + sizeof(nlmsghdr));
                    return -code;  // real code is negative defines are positive
                    // errno-base.h
                }
            }
        }

        return -1;
    }

   /**
    * @brief 
    * 
    * @param fd netlink socket fd
    * @param seq_num sequence number of the request
    * @param result pointer to an allocated memory for the dump
    * @param result_allocated_size size of allocated memory for result
    * @return int - size of dump
    * @return "-1" - error during recv;
    */
    inline int
    recv_dump (const int fd, const uint32_t seq_num, char *&result, size_t result_allocated_size)
    {
        char nl_sock_resp_buf[BUF_SIZE] = {0};
        int msg_size = 0;
        uint32_t _seq    = 0;
        int length    = 0;

        while ( 1 ) {
            // msg_size = recv(fd, nullptr, 0, MSG_PEEK | MSG_TRUNC);
            msg_size = recv(fd, nl_sock_resp_buf, BUF_SIZE, 0);
            if ( msg_size < 0 ) {
                return -1;
            }
            auto hdr = ((nlmsghdr *)nl_sock_resp_buf);
            _seq     = (hdr->nlmsg_seq);
            if ( _seq == seq_num ) {
                if ( ((nlmsghdr *)nl_sock_resp_buf)->nlmsg_type == NLMSG_ERROR ) {
                    length = 0;
                    break;
                }
                if ( ((nlmsghdr *)nl_sock_resp_buf)->nlmsg_type == NLMSG_DONE ) {
                    break;
                }
                if ( (size_t)(length + msg_size) > result_allocated_size ) {
                    result_allocated_size = result_allocated_size << 1;
                    char *concat          = new char[result_allocated_size];
                    memcpy(concat, result, length);
                    delete[] result;
                    result = concat;
                }
                memcpy(result+length, nl_sock_resp_buf, msg_size);
                length += msg_size;
            }
        }

        return length;
    }

    /*
    * Send request to create a VRF into LINUX
    * @param {fd} netlink socket fd
    * @param {name} VRF name used into linux
    * @param {rt_number} linux routing tabel number
    * @param {up} status of the VRF after creation 
    * 
    * @returns 
    * @return "0" - success;
    * @return "int" - error code absolute value;
    * @return "-1" - error during creation;
    */
    inline int
    request_create_vrf (const int fd, const std::string name, const uint32_t rt_number, bool up = true)
    {
        uint32_t seq_num = ++a_seq_num;
        const std::string kind = "vrf";

        size_t data_size      = RTA_SPACE(sizeof(rt_number));    // IFLA_VRF_TABLE
        size_t info_data_size = RTA_SPACE(kind.size())           // IFLA_INFO_KIND
                                + RTA_SPACE(sizeof(data_size));  // IFLA_INFO_DATA
        size_t msg_size = sizeof(nlmsghdr) + sizeof(ifinfomsg)   //
                          + RTA_SPACE(name.size())               // IFLA_IFNAME
                          + RTA_SPACE(info_data_size);           // IFLA_LINKINFO

        char *msg_buf = new char[msg_size];
        memset(msg_buf, 0, msg_size * sizeof(char));
        char *begin = msg_buf;

        nlmsghdr *nl_message_header    = (nlmsghdr *)msg_buf;
        nl_message_header->nlmsg_len   = msg_size;
        nl_message_header->nlmsg_type  = RTM_NEWLINK;
        nl_message_header->nlmsg_flags = NLM_F_REQUEST | NLM_F_ACK | NLM_F_MATCH | NLM_F_ATOMIC;
        nl_message_header->nlmsg_seq   = seq_num;
        msg_buf += sizeof(nlmsghdr);

        ifinfomsg *info = (ifinfomsg *)msg_buf;

        info->ifi_flags = (up ? IFF_UP | IFF_RUNNING : 0);
        info->ifi_flags |= IFF_NOARP | IFF_MASTER;
        info->ifi_change = ALL;
        info->ifi_family = AF_LOCAL;
        info->ifi_type   = 0;
        info->ifi_index  = 0;
        msg_buf += sizeof(ifinfomsg);

        msg_buf = fill_in_attr<const char(&)>(msg_buf, IFLA_IFNAME, *name.c_str(), name.size());
        msg_buf = fill_in_attr<std::nullptr_t>(msg_buf, IFLA_LINKINFO, nullptr, info_data_size);  // filling in only the header without data
        {
            msg_buf = fill_in_attr<const char(&)>(msg_buf, IFLA_INFO_KIND, *kind.c_str(), kind.size());
            msg_buf = fill_in_attr<std::nullptr_t>(msg_buf, IFLA_INFO_DATA, nullptr, data_size); // filling in only the header without data
            {
                msg_buf = fill_in_attr<uint32_t>(msg_buf, IFLA_VRF_TABLE, rt_number);
            }
        }
        int rc = send(fd, begin, msg_size, 0);
        delete[] begin;
        if ( rc == -1 ) {
            std::cout << "ERORR! send() failed, err: " << strerror(errno) << std::endl;
            return -1;
        }
        return recv_response(fd, seq_num);
    }

    /**
     * @brief 
     * Send request to add secondary ip to the linux interface
     * @param fd netlink socket fd
     * @param ip_addr the ip address being added
     * @param masklen the ip address masklen
     * @param system_iface_id linux interface id
     * @return int
     * @return "0" - success;
     * @return ">0" - a responce error code absolute value; 
     * @return "<0" - the request hasn't sent error code; 
     * 
     */
    inline int
    request_add_ip_addr (const int fd, const in_addr_t ip_addr, const uint8_t masklen, const uint32_t system_iface_id)
    {
        uint32_t seq_num = ++a_seq_num;
        if ( system_iface_id == NO_SYSTEM_ID ) {
            return -EINVAL;  //invalid interface
        }
        if ( not masklen || (masklen >= 32) ) {
            return -EINVAL;  //invalid mask
        }
        size_t msg_size = sizeof(nlmsghdr) + sizeof(ifaddrmsg)  //
                       + RTA_SPACE(sizeof(in_addr_t));       // RTA_DST
        char *msg_buf = new char[msg_size];
        memset(msg_buf, 0, msg_size * sizeof(char));
        char *begin = msg_buf;

        nlmsghdr *nl_message_header   = (nlmsghdr *)msg_buf;
        nl_message_header->nlmsg_len  = msg_size;
        nl_message_header->nlmsg_type = RTM_NEWADDR;
        nl_message_header->nlmsg_flags |= NLM_F_REQUEST;
        nl_message_header->nlmsg_flags |= NLM_F_ACK;
        nl_message_header->nlmsg_seq = seq_num;
        msg_buf += sizeof(nlmsghdr);

        ifaddrmsg *new_ip_address     = (ifaddrmsg *)msg_buf;
        new_ip_address->ifa_family    = AF_INET;
        new_ip_address->ifa_prefixlen = masklen;
        new_ip_address->ifa_index     = system_iface_id;
        new_ip_address->ifa_flags |= IFA_F_SECONDARY;
        msg_buf += sizeof(ifaddrmsg);

        msg_buf = fill_in_attr<in_addr_t>(msg_buf, IFA_LOCAL, ip_addr);

        int rc = send(fd, begin, msg_size, 0);
        if ( rc == -1 ) {
            std::cout << "ERORR! netlink socket: send() failed, err: " << strerror(errno) << std::endl;
            return -1;
        }
        delete[] begin;
        return recv_response(fd, seq_num);
    }

    inline int
    request_add_ip_addr (const int fd, const std::string ip_addr, const std::string mask, const uint32_t system_iface_id)
    {

        in_addr_t ip = 0;
        int rc       = inet_pton(AF_INET, ip_addr.c_str(), &ip);
        if ( rc < 1 ) {
            return -EINVAL;
        }

        int masklen = maskstr_to_prefixlen(mask);
        if ( masklen < 0 ) {
            return -EINVAL;
        }

        return request_add_ip_addr(fd, ip, (uint8_t)masklen, system_iface_id);
    }

    /**
     * @brief 
     * Send request to add IPv4 route with parameters
     * @param fd netlink socket fd
     * @param dst_addr route dsetination IP
     * @param gw nexthop/gateway IP
     * @param masklen length of netmask (must be no more than 32)
     * @param metric metric/priority of the route
     * @param oif_id output linux interface id
     * @param rtm_table number of linux routing tabel
     * @param proto number of route protocol
     * 
     * @returns int 
     * @return "0" - success;
     * @return ">0" - a responce error code absolute value; 
     * @return "<0" - the request hasn't sent error code; 
    */
    inline int
    request_add_route (const int fd, const in_addr_t dst_addr = 0, const in_addr_t gw = 0, uint8_t masklen = 32u,  //
                       const uint32_t metric = 0, const uint32_t oif_id = NO_SYSTEM_ID, const uint32_t rtm_table = RT_TABLE_MAIN, const uint8_t proto = RTPROT_STATIC)
    {
        if ( not masklen || (masklen >= 32) ) {
            return -EINVAL;  //invalid mask
        }

        in_addr_t mask = ~(((uint32_t)(-1) >> (masklen)) << masklen);
        in_addr_t dst_addr_sub = dst_addr & mask;  // apply mask to avoid errors
        if (dst_addr_sub != dst_addr)
        {
            return -EINVAL;  // invalid dest/mask combination
        }

        uint32_t seq_num = ++a_seq_num;

        size_t msg_size = sizeof(nlmsghdr) + sizeof(rtmsg)  //
                       + RTA_SPACE(sizeof(in_addr_t))    // RTA_DST
                       + RTA_SPACE(sizeof(in_addr_t))    // RTA_GATEWAY
                       + RTA_SPACE(sizeof(uint32_t))     // RTA_PRIORITY
                       + RTA_SPACE(sizeof(uint32_t))     // RTA_OIF
                       + RTA_SPACE(sizeof(uint32_t))     // RTA_TABLE
            ;
        char *msg_buf = new char[msg_size];
        char *begin   = msg_buf;
        memset(msg_buf, 0, msg_size * sizeof(char));

        nlmsghdr *header   = (nlmsghdr *)msg_buf;
        header->nlmsg_type = RTM_NEWROUTE;
        header->nlmsg_len  = msg_size;
        header->nlmsg_flags |= NLM_F_REQUEST;
        header->nlmsg_flags |= NLM_F_ACK;
        // header->nlmsg_flags |= NLM_F_REPLACE;
        header->nlmsg_flags |= NLM_F_CREATE;
        header->nlmsg_seq = seq_num;
        msg_buf += sizeof(nlmsghdr);

        rtmsg *routing_table_specification        = (rtmsg *)msg_buf;
        routing_table_specification->rtm_family   = AF_INET;
        routing_table_specification->rtm_table    = 0;  // will be set by attr
        routing_table_specification->rtm_protocol = proto;
        routing_table_specification->rtm_scope    = RT_SCOPE_UNIVERSE;
        if ( dst_addr ) {
            routing_table_specification->rtm_scope   = RT_SCOPE_LINK;
            routing_table_specification->rtm_dst_len = masklen;
        }
        routing_table_specification->rtm_type = RTN_UNICAST;
        msg_buf += sizeof(rtmsg);

        msg_buf = fill_in_attr<in_addr_t>(msg_buf, RTA_GATEWAY, gw);
        msg_buf = fill_in_attr<in_addr_t>(msg_buf, RTA_DST, dst_addr);
        msg_buf = fill_in_attr<uint32_t>(msg_buf, RTA_PRIORITY, metric);
        msg_buf = fill_in_attr<uint32_t>(msg_buf, RTA_OIF, oif_id);
        msg_buf = fill_in_attr<uint32_t>(msg_buf, RTA_TABLE, rtm_table);

        auto rc = send(fd, begin, msg_size, 0);
        delete[] begin;
        if ( rc == -1 ) {
            return -EBUSY;
        }
        return recv_response(fd, seq_num);
    }

    /**
     * @brief 
     * Send request to add IPv4 route with parameters
     * @param fd netlink socket fd
     * @param dst_addr route dsetination IP
     * @param gw nexthop/gateway IP
     * @param masklen length of netmask (must be no more than 32)
     * @param metric metric/priority of the route
     * @param oif_id output linux interface id
     * @param rtm_table number of linux routing tabel
     * @param proto number of route protocol
     * 
     * @returns int
     * @return "0" - success;
     * @return ">0" - a responce error code absolute value; 
     * @return "<0" - the request hasn't sent error code; 
    */
    inline int
    request_add_route (const int fd, std::string dst_addr = "0.0.0.0", std::string gw = "0.0.0.0", uint8_t masklen = 32u,   //
                       const uint32_t metric = 0, const uint32_t oif_id = NO_SYSTEM_ID, const uint32_t rtm_table = RT_TABLE_MAIN, const uint8_t proto = RTPROT_STATIC)
    {
        in_addr_t dst_ip = 0;
        in_addr_t gw_ip  = 0;
        int rc           = 0;

        rc = inet_pton(AF_INET, dst_addr.c_str(), &dst_ip);
        if ( rc < 1 ) {
            return -EINVAL;
        }
        rc = inet_pton(AF_INET, gw.c_str(), &gw_ip);
        if ( rc < 1 ) {
            return -EINVAL;
        }

        return nl_socket_handler::request_add_route(fd, dst_ip, gw_ip, masklen, metric, oif_id, rtm_table, proto);
    }

    /**
    * @brief 
    * Send request to find an interface into LINUX
    * @param fd netlink socket fd
    * @param name interface name used into linux
    * 
    * @returns 
    * @return "0" - the interface has't been found;
    * @return "uint32_t" - interface index in LINUX;
    */
    inline uint32_t
    search_iface (const int fd, const std::string name)
    {
        uint32_t seq_num = nl_socket_handler::ask_link_state(fd, name);

        bool is_multipart = true;
        char nl_sock_resp_buf[BUF_SIZE];
        char *interface_name;
        int msg_size = 0;

        char *iterator = nullptr;
        uint32_t _seq;
        // msg_size = recv(fd, nullptr, 0, MSG_PEEK | MSG_TRUNC);
        do {
            msg_size = recv(fd, nl_sock_resp_buf, BUF_SIZE, 0);
            if ( msg_size < 0 ) {
                return EBUSY;
            }
            _seq     = (((nlmsghdr *)nl_sock_resp_buf)->nlmsg_seq);
            iterator = nl_sock_resp_buf;
        } while ( _seq != seq_num );

        uint32_t interface_index = 0;
        while ( is_multipart and (iterator < (nl_sock_resp_buf + msg_size)) ) {
            is_multipart = nl_socket_handler::is_multipart_message(iterator);
            int rc       = nl_socket_handler::get_nl_sock_data(iterator, msg_size, RTM_NEWLINK, IFLA_IFNAME, &interface_name);
            if ( rc < 0 ) {
                break;
            }
            if ( name == std::string(interface_name) ) {
                interface_index = nl_socket_handler::system_iface_id(iterator);
                break;
            }
            iterator = nl_socket_handler::get_next_multipart_message(iterator);
        }

        return interface_index;
    }

    /**
     * @brief 
     * Send get-reqest for the vrf and parse the response to find routing table number
     * @param fd netlink socket fd
     * @param vrf_name linux VRF name
     * 
     * @return uint32_t 
     * @return "0" - success;
     * @return "uint32_t" - error code absolute value;
     */
    inline uint32_t
    get_rt_number_from_vrf_name (const int fd, const std::string vrf_name)
    {
        uint32_t seq_num = nl_socket_handler::ask_link_state(fd, vrf_name);

        bool is_multipart = true;
        char nl_sock_resp_buf[BUF_SIZE];
        char *iterator = nullptr;
        char *interface_name;
        char *data;
        int msg_size = 0;
        uint32_t _seq;
        do {
            msg_size = recv(fd, nl_sock_resp_buf, BUF_SIZE, 0);
            if ( msg_size < 0 ) {
                return EBUSY;
            }
            _seq     = (((nlmsghdr *)nl_sock_resp_buf)->nlmsg_seq);
            iterator = nl_sock_resp_buf;
        } while ( _seq != seq_num );

        uint32_t rt_num = 0;
        while ( is_multipart and (iterator < (nl_sock_resp_buf + msg_size)) ) {
            is_multipart = nl_socket_handler::is_multipart_message(iterator);
            int rc       = nl_socket_handler::get_nl_sock_data(iterator, msg_size, RTM_NEWLINK, IFLA_IFNAME, &interface_name);
            if ( rc < 0 ) {
                break;
            }
            if ( vrf_name == std::string(interface_name) ) {
                int size = nl_socket_handler::get_nl_sock_data(iterator, msg_size, RTM_NEWLINK, IFLA_LINKINFO, &data);
                if ( size < 0 ) {
                    break;
                }
               
                char *ptr = data + size - sizeof(uint32_t); // last 4 bytes of the data is rt_num
                rt_num = *(uint32_t *)(ptr);

                break;
            }
            iterator = nl_socket_handler::get_next_multipart_message(iterator);
        }

        return rt_num;
    }

    /*
    * Send request to delete a VRF into LINUX
    * @param {fd} netlink socket fd
    * @param {index} linux interface number
    * 
    * @returns 
    * @return "0" - success;
    * @return "int" - error code absolute value;
    * @return "-1" - error during creation;
    */
    inline int
    request_del_vrf (const int fd, const int index)
    {
        uint32_t seq_num = ++a_seq_num;

        size_t msg_size  = sizeof(nlmsghdr) + sizeof(ifinfomsg);
        char *msg_buf = new char[msg_size];
        memset(msg_buf, 0, msg_size * sizeof(char));
        char *begin = msg_buf;

        nlmsghdr *nl_message_header    = (nlmsghdr *)msg_buf;
        nl_message_header->nlmsg_len   = msg_size;
        nl_message_header->nlmsg_type  = RTM_DELLINK;
        nl_message_header->nlmsg_flags = NLM_F_REQUEST | NLM_F_ACK;
        nl_message_header->nlmsg_seq   = seq_num;
        msg_buf += sizeof(nlmsghdr);

        ifinfomsg *info = (ifinfomsg *)msg_buf;
        info->ifi_type  = 0;
        info->ifi_index = index;
        msg_buf += sizeof(ifinfomsg);

        int rc = send(fd, begin, msg_size, 0);
        delete[] begin;
        if ( rc == -1 ) {
            std::cout << "ERORR! send() failed, err: " << strerror(errno) << std::endl;
            return -1;
        }
        return recv_response(fd, seq_num);
    }

    /*
    * Send request to add a linux interface to VRF
    * @param {fd} netlink socket fd
    * @param {vrf_index} VRF interface number (use 0 to delete from VRF)
    * @param {iface_index} linux interface number
    * 
    * @returns 
    * @return "0" - success;
    * @return "int" - error code absolute value;
    * @return "-1" - error during request;
    */
    inline int request_add_iface_to_vrf (const int fd, const uint32_t vrf_index, const uint32_t iface_index)
    {
        uint32_t seq_num = ++a_seq_num;
        size_t msg_size     = sizeof(nlmsghdr) + sizeof(ifinfomsg)  //
                       + RTA_SPACE(sizeof(uint32_t));            // IFLA_MASTER
        char *msg_buf           = new char[msg_size];
        memset(msg_buf, 0, msg_size * sizeof(char));
        char *begin = msg_buf;

        nlmsghdr *nl_message_header    = (nlmsghdr *)msg_buf;
        nl_message_header->nlmsg_len   = msg_size;
        nl_message_header->nlmsg_type  = RTM_NEWLINK;
        nl_message_header->nlmsg_flags = NLM_F_REQUEST | NLM_F_ACK;
        nl_message_header->nlmsg_seq   = seq_num;
        msg_buf += sizeof(nlmsghdr);

        ifinfomsg *info = (ifinfomsg *)msg_buf;
        info->ifi_index = iface_index;
        msg_buf += sizeof(ifinfomsg);

        msg_buf = fill_in_attr<uint32_t>(msg_buf, IFLA_MASTER, vrf_index);

        int rc = send(fd, begin, msg_size, 0);
        delete[] begin;
        if ( rc == -1 ) {
            std::cout << "ERORR! send() failed, err: " << strerror(errno) << std::endl;
            return -1;
        }
        return recv_response(fd, seq_num);
    }

    /*
    * Send request to delete a linux interface from a VRF
    * @param {fd} netlink socket fd
    * @param {iface_index} linux interface number
    * 
    * @returns 
    * @return "0" - success;
    * @return "int" - error code absolute value;
    * @return "-1" - error during request;
    */
    inline int request_del_iface_from_vrf (const int fd, const uint32_t iface_index)
    {
        return request_add_iface_to_vrf(fd, 0, iface_index);
    }

    /**
     * @brief 
     * 
     * @param fd netlink socket fd
     * @param rtm_table linux routing table number
     * @param result pointer to an allocated memory for the dump
     * @param result_allocated_size size of allocated memory for result
     * @return size_t - size of dump
     */
    inline size_t
    request_get_route_list (const int fd, const uint32_t rtm_table, char *&result, size_t result_allocated_size)
    {
        static uint32_t seq_num = ++a_seq_num;

        size_t msg_size = sizeof(nlmsghdr) + sizeof(rtmsg)  //
                       + RTA_SPACE(sizeof(uint32_t));    // RTA_TABLE
        char *msg_buf = new char[msg_size];
        memset(msg_buf, 0, msg_size * sizeof(char));
        char *begin = msg_buf;

        nlmsghdr *nl_message_header    = (nlmsghdr *)msg_buf;
        nl_message_header->nlmsg_len   = msg_size;
        nl_message_header->nlmsg_type  = RTM_GETROUTE;
        nl_message_header->nlmsg_flags = NLM_F_REQUEST | NLM_F_DUMP;
        nl_message_header->nlmsg_seq   = seq_num;
        msg_buf += sizeof(nlmsghdr);

        rtmsg *rt_specification      = (rtmsg *)msg_buf;
        rt_specification->rtm_family = AF_INET;
        rt_specification->rtm_table  = 0;  // set into the attribute
        msg_buf += sizeof(rtmsg);

        msg_buf = fill_in_attr<uint32_t>(msg_buf, RTA_TABLE, rtm_table);

        int rc = send(fd, begin, msg_size, 0);
        delete[] begin;
        if ( rc == -1 ) {
            return return_code::unix_send_err;
        }

        auto dump_size = recv_dump(fd, seq_num, result, result_allocated_size);
        return dump_size;
    }

    /**
     * @brief 
     * adds a PROBE entity to the linux arp table
     * @param fd netlink socket fd
     * @param dst_addr ip addr
     * @param lladdr mac addr 
     * @param oif_id linux interface id
     * @returns int 
     * @return "0" - success;
     * @return ">0" - a responce error code absolute value; 
     * @return "<0" - the request hasn't sent error code; 
     */
    inline int
    request_add_neighbor (const int fd, const in_addr_t dst_addr, const char (&lladdr)[6], const uint32_t oif_id)
    {
        uint32_t seq_num = ++a_seq_num;

        size_t msg_size = sizeof(nlmsghdr) + sizeof(ndmsg)    //
                          + RTA_SPACE(sizeof(in_addr_t))      // NDA_DST
                          + RTA_SPACE(sizeof(lladdr))         // NDA_LLADDR
                          + RTA_SPACE(sizeof(uint32_t))       // NDA_PROBES
                          + RTA_SPACE(sizeof(nda_cacheinfo))  // NDA_CACHEINFO
            ;
        char *msg_buf = new char[msg_size];
        char *begin   = msg_buf;
        memset(msg_buf, 0, msg_size * sizeof(char));

        nlmsghdr *header   = (nlmsghdr *)msg_buf;
        header->nlmsg_type = RTM_NEWNEIGH;
        header->nlmsg_len  = msg_size;
        header->nlmsg_flags |= NLM_F_REQUEST;
        header->nlmsg_flags |= NLM_F_ACK;
        header->nlmsg_flags |= NLM_F_CREATE;
        header->nlmsg_seq = seq_num;
        msg_buf += sizeof(nlmsghdr);

        ndmsg *neighbor_specification       = (ndmsg *)msg_buf;
        neighbor_specification->ndm_family  = AF_INET;
        neighbor_specification->ndm_ifindex = oif_id;
        neighbor_specification->ndm_flags   = NTF_SELF;//NTF_USE;
        neighbor_specification->ndm_state   = NUD_PROBE;
        neighbor_specification->ndm_type    = 1;
        msg_buf += sizeof(ndmsg);

        msg_buf = fill_in_attr<in_addr_t>(msg_buf, NDA_DST, dst_addr);
        msg_buf = fill_in_attr<const char(&)>(msg_buf, NDA_LLADDR, *lladdr, sizeof(lladdr));
        msg_buf = fill_in_attr<uint32_t>(msg_buf, NDA_PROBES, 0);
        msg_buf = fill_in_attr<nda_cacheinfo>(msg_buf, NDA_CACHEINFO, nda_cacheinfo {
                                                                          .ndm_confirmed = 0,
                                                                          .ndm_used      = 0,
                                                                          .ndm_updated   = 0,
                                                                          .ndm_refcnt    = 1,
                                                                      });

        auto rc = send(fd, begin, msg_size, 0);
        delete[] begin;
        if ( rc == -1 ) {
            return -EBUSY;
        }
        return recv_response(fd, seq_num);
    }

    /**
     * @brief 
     * updates an entity in the linux arp table if it is stale
     * @param fd netlink socket fd
     * @param dst_addr ip addr
     * @param lladdr mac addr 
     * @param oif_id linux interface id
     * @returns int 
     * @return "0" - success;
     * @return ">0" - a responce error code absolute value; 
     * @return "<0" - the request hasn't sent error code; 
     */
    inline int
    request_update_neighbor (const int fd, const in_addr_t dst_addr, const uint32_t oif_id)
    {
        uint32_t seq_num = ++a_seq_num;

        size_t msg_size = sizeof(nlmsghdr) + sizeof(ndmsg)    //
                          + RTA_SPACE(sizeof(in_addr_t))      // NDA_DST
                          + RTA_SPACE(sizeof(uint32_t))       // NDA_PROBES
                          + RTA_SPACE(sizeof(nda_cacheinfo))  // NDA_CACHEINFO
            ;
        char *msg_buf = new char[msg_size];
        char *begin   = msg_buf;
        memset(msg_buf, 0, msg_size * sizeof(char));

        nlmsghdr *header   = (nlmsghdr *)msg_buf;
        header->nlmsg_type = RTM_NEWNEIGH;
        header->nlmsg_len  = msg_size;
        header->nlmsg_flags |= NLM_F_REQUEST;
        header->nlmsg_flags |= NLM_F_ACK;
        header->nlmsg_flags |= NLM_F_CREATE;
        header->nlmsg_seq = seq_num;
        msg_buf += sizeof(nlmsghdr);

        ndmsg *neighbor_specification       = (ndmsg *)msg_buf;
        neighbor_specification->ndm_family  = AF_INET;
        neighbor_specification->ndm_ifindex = oif_id;
        neighbor_specification->ndm_flags   = NTF_SELF;//NTF_USE;
        neighbor_specification->ndm_state   = NUD_PROBE;
        neighbor_specification->ndm_type    = 1;
        msg_buf += sizeof(ndmsg);

        msg_buf = fill_in_attr<in_addr_t>(msg_buf, NDA_DST, dst_addr);
        // msg_buf = fill_in_attr<const char(&)[6]>(msg_buf, NDA_LLADDR, lladdr);
        msg_buf = fill_in_attr<uint32_t>(msg_buf, NDA_PROBES, 0);
        msg_buf = fill_in_attr<nda_cacheinfo>(msg_buf, NDA_CACHEINFO, nda_cacheinfo {
                                                                          .ndm_confirmed = 0,
                                                                          .ndm_used      = 0,
                                                                          .ndm_updated   = 0,
                                                                          .ndm_refcnt    = 1,
                                                                      });

        auto rc = send(fd, begin, msg_size, 0);
        delete[] begin;
        if ( rc == -1 ) {
            return -EBUSY;
        }
        return recv_response(fd, seq_num);
    }

    /**
     * @brief 
     * adds a PROBE entity to the linux arp table
     * @param fd netlink socket fd
     * @param dst_addr ip addr
     * @param oif_id linux interface id
     * @returns int 
     * @return "0" - success;
     * @return "int" - error code absolute value;
     */
    inline int
    request_search_neighbor (const int fd, const in_addr_t dst_addr, const uint32_t oif_id)
    {
        char lladdr[6] = {
            (char)0xff,
            (char)0xff,
            (char)0xff,
            (char)0xff,
            (char)0xff,
            (char)0xff,
        };
        return request_add_neighbor(fd, dst_addr, lladdr, oif_id);
    }

    /**
     * @brief 
     * delete an entity in the linux arp table 
     * @param fd netlink socket fd
     * @param dst_addr ip addr
     * @returns int 
     * @return "0" - success;
     * @return ">0" - a responce error code absolute value; 
     * @return "<0" - the request hasn't sent error code; 
     */
    inline int
    request_delete_neighbor (const int fd, const in_addr_t dst_addr, const uint32_t oif_id)
    {
        uint32_t seq_num = ++a_seq_num;

        size_t msg_size = sizeof(nlmsghdr) + sizeof(ndmsg)    //
                          + RTA_SPACE(sizeof(in_addr_t))      // NDA_DST
            ;
        char *msg_buf = new char[msg_size];
        char *begin   = msg_buf;
        memset(msg_buf, 0, msg_size * sizeof(char));

        nlmsghdr *header   = (nlmsghdr *)msg_buf;
        header->nlmsg_type = RTM_DELNEIGH;
        header->nlmsg_len  = msg_size;
        header->nlmsg_flags |= NLM_F_REQUEST;
        header->nlmsg_flags |= NLM_F_ACK;
        header->nlmsg_seq = seq_num;
        msg_buf += sizeof(nlmsghdr);

        ndmsg *neighbor_specification       = (ndmsg *)msg_buf;
        neighbor_specification->ndm_family  = AF_INET;
        neighbor_specification->ndm_ifindex = oif_id;
        neighbor_specification->ndm_flags   = NTF_SELF;//NTF_USE;
        neighbor_specification->ndm_state   = NUD_PROBE;
        neighbor_specification->ndm_type    = 1;
        msg_buf += sizeof(ndmsg);

        msg_buf = fill_in_attr<in_addr_t>(msg_buf, NDA_DST, dst_addr);


        auto rc = send(fd, begin, msg_size, 0);
        delete[] begin;
        if ( rc == -1 ) {
            return -EBUSY;
        }
        return recv_response(fd, seq_num);
    }
    //doesn't work yet
    inline int
    request_flush_stale (const int fd, const uint32_t oif_id = 0)
    {
        uint32_t seq_num = ++a_seq_num;

        size_t msg_size  = sizeof(nlmsghdr) + sizeof(rtmsg);
        char *msg_buf = new char[msg_size];
        char *begin   = msg_buf;
        memset(msg_buf, 0, msg_size * sizeof(char));

        nlmsghdr *header   = (nlmsghdr *)msg_buf;
        header->nlmsg_type = RTM_DELNEIGH;
        header->nlmsg_len  = msg_size;
        header->nlmsg_flags |= NLM_F_REQUEST;
        header->nlmsg_flags |= NLM_F_ACK;
        // header->nlmsg_flags |= NLM_F_DUMP;
        header->nlmsg_seq = seq_num;
        msg_buf += sizeof(nlmsghdr);

        ndmsg *neighbor_specification       = (ndmsg *)msg_buf;
        neighbor_specification->ndm_family  = AF_INET;
        neighbor_specification->ndm_ifindex = oif_id;
        neighbor_specification->ndm_flags   = 0;
        neighbor_specification->ndm_state   = NUD_STALE;
        neighbor_specification->ndm_type    = 1;
        msg_buf += sizeof(ndmsg);

      
        auto rc = send(fd, begin, msg_size, 0);
        delete[] begin;
        if ( rc == -1 ) {
            return -EBUSY;
        }
        return recv_response(fd, seq_num);
    }
}  // namespace nl_socket_handler



#endif  //PROJECT_NL_SOCK_HANDL_H