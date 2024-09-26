#include <gtest/gtest.h>

#include "netlink.h"

system_iface *iface = nullptr;
int _iface_index    = -1;

class Netlink_test : public ::testing::Test
{
   public:
    inline static const std::string name     = "test";
    inline static const std::string ip       = "100.100.100.100";
    inline static const std::string mask     = "255.255.255.0";
    inline static const std::string vrf_name = "vrf_test";
    inline static const uint32_t rt_number   = 1111111;

   protected:
    Netlink_test() { }

    ~Netlink_test() { }

    void SetUp () override { }

    void TearDown () override { }

    static void SetUpTestSuite ()
    {
        iface = new system_iface(name, ip, false, false);  // allocated for all the testsuit
        iface->search_iface();

        iface->set_ip_addr(ip, mask);
        // iface->set_iface_state(true);

        system("sysctl -w net.ipv4.neigh.default.gc_stale_time=1");
        system("sysctl -w net.ipv4.neigh.default.gc_thresh1=1");
    }

    static void TearDownTestSuite ()
    {
        _iface_index = nl_socket_handler::search_iface(iface->nl_socket, vrf_name);
        if ( _iface_index ) {
            nl_socket_handler::request_del_vrf(iface->nl_socket, _iface_index);  // if some test failed we need to delete a vrf after the suit
        }

        delete iface;
    }
};

TEST_F(Netlink_test, add_to_vrf)
{
    // GTEST_SKIP() << "Skipping single test";
    int rc = -1;
    rc     = nl_socket_handler::request_create_vrf(iface->nl_socket, vrf_name, rt_number, true);
    EXPECT_EQ(rc, EXIT_SUCCESS);

    uint32_t _rt_number = nl_socket_handler::get_rt_number_from_vrf_name(iface->nl_socket, vrf_name); 
    EXPECT_EQ(_rt_number, rt_number);

    rc = nl_socket_handler::request_create_vrf(iface->nl_socket, vrf_name, rt_number, true);
    EXPECT_EQ(rc, EEXIST);
}

TEST_F(Netlink_test, search_iface)
{
    // GTEST_SKIP() << "Skipping single test";
    _iface_index = nl_socket_handler::search_iface(iface->nl_socket, vrf_name);
    EXPECT_GT(_iface_index, 0);
}

TEST_F(Netlink_test, del_vrf)
{
    // GTEST_SKIP() << "Skipping single test";
    int rc = -1;
    rc     = nl_socket_handler::request_del_vrf(iface->nl_socket, _iface_index);
    EXPECT_EQ(rc, EXIT_SUCCESS);

    rc = nl_socket_handler::request_del_vrf(iface->nl_socket, _iface_index);
    EXPECT_EQ(rc, ENODEV);
}

TEST_F(Netlink_test, search_unexisted_iface)
{
    // GTEST_SKIP() << "Skipping single test";
    _iface_index = nl_socket_handler::search_iface(iface->nl_socket, vrf_name);
    EXPECT_EQ(_iface_index, 0);
}

TEST_F(Netlink_test, add_iface_into_vrf)
{
    // GTEST_SKIP() << "Skipping single test";
    int rc = -1;
    rc     = nl_socket_handler::request_create_vrf(iface->nl_socket, vrf_name, rt_number, true);
    EXPECT_EQ(rc, EXIT_SUCCESS);

    int vrf_index = nl_socket_handler::search_iface(iface->nl_socket, vrf_name);
    EXPECT_GT(vrf_index, 0);

    rc = nl_socket_handler::request_add_iface_to_vrf(iface->nl_socket, vrf_index, iface->linux_interface_id);
    EXPECT_EQ(rc, EXIT_SUCCESS);

    rc = nl_socket_handler::request_del_iface_from_vrf(iface->nl_socket, iface->linux_interface_id);
    EXPECT_EQ(rc, EXIT_SUCCESS);

    rc = nl_socket_handler::request_add_iface_to_vrf(iface->nl_socket, vrf_index, iface->linux_interface_id);
    EXPECT_EQ(rc, EXIT_SUCCESS);

    rc = nl_socket_handler::request_del_vrf(iface->nl_socket, vrf_index);
    EXPECT_EQ(rc, EXIT_SUCCESS);
}

TEST_F(Netlink_test, check_rt_number)
{
    // GTEST_SKIP() << "Skipping single test";
    iface->set_iface_state(false);  // DOWN the interface -> clear the table

    int rc           = 0;
    char *result     = nullptr;
    size_t dump_size = 0;
    result   = new char[BUF_SIZE];
    dump_size = nl_socket_handler::request_get_route_list(iface->nl_socket, rt_number, result, BUF_SIZE);
    delete[] result;
    EXPECT_EQ(dump_size, 0);

    result   = new char[BUF_SIZE];
    dump_size = nl_socket_handler::request_get_route_list(iface->nl_socket, rt_number, result, BUF_SIZE);
    delete[] result;
    EXPECT_EQ(dump_size, 0);

    in_addr_t dst_ip = 0;
    in_addr_t gw     = 0;
    inet_pton(AF_INET, "192.168.1.0", &dst_ip);
    inet_pton(AF_INET, ip.c_str(), &gw);

    rc = nl_socket_handler::request_add_route(iface->nl_socket, dst_ip, gw, 24, 0, iface->linux_interface_id, rt_number);
    // EXPECT_EQ(rc, ENETDOWN);
    EXPECT_EQ(rc, ENETUNREACH);

    iface->set_iface_state(true); // UP the interface
    rc = nl_socket_handler::request_add_route(iface->nl_socket, dst_ip, gw, 24, 0, iface->linux_interface_id, rt_number);
    EXPECT_EQ(rc, EXIT_SUCCESS);

    result    = new char[BUF_SIZE];
    dump_size = nl_socket_handler::request_get_route_list(iface->nl_socket, rt_number, result, BUF_SIZE);
    delete[] result;
    EXPECT_GT(dump_size, 0);

    iface->set_iface_state(false);  // DOWN the interface -> clear the table
    result    = new char[BUF_SIZE];
    dump_size = nl_socket_handler::request_get_route_list(iface->nl_socket, rt_number, result, BUF_SIZE);
    delete[] result;
    EXPECT_EQ(dump_size, 0);
}


TEST_F(Netlink_test, parse_routing_tabel){
    // GTEST_SKIP() << "Skipping single test";

    int rc           = 0;
    char *result     = nullptr;
    size_t dump_size = 0;

    in_addr_t dst_ip = 0;
    in_addr_t gw     = 0;
    inet_pton(AF_INET, "192.168.1.0", &dst_ip);
    inet_pton(AF_INET, ip.c_str(), &gw);

    iface->set_iface_state(true); // UP the interface

    rc = nl_socket_handler::request_add_route(iface->nl_socket, ++dst_ip, gw, 0, 0, iface->linux_interface_id, rt_number, RTPROT_STATIC); // mask == 0
    EXPECT_EQ(rc, -EINVAL);

    rc = nl_socket_handler::request_add_route(iface->nl_socket, ++dst_ip, gw, 33, 0, iface->linux_interface_id, rt_number, RTPROT_STATIC); // mask == 33
    EXPECT_EQ(rc, -EINVAL);

    for (uint8_t i = 1; i <= 254; i++)
    {
        rc = nl_socket_handler::request_add_route(iface->nl_socket, ++dst_ip, gw, 24, 0, iface->linux_interface_id, rt_number, i);
        EXPECT_EQ(rc, EXIT_SUCCESS);
    }

    linux_routing_table rt(rt_number,vrf_name);
    result    = new char[BUF_SIZE];
    dump_size = nl_socket_handler::request_get_route_list(iface->nl_socket, rt_number, result, BUF_SIZE);

    auto count = rt.get_routes_from_nl_resp(result, dump_size);
    EXPECT_EQ(count, 254);
    EXPECT_EQ(rt.size(), 254);


    delete[] result;
    iface->set_iface_state(false);  // DOWN the interface -> clear the table

}

TEST_F(Netlink_test, test){
    GTEST_SKIP() << "Skipping single test";

    system_iface iface0 =  system_iface(name+"0", ip, false, false); 
    system_iface iface1 =  system_iface(name+"1", ip, false, false);  
}

TEST_F(Netlink_test, manager){
    GTEST_SKIP() << "Skipping single test";

    int rc           = 0;
    char *result     = nullptr;
    size_t dump_size = 0;

    in_addr_t dst_ip = 0;
    in_addr_t gw     = 0;
    inet_pton(AF_INET, "192.168.1.0", &dst_ip);
    inet_pton(AF_INET, ip.c_str(), &gw);

    iface->set_iface_state(true); // UP the interface

    auto rt_number2 = rt_number + 1;
    size_t SSIZE = 254;
    for (uint8_t i = 1; i <= SSIZE; i++)
    {
        ++dst_ip;
        rc = nl_socket_handler::request_add_route(iface->nl_socket, dst_ip, gw, 24, 0, iface->linux_interface_id, rt_number, i);
        rc = nl_socket_handler::request_add_route(iface->nl_socket, dst_ip, gw, 24, 0, iface->linux_interface_id, rt_number2, i);

        EXPECT_EQ(rc, EXIT_SUCCESS);
    }

    result    = new char[BUF_SIZE];
    dump_size = nl_socket_handler::request_get_route_list(iface->nl_socket, rt_number, result, BUF_SIZE);

    linux_routing_table rt(rt_number,vrf_name);
    auto count = rt.get_routes_from_nl_resp(result, dump_size);
    EXPECT_EQ(count, SSIZE);
    EXPECT_EQ(rt.size(), SSIZE);

    linux_routing_table rt2(rt_number2);
    dump_size = nl_socket_handler::request_get_route_list(iface->nl_socket, rt_number2, result, BUF_SIZE);
    auto count2 = rt2.get_routes_from_nl_resp(result, dump_size);
    EXPECT_EQ(count2, SSIZE);
    EXPECT_EQ(rt2.size(), SSIZE);

    linux_rt_manager* rt_manager = &(linux_rt_manager::get_instance());
    int code = rt_manager->update(rt);
    EXPECT_EQ(code, -1);
    EXPECT_EQ (rt_manager->get(rt_number), nullptr);

    rt_manager->follow_rt(rt._rt_number);
    rt_manager->update(rt);
    EXPECT_EQ(rt_manager->get(rt_number)->size(), SSIZE);  //update the same
    rt_manager->follow_rt(rt2._rt_number);
    rt_manager->update(rt2);
    EXPECT_EQ (rt_manager->get(rt_number2)->size(), SSIZE);

    auto rt_unexist = rt_number2 +1 ;
    auto unesixt = rt_manager->get(rt_unexist);
    EXPECT_EQ (unesixt, nullptr);


    delete[] result;
    iface->set_iface_state(false);  // DOWN the interface -> clear the table
}

TEST_F(Netlink_test, fill_in_arp)
{
    // GTEST_SKIP() << "Skipping single test";

    uint32_t rc = 0;
    in_addr_t dst_ip = 0;
    uint32_t oif_id  = 2;

    inet_pton(AF_INET, "172.30.246.245", &dst_ip);
    rc = nl_socket_handler::request_search_neighbor(iface->nl_socket, dst_ip, oif_id);
    EXPECT_EQ(rc, 0);

    inet_pton(AF_INET, "172.30.246.246", &dst_ip);
    rc = nl_socket_handler::request_search_neighbor(iface->nl_socket, dst_ip, oif_id);

    EXPECT_EQ(rc, 0);

    rc = nl_socket_handler::request_delete_neighbor(iface->nl_socket, dst_ip, oif_id);
    EXPECT_EQ(rc, 0);

    inet_pton(AF_INET, "172.30.246.247", &dst_ip);
    rc = nl_socket_handler::request_update_neighbor(iface->nl_socket, dst_ip, oif_id); // uptade unexisted entity
    EXPECT_EQ(rc, EINVAL);

    rc = nl_socket_handler::request_search_neighbor(iface->nl_socket, dst_ip, oif_id);
    EXPECT_EQ(rc, 0);
    for (size_t i = 0; i < 2; i++)
    {
        sleep(1);
        rc = nl_socket_handler::request_update_neighbor(iface->nl_socket, dst_ip, oif_id);
        EXPECT_EQ(rc, 0);
    }
}

TEST_F(Netlink_test, flash)
{
    // GTEST_SKIP() << "Skipping single test";

    uint32_t rc = 0;

    rc = nl_socket_handler::request_flush_stale(iface->nl_socket, 0);
    EXPECT_EQ(rc, 0);
}