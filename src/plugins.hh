
#ifndef __plugins_hh
#define __plugins_hh

#include <string>

class extension_point {
public:
    extension_point(std::string name);
    ~extension_point();

    std::string get_name() { return this->ep_name; };
    
private:
    std::string ep_name;
};

#endif
