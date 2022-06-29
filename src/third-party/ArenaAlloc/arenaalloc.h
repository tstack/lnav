// -*- c++ -*-
/******************************************************************************
 *  arenaalloc.h
 *  
 *  Arena allocator based on the example logic provided by Nicolai Josuttis
 *  and available at http://www.josuttis.com/libbook/examples.html.
 *  This enhanced work is provided under the terms of the MIT license.
 *  
 *****************************************************************************/

#ifndef _ARENA_ALLOC_H
#define _ARENA_ALLOC_H

#include <limits>
#include <memory>

#if __cplusplus >= 201103L
#include <type_traits>
#include <utility>
#endif

// Define macro ARENA_ALLOC_DEBUG to enable some tracing of the allocator
#include "arenaallocimpl.h"

namespace ArenaAlloc 
{  
  
  struct _newAllocatorImpl
  {
    // these two functions should be supported by a specialized
    // allocator for shared memory or another source of specialized
    // memory such as device mapped memory.
    void* allocate( size_t numBytes ) { return new char[ numBytes ]; }
    void deallocate( void* ptr ) { delete[]( (char*)ptr ); }
  };
  
  template <class T, 
	    class AllocatorImpl = _newAllocatorImpl, 
	    class MemblockImpl = _memblockimpl<AllocatorImpl> >
  class Alloc {
    
  private:        
    MemblockImpl* m_impl;    
    
  public:
    // type definitions
    typedef T        value_type;
    typedef T*       pointer;
    typedef const T* const_pointer;
    typedef T&       reference;
    typedef const T& const_reference;
    typedef std::size_t    size_type;
    typedef std::ptrdiff_t difference_type;
    
#if __cplusplus >= 201103L
    // when containers are swapped, (i.e. vector.swap)
    // swap the allocators also.  This was not specified in c++98
    // thus users of this code not using c++11 must
    // exercise caution when using the swap algorithm or
    // specialized swap member function.  Specifically,
    // don't swap containers not sharing the same
    // allocator internal implementation in c++98.  This is ok
    // in c++11.
    typedef std::true_type propagate_on_container_swap;

    // container moves should move the allocator also.
    typedef std::true_type propagate_on_container_move_assignment;    
#endif
    
    // rebind allocator to type U
    template <class U>
    struct rebind {
      typedef Alloc<U,AllocatorImpl,MemblockImpl> other;
    };
   
    // return address of values
    pointer address (reference value) const {
      return &value;
    }
    const_pointer address (const_reference value) const {
      return &value;
    }

    Alloc( std::size_t defaultSize = 32768, AllocatorImpl allocImpl = AllocatorImpl() ) throw():
      m_impl( MemblockImpl::create( defaultSize, allocImpl ) )
    {      
    }
    
    Alloc(const Alloc& src)  throw(): 
      m_impl( src.m_impl )
    {
      m_impl->incrementRefCount();
    }
    
    template <class U>
    Alloc (const Alloc<U,AllocatorImpl,MemblockImpl>& src) throw(): 
      m_impl( 0 )
    {
      MemblockImpl::assign( src, m_impl );
      m_impl->incrementRefCount();
    }
    
    ~Alloc() throw() 
    {
      m_impl->decrementRefCount();
    }

    // return maximum number of elements that can be allocated
    size_type max_size () const throw() 
    {
      return std::numeric_limits<std::size_t>::max() / sizeof(T);
    }

    // allocate but don't initialize num elements of type T
    pointer allocate (size_type num, const void* = 0) 
    {
      return reinterpret_cast<pointer>( m_impl->allocate(num*sizeof(T)) );
    }

    // initialize elements of allocated storage p with value value
#if __cplusplus >= 201103L

    // use c++11 style forwarding to construct the object    
    template< typename P, typename... Args>
    void construct( P* obj, Args&&... args )
    {
      ::new((void*) obj ) P( std::forward<Args>( args )... );
    }

    template< typename P >
    void destroy( P* obj ) { obj->~P(); }
    
#else
    void construct (pointer p, const T& value) 
    {
      new((void*)p)T(value);
    }
    void destroy (pointer p) { p->~T(); }
#endif

    // deallocate storage p of deleted elements
    void deallocate (pointer p, size_type num) 
    {
      m_impl->deallocate( p );
    }
    
    bool equals( const MemblockImpl * impl ) const
    {
      return impl == m_impl;
    }
  
    bool operator == ( const Alloc& t2 ) const
    {
      return m_impl == t2.m_impl;
    }
  
    friend MemblockImpl;
    
    template< typename Other >
    bool operator == ( const Alloc< Other, AllocatorImpl, MemblockImpl >& t2 )
    {
      return t2.equals( m_impl );
    }
  
    template< typename Other >
    bool operator != ( const Alloc< Other, AllocatorImpl, MemblockImpl >& t2 )
    {
      return !t2.equals( m_impl );
    }
  
    // These are extension functions not required for an stl allocator
    size_t getNumAllocations() { return m_impl->getNumAllocations(); }
    size_t getNumDeallocations() { return m_impl->getNumDeallocations(); }
    size_t getNumBytesAllocated() { return m_impl->getNumBytesAllocated(); }    
  };
  
  template<typename A>
  template<typename T>
  void _memblockimpl<A>::assign( const Alloc<T,A, _memblockimpl<A> >& src, _memblockimpl<A> *& dest )
  {
    dest = const_cast<_memblockimpl<A>* >(src.m_impl);
  }
  
}

#endif
