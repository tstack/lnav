// -*- c++ -*-
/******************************************************************************
 **  arenaallocimpl.h
 **
 **  Internal implementation types of the arena allocator
 **  MIT license
 *****************************************************************************/

#ifndef _ARENA_ALLOC_IMPL_H
#define _ARENA_ALLOC_IMPL_H

#ifdef ARENA_ALLOC_DEBUG
#    include <stdio.h>
#endif

#include <stdint.h>

namespace ArenaAlloc {

template<typename T, typename A, typename M>
class Alloc;

// internal structure for tracking memory blocks
template<typename AllocImpl>
struct _memblock {
    // allocations are rounded up to a multiple of the size of this
    // struct to maintain proper alignment for any pointer and double
    // values stored in the allocation.
    // A future goal is to support even stricter alignment for example
    // to support cache alignment, special device  dependent mappings,
    // or GPU ops.
    union _roundsize {
        double d;
        void* p;
    };

    _memblock* m_next{nullptr};  // blocks kept link listed for cleanup at end
    std::size_t m_bufferSize;  // size of the buffer
    std::size_t m_index;  // index of next allocatable byte in the block
    char* m_buffer;  // pointer to large block to allocate from

    _memblock(std::size_t bufferSize, AllocImpl& allocImpl)
        : m_bufferSize(roundSize(bufferSize)), m_index(0),
          m_buffer(reinterpret_cast<char*>(allocImpl.allocate(
              bufferSize)))  // this works b/c of order of decl
    {
    }

    std::size_t roundSize(std::size_t numBytes)
    {
        // this is subject to overflow.  calling logic should not permit
        // an attempt to allocate a really massive size.
        // i.e. an attempt to allocate 10s of terabytes should be an error
        return ((numBytes + sizeof(_roundsize) - 1) / sizeof(_roundsize))
            * sizeof(_roundsize);
    }

    char* allocate(std::size_t numBytes)
    {
        std::size_t roundedSize = roundSize(numBytes);
        if (roundedSize + m_index > m_bufferSize)
            return 0;

        char* ptrToReturn = &m_buffer[m_index];
        m_index += roundedSize;
        return ptrToReturn;
    }

    void reset() { this->m_index = 0; }

    void dispose(AllocImpl& impl) { impl.deallocate(m_buffer); }

    ~_memblock() {}
};

template<typename AllocatorImpl, typename Derived>
struct _memblockimplbase {
    AllocatorImpl m_alloc;
    std::size_t m_refCount;  // when refs -> 0 delete this
    std::size_t m_defaultSize;

    std::size_t m_numAllocate;  // number of times allocate called
    std::size_t m_numDeallocate;  // number of time deallocate called
    std::size_t m_numBytesAllocated;  // A good estimate of amount of space used

    _memblock<AllocatorImpl>* m_head;
    _memblock<AllocatorImpl>* m_current;

    // round up 2 next power of 2 if not already
    // a power of 2
    std::size_t roundpow2(std::size_t value)
    {
        // note this works because subtracting 1 is equivalent to
        // inverting the lowest set bit and complementing any
        // bits lower than that.  only a power of 2
        // will yield 0 in the following check
        if (0 == (value & (value - 1)))
            return value;  // already a power of 2

        // fold t over itself. This will set all bits after the highest set bit
        // of t to 1 who said bit twiddling wasn't practical?
        value |= value >> 1;
        value |= value >> 2;
        value |= value >> 4;
        value |= value >> 8;
        value |= value >> 16;
#if SIZE_MAX > UINT32_MAX
        value |= value >> 32;
#endif

        return value + 1;
    }

    _memblockimplbase(std::size_t defaultSize, AllocatorImpl& allocator)
        : m_alloc(allocator), m_refCount(1), m_defaultSize(defaultSize),
          m_numAllocate(0), m_numDeallocate(0), m_numBytesAllocated(0),
          m_head(0), m_current(0)
    {
        if (m_defaultSize < 256) {
            m_defaultSize = 256;  // anything less is academic. a more practical
                                  // size is 4k or more
        } else if (m_defaultSize > 1024UL * 1024 * 1024 * 16) {
            // when this becomes a problem, this package has succeeded beyond my
            // wildest expectations
            m_defaultSize = 1024UL * 1024 * 1024 * 16;
        }

        // for convenience block size should be a power of 2
        // round up to next power of 2
        m_defaultSize = roundpow2(m_defaultSize);
        allocateNewBlock(m_defaultSize);
    }

    char* allocate(std::size_t numBytes)
    {
        char* ptrToReturn = m_current->allocate(numBytes);
        if (!ptrToReturn) {
            allocateNewBlock(numBytes > m_defaultSize / 2
                                 ? roundpow2(numBytes * 2)
                                 : m_defaultSize);

            ptrToReturn = m_current->allocate(numBytes);
        }

#ifdef ARENA_ALLOC_DEBUG
        fprintf(stdout,
                "_memblockimpl=%p allocated %ld bytes at address=%p\n",
                this,
                numBytes,
                ptrToReturn);
#endif

        ++m_numAllocate;
        m_numBytesAllocated += numBytes;  // does not account for the small
                                          // overhead in tracking the allocation

        return ptrToReturn;
    }

    void allocateNewBlock(std::size_t blockSize)
    {
        _memblock<AllocatorImpl>* newBlock
            = new (m_alloc.allocate(sizeof(_memblock<AllocatorImpl>)))
                _memblock<AllocatorImpl>(blockSize, m_alloc);

#ifdef ARENA_ALLOC_DEBUG
        fprintf(stdout,
                "_memblockimplbase=%p allocating a new block of size=%ld\n",
                this,
                blockSize);
#endif

        if (m_head == 0) {
            m_head = m_current = newBlock;
        } else {
            m_current->m_next = newBlock;
            m_current = newBlock;
        }
    }

    void deallocate(void* ptr) { ++m_numDeallocate; }

    size_t getNumAllocations() { return m_numAllocate; }
    size_t getNumDeallocations() { return m_numDeallocate; }
    size_t getNumBytesAllocated() { return m_numBytesAllocated; }

    void clear()
    {
        _memblock<AllocatorImpl>* block = m_head;
        while (block) {
            _memblock<AllocatorImpl>* curr = block;
            block = block->m_next;
            curr->dispose(m_alloc);
            curr->~_memblock<AllocatorImpl>();
            m_alloc.deallocate(curr);
        }
    }

    void reset()
    {
        m_head->reset();
        m_current = m_head;

        this->m_numBytesAllocated = 0;

        _memblock<AllocatorImpl>* block = m_head->m_next;
        m_head->m_next = nullptr;
        while (block) {
            _memblock<AllocatorImpl>* curr = block;
            block = block->m_next;
            curr->dispose(m_alloc);
            curr->~_memblock<AllocatorImpl>();
            m_alloc.deallocate(curr);
        }
    }

    // The ref counting model does not permit the sharing of
    // this object across multiple threads unless an external locking mechanism
    // is applied to ensure the atomicity of the reference count.
    void incrementRefCount()
    {
        ++m_refCount;
#ifdef ARENA_ALLOC_DEBUG
        fprintf(stdout,
                "ref count on _memblockimplbase=%p incremented to %ld\n",
                this,
                m_refCount);
#endif
    }

    void decrementRefCount()
    {
        --m_refCount;
#ifdef ARENA_ALLOC_DEBUG
        fprintf(stdout,
                "ref count on _memblockimplbase=%p decremented to %ld\n",
                this,
                m_refCount);
#endif

        if (m_refCount == 0) {
            Derived::destroy(static_cast<Derived*>(this));
        }
    }
};

// Each allocator points to an instance of _memblockimpl which
// contains the list of _memblock objects and other tracking info
// including a refcount.
// This object is instantiated in space obtained from the allocator
// implementation. The allocator implementation is the component
// on which allocate/deallocate are called to obtain storage from.
template<typename AllocatorImpl>
struct _memblockimpl
    : public _memblockimplbase<AllocatorImpl, _memblockimpl<AllocatorImpl> > {
private:
    typedef struct _memblockimplbase<AllocatorImpl,
                                     _memblockimpl<AllocatorImpl> >
        base_t;
    friend struct _memblockimplbase<AllocatorImpl,
                                    _memblockimpl<AllocatorImpl> >;

    // to get around some sticky access issues between Alloc<T1> and Alloc<T2>
    // when sharing the implementation.
    template<typename U, typename A, typename M>
    friend class Alloc;

    template<typename T>
    static void assign(
        const Alloc<T, AllocatorImpl, _memblockimpl<AllocatorImpl> >& src,
        _memblockimpl*& dest);

    static _memblockimpl<AllocatorImpl>* create(size_t defaultSize,
                                                AllocatorImpl& alloc)
    {
        return new (alloc.allocate(sizeof(_memblockimpl)))
            _memblockimpl<AllocatorImpl>(defaultSize, alloc);
    }

    static void destroy(_memblockimpl<AllocatorImpl>* objToDestroy)
    {
        AllocatorImpl allocImpl = objToDestroy->m_alloc;
        objToDestroy->~_memblockimpl<AllocatorImpl>();
        allocImpl.deallocate(objToDestroy);
    }

    _memblockimpl(std::size_t defaultSize, AllocatorImpl& allocImpl)
        : _memblockimplbase<AllocatorImpl, _memblockimpl<AllocatorImpl> >(
            defaultSize, allocImpl)
    {
#ifdef ARENA_ALLOC_DEBUG
        fprintf(stdout,
                "_memblockimpl=%p constructed with default size=%ld\n",
                this,
                base_t::m_defaultSize);
#endif
    }

    ~_memblockimpl()
    {
#ifdef ARENA_ALLOC_DEBUG
        fprintf(stdout, "~memblockimpl() called on _memblockimpl=%p\n", this);
#endif
        base_t::clear();
    }
};
}  // namespace ArenaAlloc

#endif
