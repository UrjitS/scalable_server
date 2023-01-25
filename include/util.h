#ifndef SCALABLE_SERVER_UTIL_H
#define SCALABLE_SERVER_UTIL_H

#include <netinet/in.h>

struct options
{
    char * ip_address;
    in_port_t port_out;
};

in_port_t parse_port(const char *buff, int radix);
size_t parse_size_t(const char *buff, int radix);

#endif //SCALABLE_SERVER_UTIL_H
