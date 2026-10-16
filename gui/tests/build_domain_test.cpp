#include "build_domain.hpp"

#include <iostream>

int main()
{
    if (stage1ServerDomain("prefix http://nas.localhost/payload suffix") != "localhost") return 1;
    if (stage1ServerDomain("prefix http://nas.example.com/payload suffix") != "example.com") return 2;
    if (stage1ServerDomain("no URL") != "") return 3;
    if (geckoServerDomain("68747470 3A2F2F6E 61732E6C 6F63616C 686F7374 2F773100") != "localhost") return 4;
    if (geckoServerDomain("68747470 3A2F2F6E 61732E65 78616D70 6C652E63 6F6D2F77 31000000") != "example.com") return 5;
    std::cout << "Embedded build domains decoded correctly\n";
    return 0;
}
