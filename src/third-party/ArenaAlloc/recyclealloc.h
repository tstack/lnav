// -*- c++ -*-
/******************************************************************************
 **  recyclealloc.h
 **
 **  Arena allocator with some modest recycling of freed resources.  
 **  MIT license
 **
 *****************************************************************************/
#ifndef _RECYCLE_ALLOC_H
#define _RECYCLE_ALLOC_H

#include "arenaalloc.h"
#include <string.h>
#include <inttypes.h>

namespace ArenaAlloc
{
  
  // todo:
  // attempt refactor of boilerplate in _memblockimpl and _recycleallocimpl
  template< typename AllocatorImpl, uint16_t StepSize = 16, uint16_t NumBuckets = 256 >
  struct _recycleallocimpl : public _memblockimplbase<AllocatorImpl, _recycleallocimpl<AllocatorImpl> >
  {     
  private:
    
    static_assert( ( StepSize >= 16 && NumBuckets >= 16 ), "Min step size=16, Min num buckets=16" );
    static_assert( !( StepSize & ( StepSize - 1 ) ), "Step size must be a power of 2" );
    
    struct _freeEntry
    {
      // note: order of declaration matters
      std::size_t m_size;
      _freeEntry * m_next;      
    };
    
    _freeEntry * m_buckets[ NumBuckets ]; // m_buckets[ NumBuckets - 1 ] is the oversize bucket
    
    typedef struct _memblockimplbase< AllocatorImpl, _recycleallocimpl<AllocatorImpl> > base_t;
    friend struct _memblockimplbase< AllocatorImpl, _recycleallocimpl<AllocatorImpl> >;
    
    // to get around some sticky access issues between Alloc<T1> and Alloc<T2> when sharing
    // the implementation.
    template <typename U, typename A, typename M >
    friend class Alloc;
    
    template< typename T >
    static void assign( const Alloc<T,AllocatorImpl, _recycleallocimpl<AllocatorImpl> >& src, 
			  _recycleallocimpl *& dest )
    {
      dest = const_cast< _recycleallocimpl<AllocatorImpl>* >( src.m_impl );
    }
        
    static _recycleallocimpl<AllocatorImpl> * create( std::size_t defaultSize, AllocatorImpl& alloc )
    {
      return new ( 
	alloc.allocate( sizeof( _recycleallocimpl ) ) ) _recycleallocimpl<AllocatorImpl>( defaultSize, 
											  alloc );
    }
   
    static void destroy( _recycleallocimpl<AllocatorImpl> * objToDestroy )
    {      
      AllocatorImpl allocImpl = objToDestroy->m_alloc;
      objToDestroy-> ~_recycleallocimpl<AllocatorImpl>();
      allocImpl.deallocate( objToDestroy );      
    }
    
    _recycleallocimpl( std::size_t defaultSize, AllocatorImpl& allocImpl ):
      _memblockimplbase<AllocatorImpl, _recycleallocimpl<AllocatorImpl> >( defaultSize, allocImpl )
    {
      memset( m_buckets, 0, sizeof( m_buckets ) );

#ifdef ARENA_ALLOC_DEBUG
      fprintf( stdout, "_recycleallocimpl=%p constructed with default size=%ld\n", this, 
	       base_t::m_defaultSize );
#endif
    }
  
    ~_recycleallocimpl( )
    {
#ifdef ARENA_ALLOC_DEBUG
      fprintf( stdout, "~_recycleallocimpl() called on _recycleallocimpl=%p\n", this );
#endif      
      base_t::clear();
    }  

    char * allocate( std::size_t numBytes )
    {      
      
      numBytes = ( (numBytes + sizeof( std::size_t ) + StepSize - 1) / StepSize ) * StepSize;
      
      char * returnValue = allocateInternal( numBytes );
      if( !returnValue )
      {
	char * allocValue = base_t::allocate( numBytes );
	
	if( !allocValue )
	  return 0; //allocation failure

	*((std::size_t*)allocValue ) = numBytes; // that includes the header
	return allocValue + sizeof( std::size_t );
      }
      
      return returnValue;
    }
    
    void deallocate( void * ptr )
    {      
      deallocateInternal( reinterpret_cast<char*>(ptr) );
      base_t::deallocate( ptr ); // this is called b/c it is known this just updates stats
    }

    char * allocateInternal( std::size_t numBytes )
    {      
      // numBytes must already be rounded to a multiple of stepsize and have an
      // extra sizeof( std::size_t ) bytes tacked on for the header
      // pointer returned points sizeof( std::size_t ) bytes into the allocation
      // bucket 0 is always null in this scheme. 
      
      uint16_t bucketNumber = numBytes / StepSize;
      
      if( bucketNumber > NumBuckets - 1 )
	bucketNumber = NumBuckets - 1; // oversize alloc
      
      // search max 3 consecutive buckets for an item large enough.
      // in the oversize bucket and only in the oversize bucket,
      // search upto 3 items into the linked list for an entry
      // large enough for the specified size
      for( uint16_t bkt = bucketNumber, i = 0; i < 3 && bkt < NumBuckets; ++i, ++bkt )
      {
	if( m_buckets[ bkt ] )
	  return allocateFrom( numBytes, m_buckets[ bkt ] );
      }
      
      return 0;
    }

    char * allocateFrom( std::size_t numBytes, _freeEntry *& head )
    {      
      _freeEntry * current = head;
      _freeEntry * prev = 0;
      
      int count = 0;

      while( current && count < 3 )
      {
	if( current->m_size >= numBytes )
	{
	  if( prev == 0 )
	    head = current->m_next;
	  else
	    prev->m_next = current->m_next;
	  
	  return reinterpret_cast<char*>(&current->m_next);
	}
	
	++count;
	prev = current;
	current = current->m_next;
      }
      
      return 0;
    }

    void deallocateInternal( char * ptr )
    {
      _freeEntry * v = reinterpret_cast< _freeEntry* >( ptr - sizeof( std::size_t ) );
      uint16_t bucketNumber = v->m_size / StepSize;
      
      if( bucketNumber > NumBuckets - 1 )
	bucketNumber = NumBuckets - 1;
      
      _freeEntry * next = m_buckets[ bucketNumber ];
      v->m_next = next;
      m_buckets[ bucketNumber ] = v;
    }
    
  };
  
  template< typename T, typename Allocator = _newAllocatorImpl >
  using RecycleAlloc = Alloc< T, Allocator, _recycleallocimpl<Allocator> >;
  
}

#endif
