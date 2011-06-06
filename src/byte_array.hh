
#ifndef __byte_array_hh
#define __byte_array_hh

#include <string.h>
#include <sys/types.h>

template<size_t BYTE_COUNT>
struct byte_array {
    byte_array() { };
    
    byte_array(const byte_array &other) {
	memcpy(this->ba_data, other.ba_data, BYTE_COUNT);
    };
    
    bool operator<(const byte_array &other) const {
	return memcmp(this->ba_data, other.ba_data, BYTE_COUNT) < 0;
    };
    
    unsigned char ba_data[BYTE_COUNT];
};

#endif
