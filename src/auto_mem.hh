/**
 * @file auto_mem.hh
 */

#ifndef __auto_mem_hh
#define __auto_mem_hh

#include <assert.h>
#include <unistd.h>
#include <stdlib.h>

#include <exception>

/**
 * Resource management class for memory allocated by a custom allocator.
 *
 * @param T The object type.
 * @param auto_free The function to call to free the managed object.
 */
template<class T, void (*auto_free)(void *) = free>
class auto_mem {

public:
    auto_mem(T *ptr = NULL) : am_ptr(ptr) { };

    auto_mem(auto_mem &am) : am_ptr(am.release()) { };

    ~auto_mem() { this->reset(); };

    operator T *(void) const { return this->am_ptr; };

    auto_mem &operator=(T *ptr) {
	this->reset(ptr);
	return *this;
    };
    
    auto_mem &operator=(auto_mem &am) {
	this->reset(am.release());
	return *this;
    };

    T *release(void) {
	T *retval = this->am_ptr;

	this->am_ptr = NULL;
	return retval;
    };

    T *in(void) { return this->am_ptr; };
    
    T **out(void) {
	this->reset();
	return &this->am_ptr;
    };

    void reset(T *ptr = NULL) {
	if (this->am_ptr != ptr) {
	    auto_free(this->am_ptr);
	    this->am_ptr = ptr;
	}
    };
    
private:
    T *am_ptr;
    
};

#endif
