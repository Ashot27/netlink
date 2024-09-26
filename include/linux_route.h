#ifndef PROJECT_LINUX_ROUTE
#define PROJECT_LINUX_ROUTE

#include <arpa/inet.h>
#include <stdint.h>
#include <sys/socket.h>
#include <cstring>  //memset
#include <iostream>
#include <map>
#include <vector>
#include <linux/rtnetlink.h> // nlhdr 
#include <sstream>
#include <unistd.h> //close

#include "nl_socket_handler.h"

struct linux_route
{

    enum e_status : uint8_t {
        EMPTY  = 0,
        NEW    = 1,
        DELETE = 2
    };

    linux_route()
    {
        memset(&dest, 0, sizeof(sockaddr_storage));
        memset(&src, 0, sizeof(sockaddr_storage));
        memset(&gw, 0, sizeof(sockaddr_storage));
    }

    ~linux_route() = default;

    sockaddr_storage dest;
    sockaddr_storage src;
    sockaddr_storage gw;

    uint8_t priority  = 0;
    uint8_t metrics   = 0;
    uint8_t proto     = 0;
    uint32_t rt_number = 0;
    uint8_t mask_len  = 0;
    e_status status   = EMPTY;
    uint16_t iface_id = 0;

    static linux_route parse_route_from_nl_resp_hdr (nlmsghdr *nlh);

    friend std::ostream &operator<< (std::ostream &os, linux_route &route)
    {
        os << "Route info :" << std::endl;
        os << "  destination    - " << get_ip_str(route.dest) << std::endl;
        os << "  gateway        - " << get_ip_str(route.gw) << std::endl;
        os << "  table          - " << route.rt_number << std::endl;
        os << "  priority       - " << (uint16_t)route.priority << std::endl;
        os << "  mask_len       - " << (uint16_t)route.mask_len << std::endl;
        os << "  iface_id       - " << (uint16_t)route.iface_id << std::endl;
        os << "  metrics        - " << (uint16_t)route.metrics << std::endl;
        os << "  proto          - " << (uint16_t)route.proto << std::endl;
        os << "  status         - ";
        if ( route.status == linux_route::e_status::NEW ) {
            os << "new" << std::endl;
        }
        if ( route.status == linux_route::e_status::DELETE ) {
            os << "delete" << std::endl;
        }

        return os;
    };

    std::string str () const
    {
        std::stringstream ss_info;
        ss_info << this;

        return ss_info.str();
    };

    bool operator== (const linux_route &route) const
    {
        if ( proto == route.proto                 //
             and mask_len == route.mask_len           //
             and rt_number == route.rt_number         //                                                                                                                              = route.rt_number;
             and iface_id == route.iface_id           //
             and metrics == route.metrics             //
             and priority == route.priority           //
             and sockaddr_is_equal(dest, route.dest)  //
             and sockaddr_is_equal(src, route.src)    //
             and sockaddr_is_equal(gw, route.gw)      //
        ) {
            return true;
        }
        return false;
    }

    bool operator!= (const linux_route &route) const
    {
        return not operator== (route);
    }

    // bool operator<(const linux_route &route) const
    // {
    //     if ( mask_len == route.mask_len ) {
    //         return metrics < route.metrics;
    //     }
    //     return mask_len > route.mask_len;  // reverse
    // }

    static bool sockaddr_is_equal (const sockaddr_storage &ss1, const sockaddr_storage &ss2)
    {
        if ( ss1.ss_family == ss2.ss_family ) {
            if (not ss1.ss_family){
                return true;
            }
            else if ( ss1.ss_family == AF_INET ) {
                if ( ((sockaddr_in *)(&ss1))->sin_addr.s_addr == ((sockaddr_in *)(&ss2))->sin_addr.s_addr ) {
                    return true;
                };
            }
            else if ( ss1.ss_family == AF_INET6 ) {
                if ( ((sockaddr_in6 *)(&ss1))->sin6_addr.__in6_u.__u6_addr32 == ((sockaddr_in6 *)(&ss2))->sin6_addr.__in6_u.__u6_addr32 ) {
                    return true;
                };
            }
        }
        return false;
    }

    static std::string get_ip_str (const sockaddr_storage &ss)
    {
        char str[INET6_ADDRSTRLEN] = {0};
        if ( ss.ss_family == AF_INET ) {
            inet_ntop(AF_INET, &((sockaddr_in *)(&ss))->sin_addr, str, INET_ADDRSTRLEN);
        }
        if ( ss.ss_family == AF_INET6 ) {
            inet_ntop(AF_INET6, &((sockaddr_in6 *)(&ss))->sin6_addr, str, INET6_ADDRSTRLEN);
        }
        return str;
    };
};

class linux_routing_table
{
    std::vector<linux_route> _table = {};

   public:
    const uint32_t _rt_number   = 0;
    const std::string _vrf_name = "";  // vrf name

    linux_routing_table() = default;
    linux_routing_table(const uint32_t rt_number, const std::string &vrf_name = "")
        : _rt_number(rt_number), _vrf_name(vrf_name) {};
    linux_routing_table(const linux_routing_table &rt) = default;

    linux_routing_table &operator= (linux_routing_table rt)
    {
        const_cast<std::string &>(this->_vrf_name) = rt._vrf_name;
        const_cast<uint32_t &>(this->_rt_number)   = rt._rt_number;
        this->_table                               = rt._table;
        return *this;
    }

    int update (const linux_route &route, const bool need_to_check = true);
    std::pair<bool, size_t> find (const linux_route &route) const;  // O(n)
    void change_vrf_name (const std::string &name);
    std::vector<linux_route> *get ();
    uint32_t size () const;
    uint32_t get_routes_from_nl_resp (const char *nl_sock_resp_buf, ssize_t msg_size);
};

class linux_rt_manager
{
    bool _was_changed = false;

    std::vector<uint32_t> followed_rt_list           = {};  // list of rt_numbers used by manager
    std::map<uint32_t, size_t> m_id                  = {};  // routing tabel number to FIB ID
    std::map<uint32_t, std::string> m_name           = {};  // routing tabel number to vrf/table name
    std::map<uint32_t, linux_routing_table> m_tables = {};  // routing tabel number to table

    struct sockaddr_nl nl_addr;
    int nl_socket = 0;

    linux_rt_manager(){
        open_nl_socket();
    };
    ~linux_rt_manager(){
        stop();
    };


   public:
   
    static linux_rt_manager &get_instance ()
    {
        static linux_rt_manager instance;
        return instance;
    }

    void operator= (linux_rt_manager const &)  = delete;  // we won't copy file descripors
    linux_rt_manager(linux_rt_manager const &) = delete;

    int open_nl_socket();
    void stop();
    int get_nl_fd () const;

    int update (const linux_routing_table &_table);
    int update (const linux_route &route, const bool need_to_check = true);
    int update (const uint32_t rt_number,const std::string& vrf_name);


    void follow_rt (const uint32_t rt_number, std::string vrf_name = "");
    std::vector<uint32_t> get_follow_list ()const ;
    bool rt_number_is_followed (const uint32_t rt_number) const;
    std::pair<bool, size_t> find (const linux_route &route) const;  // O(n)
    ssize_t get_FIB_id (const uint32_t rt_number);
    void add_FIB_id (const uint32_t rt_number, const size_t fib_rt_num);
    int add_name (const uint32_t rt_number, const std::string &name);
    std::string get_name(const uint32_t rt_number) const ;
    bool was_changed () const;
    std::vector<linux_route> *get (const uint32_t rt_number);
    uint32_t get_routes_from_nl_resp (const uint32_t rt_number, const char *nl_sock_resp_buf, ssize_t msg_size);
    bool catch_route_update_notification(linux_route &route);

};

#endif  // PROJECT_LINUX_ROUTE