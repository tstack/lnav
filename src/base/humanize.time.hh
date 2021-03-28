
#ifndef lnav_humanize_time_hh
#define lnav_humanize_time_hh

#include <string>

namespace humanize {
namespace time {

std::string time_ago(time_t last_time, bool convert_local = false);

std::string precise_time_ago(const struct timeval &tv, bool convert_local = false);

}
}

#endif
