/*******************************************************************************
 * tlx/container/btree.hpp
 *
 * Part of tlx - http://panthema.net/tlx
 *
 * Copyright (C) 2008-2017 Timo Bingmann <tb@panthema.net>
 *
 * All rights reserved. Published under the Boost Software License, Version 1.0
 ******************************************************************************/

#ifndef TLX_CONTAINER_BTREE_HEADER
#define TLX_CONTAINER_BTREE_HEADER

#include <tlx/die/core.hpp>

// *** Required Headers from the STL

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <functional>
#include <iterator>
#include <memory>
#include <utility>

namespace tlx {

//! \addtogroup tlx_container
//! \{
//! \defgroup tlx_container_btree B+ Trees
//! B+ tree variants
//! \{

// *** Debugging Macros

#ifdef TLX_BTREE_DEBUG

#include <iostream>

//! Print out debug information to std::cout if TLX_BTREE_DEBUG is defined.
#define TLX_BTREE_PRINT(x)                                                     \
    do                                                                         \
    {                                                                          \
        if (debug)                                                             \
            (std::cout << x << std::endl);                                     \
    } while (0)

//! Assertion only if TLX_BTREE_DEBUG is defined. This is not used in verify().
#define TLX_BTREE_ASSERT(x)                                                    \
    do                                                                         \
    {                                                                          \
        assert(x);                                                             \
    } while (0)

#else

//! Print out debug information to std::cout if TLX_BTREE_DEBUG is defined.
#define TLX_BTREE_PRINT(x)                                                     \
    do                                                                         \
    {                                                                          \
    } while (0)

//! Assertion only if TLX_BTREE_DEBUG is defined. This is not used in verify().
#define TLX_BTREE_ASSERT(x)                                                    \
    do                                                                         \
    {                                                                          \
    } while (0)

#endif

//! The maximum of a and b. Used in some compile-time formulas.
#define TLX_BTREE_MAX(a, b) ((a) < (b) ? (b) : (a))

#ifndef TLX_BTREE_FRIENDS
//! The macro TLX_BTREE_FRIENDS can be used by outside class to access the B+
//! tree internals. This was added for wxBTreeDemo to be able to draw the
//! tree.
#define TLX_BTREE_FRIENDS friend class btree_friend
#endif

/*!
 * Generates default traits for a B+ tree used as a set or map. It estimates
 * leaf and inner node sizes by assuming a cache line multiple of 256 bytes.
 */
template <typename Key, typename Value>
struct btree_default_traits
{
    //! If true, the tree will self verify its invariants after each insert() or
    //! erase(). The header must have been compiled with TLX_BTREE_DEBUG
    //! defined.
    static const bool self_verify = false;

    //! If true, the tree will print out debug information and a tree dump
    //! during insert() or erase() operation. The header must have been
    //! compiled with TLX_BTREE_DEBUG defined and key_type must be std::ostream
    //! printable.
    static const bool debug = false;

    //! Number of slots in each leaf of the tree. Estimated so that each node
    //! has a size of about 256 bytes.
    static const int leaf_slots = TLX_BTREE_MAX(8, 256 / (sizeof(Value)));

    //! Number of slots in each inner node of the tree. Estimated so that each
    //! node has a size of about 256 bytes.
    static const int inner_slots =
        TLX_BTREE_MAX(8, 256 / (sizeof(Key) + sizeof(void*)));

    //! As of stx-btree-0.9, the code does linear search in find_lower() and
    //! find_upper() instead of binary_search, unless the node size is larger
    //! than this threshold. See notes at
    //! http://panthema.net/2013/0504-STX-B+Tree-Binary-vs-Linear-Search
    static const size_t binsearch_threshold = 256;
};

/*!
 * Basic class implementing a B+ tree data structure in memory.
 *
 * The base implementation of an in-memory B+ tree. It is based on the
 * implementation in Cormen's Introduction into Algorithms, Jan Jannink's paper
 * and other algorithm resources. Almost all STL-required function calls are
 * implemented. The asymptotic time requirements of the STL are not always
 * fulfilled in theory, however, in practice this B+ tree performs better than a
 * red-black tree and almost always uses less memory. The insertion function
 * splits the nodes on the recursion unroll. Erase is largely based on Jannink's
 * ideas.
 *
 * This class is specialized into btree_set, btree_multiset, btree_map and
 * btree_multimap using default template parameters and facade functions.
 */
template <typename Key, typename Value, typename KeyOfValue,
          typename Compare = std::less<Key>,
          typename Traits = btree_default_traits<Key, Value>,
          bool Duplicates = false, typename Allocator = std::allocator<Value> >
class BTree
{
public:
    //! \name Template Parameter Types
    //! \{

    //! First template parameter: The key type of the B+ tree. This is stored in
    //! inner nodes.
    typedef Key key_type;

    //! Second template parameter: Composition pair of key and data types, or
    //! just the key for set containers. This data type is stored in the leaves.
    typedef Value value_type;

    //! Third template: key extractor class to pull key_type from value_type.
    typedef KeyOfValue key_of_value;

    //! Fourth template parameter: key_type comparison function object
    typedef Compare key_compare;

    //! Fifth template parameter: Traits object used to define more parameters
    //! of the B+ tree
    typedef Traits traits;

    //! Sixth template parameter: Allow duplicate keys in the B+ tree. Used to
    //! implement multiset and multimap.
    static const bool allow_duplicates = Duplicates;

    //! Seventh template parameter: STL allocator for tree nodes
    typedef Allocator allocator_type;

    //! \}

    // The macro TLX_BTREE_FRIENDS can be used by outside class to access the B+
    // tree internals. This was added for wxBTreeDemo to be able to draw the
    // tree.
    TLX_BTREE_FRIENDS;

public:
    //! \name Constructed Types
    //! \{

    //! Typedef of our own type
    typedef BTree<key_type, value_type, key_of_value, key_compare, traits,
                  allow_duplicates, allocator_type>
        Self;

    //! Size type used to count keys
    typedef size_t size_type;

    //! \}

public:
    //! \name Static Constant Options and Values of the B+ Tree
    //! \{

    //! Base B+ tree parameter: The number of key/data slots in each leaf
    static const unsigned short leaf_slotmax = traits::leaf_slots;

    //! Base B+ tree parameter: The number of key slots in each inner node,
    //! this can differ from slots in each leaf.
    static const unsigned short inner_slotmax = traits::inner_slots;

    //! Computed B+ tree parameter: The minimum number of key/data slots used
    //! in a leaf. If fewer slots are used, the leaf will be merged or slots
    //! shifted from it's siblings.
    static const unsigned short leaf_slotmin = (leaf_slotmax / 2);

    //! Computed B+ tree parameter: The minimum number of key slots used
    //! in an inner node. If fewer slots are used, the inner node will be
    //! merged or slots shifted from it's siblings.
    static const unsigned short inner_slotmin = (inner_slotmax / 2);

    //! Debug parameter: Enables expensive and thorough checking of the B+ tree
    //! invariants after each insert/erase operation.
    static const bool self_verify = traits::self_verify;

    //! Debug parameter: Prints out lots of debug information about how the
    //! algorithms change the tree. Requires the header file to be compiled
    //! with TLX_BTREE_DEBUG and the key type must be std::ostream printable.
    static const bool debug = traits::debug;

    //! \}

private:
    //! \name Node Classes for In-Memory Nodes
    //! \{

    //! The header structure of each node in-memory. This structure is extended
    //! by InnerNode or LeafNode.
    struct node
    {
        //! Level in the b-tree, if level == 0 -> leaf node
        unsigned short level;

        //! Number of key slotuse use, so the number of valid children or data
        //! pointers
        unsigned short slotuse;

        //! Delayed initialisation of constructed node.
        void initialize(const unsigned short l)
        {
            level = l;
            slotuse = 0;
        }

        //! True if this is a leaf node.
        bool is_leafnode() const
        {
            return (level == 0);
        }
    };

    //! Extended structure of a inner node in-memory. Contains only keys and no
    //! data items.
    struct InnerNode : public node
    {
        //! Define an related allocator for the InnerNode structs.
        typedef typename std::allocator_traits<
            Allocator>::template rebind_alloc<InnerNode>
            alloc_type;

        //! Keys of children or data pointers
        key_type slotkey[inner_slotmax];

        //! Pointers to children
        node* childid[inner_slotmax + 1];

        //! Set variables to initial values.
        void initialize(const unsigned short l)
        {
            node::initialize(l);
        }

        //! Return key in slot s
        const key_type& key(size_t s) const
        {
            return slotkey[s];
        }

        //! True if the node's slots are full.
        bool is_full() const
        {
            return (node::slotuse == inner_slotmax);
        }

        //! True if few used entries, less than half full.
        bool is_few() const
        {
            return (node::slotuse <= inner_slotmin);
        }

        //! True if node has too few entries.
        bool is_underflow() const
        {
            return (node::slotuse < inner_slotmin);
        }
    };

    //! Extended structure of a leaf node in memory. Contains pairs of keys and
    //! data items. Key and data slots are kept together in value_type.
    struct LeafNode : public node
    {
        //! Define an related allocator for the LeafNode structs.
        typedef typename std::allocator_traits<
            Allocator>::template rebind_alloc<LeafNode>
            alloc_type;

        //! Double linked list pointers to traverse the leaves
        LeafNode* prev_leaf;

        //! Double linked list pointers to traverse the leaves
        LeafNode* next_leaf;

        //! Array of (key, data) pairs
        value_type slotdata[leaf_slotmax];

        //! Set variables to initial values
        void initialize()
        {
            node::initialize(0);
            prev_leaf = next_leaf = nullptr;
        }

        //! Return key in slot s.
        const key_type& key(size_t s) const
        {
            return key_of_value::get(slotdata[s]);
        }

        //! True if the node's slots are full.
        bool is_full() const
        {
            return (node::slotuse == leaf_slotmax);
        }

        //! True if few used entries, less than half full.
        bool is_few() const
        {
            return (node::slotuse <= leaf_slotmin);
        }

        //! True if node has too few entries.
        bool is_underflow() const
        {
            return (node::slotuse < leaf_slotmin);
        }

        //! Set the (key,data) pair in slot. Overloaded function used by
        //! bulk_load().
        void set_slot(unsigned short slot, const value_type& value)
        {
            TLX_BTREE_ASSERT(slot < node::slotuse);
            slotdata[slot] = value;
        }
    };

    //! \}

public:
    //! \name Iterators and Reverse Iterators
    //! \{

    class iterator;
    class const_iterator;
    class reverse_iterator;
    class const_reverse_iterator;

    //! STL-like iterator object for B+ tree items. The iterator points to a
    //! specific slot number in a leaf.
    class iterator
    {
    public:
        // *** Types

        //! The key type of the btree. Returned by key().
        typedef typename BTree::key_type key_type;

        //! The value type of the btree. Returned by operator*().
        typedef typename BTree::value_type value_type;

        //! Reference to the value_type. STL required.
        typedef value_type& reference;

        //! Pointer to the value_type. STL required.
        typedef value_type* pointer;

        //! STL-magic iterator category
        typedef std::bidirectional_iterator_tag iterator_category;

        //! STL-magic
        typedef ptrdiff_t difference_type;

        //! Our own type
        typedef iterator self;

    private:
        // *** Members

        //! The currently referenced leaf node of the tree
        typename BTree::LeafNode* curr_leaf;

        //! Current key/data slot referenced
        unsigned short curr_slot;

        //! Friendly to the const_iterator, so it may access the two data items
        //! directly.
        friend class const_iterator;

        //! Also friendly to the reverse_iterator, so it may access the two
        //! data items directly.
        friend class reverse_iterator;

        //! Also friendly to the const_reverse_iterator, so it may access the
        //! two data items directly.
        friend class const_reverse_iterator;

        //! Also friendly to the base btree class, because erase_iter() needs
        //! to read the curr_leaf and curr_slot values directly.
        friend class BTree<key_type, value_type, key_of_value, key_compare,
                           traits, allow_duplicates, allocator_type>;

        // The macro TLX_BTREE_FRIENDS can be used by outside class to access
        // the B+ tree internals. This was added for wxBTreeDemo to be able to
        // draw the tree.
        TLX_BTREE_FRIENDS;

    public:
        // *** Methods

        //! Default-Constructor of a mutable iterator
        iterator() : curr_leaf(nullptr), curr_slot(0)
        {
        }

        //! Initializing-Constructor of a mutable iterator
        iterator(typename BTree::LeafNode* l, unsigned short s)
            : curr_leaf(l), curr_slot(s)
        {
        }

        //! Copy-constructor from a reverse iterator
        iterator(const reverse_iterator& it)
            : curr_leaf(it.curr_leaf), curr_slot(it.curr_slot)
        {
        }

        //! Dereference the iterator.
        reference operator*() const
        {
            return curr_leaf->slotdata[curr_slot];
        }

        //! Dereference the iterator.
        pointer operator->() const
        {
            return &curr_leaf->slotdata[curr_slot];
        }

        //! Key of the current slot.
        const key_type& key() const
        {
            return curr_leaf->key(curr_slot);
        }

        difference_type operator-(iterator rhs) const
        {
            difference_type retval = 0;

            while (rhs.curr_leaf != this->curr_leaf) {
                retval += rhs.curr_leaf->slotuse - rhs.curr_slot;
                rhs.curr_leaf = rhs.curr_leaf->next_leaf;
                rhs.curr_slot = 0;
            }

            retval += this->curr_slot - rhs.curr_slot;

            return retval;
        }

        //! Prefix++ advance the iterator to the next slot.
        iterator& operator++()
        {
            if (curr_slot + 1U < curr_leaf->slotuse)
            {
                ++curr_slot;
            }
            else if (curr_leaf->next_leaf != nullptr)
            {
                curr_leaf = curr_leaf->next_leaf;
                curr_slot = 0;
            }
            else
            {
                // this is end()
                curr_slot = curr_leaf->slotuse;
            }

            return *this;
        }

        //! Postfix++ advance the iterator to the next slot.
        iterator operator++(int)
        {
            iterator tmp = *this; // copy ourselves

            if (curr_slot + 1U < curr_leaf->slotuse)
            {
                ++curr_slot;
            }
            else if (curr_leaf->next_leaf != nullptr)
            {
                curr_leaf = curr_leaf->next_leaf;
                curr_slot = 0;
            }
            else
            {
                // this is end()
                curr_slot = curr_leaf->slotuse;
            }

            return tmp;
        }

        //! Prefix-- backstep the iterator to the last slot.
        iterator& operator--()
        {
            if (curr_slot > 0)
            {
                --curr_slot;
            }
            else if (curr_leaf->prev_leaf != nullptr)
            {
                curr_leaf = curr_leaf->prev_leaf;
                curr_slot = curr_leaf->slotuse - 1;
            }
            else
            {
                // this is begin()
                curr_slot = 0;
            }

            return *this;
        }

        //! Postfix-- backstep the iterator to the last slot.
        iterator operator--(int)
        {
            iterator tmp = *this; // copy ourselves

            if (curr_slot > 0)
            {
                --curr_slot;
            }
            else if (curr_leaf->prev_leaf != nullptr)
            {
                curr_leaf = curr_leaf->prev_leaf;
                curr_slot = curr_leaf->slotuse - 1;
            }
            else
            {
                // this is begin()
                curr_slot = 0;
            }

            return tmp;
        }

        //! Equality of iterators.
        bool operator==(const iterator& x) const
        {
            return (x.curr_leaf == curr_leaf) && (x.curr_slot == curr_slot);
        }

        //! Inequality of iterators.
        bool operator!=(const iterator& x) const
        {
            return (x.curr_leaf != curr_leaf) || (x.curr_slot != curr_slot);
        }
    };

    //! STL-like read-only iterator object for B+ tree items. The iterator
    //! points to a specific slot number in a leaf.
    class const_iterator
    {
    public:
        // *** Types

        //! The key type of the btree. Returned by key().
        typedef typename BTree::key_type key_type;

        //! The value type of the btree. Returned by operator*().
        typedef typename BTree::value_type value_type;

        //! Reference to the value_type. STL required.
        typedef const value_type& reference;

        //! Pointer to the value_type. STL required.
        typedef const value_type* pointer;

        //! STL-magic iterator category
        typedef std::bidirectional_iterator_tag iterator_category;

        //! STL-magic
        typedef ptrdiff_t difference_type;

        //! Our own type
        typedef const_iterator self;

    private:
        // *** Members

        //! The currently referenced leaf node of the tree
        const typename BTree::LeafNode* curr_leaf;

        //! Current key/data slot referenced
        unsigned short curr_slot;

        //! Friendly to the reverse_const_iterator, so it may access the two
        //! data items directly
        friend class const_reverse_iterator;

        // The macro TLX_BTREE_FRIENDS can be used by outside class to access
        // the B+ tree internals. This was added for wxBTreeDemo to be able to
        // draw the tree.
        TLX_BTREE_FRIENDS;

    public:
        // *** Methods

        //! Default-Constructor of a const iterator
        const_iterator() : curr_leaf(nullptr), curr_slot(0)
        {
        }

        //! Initializing-Constructor of a const iterator
        const_iterator(const typename BTree::LeafNode* l, unsigned short s)
            : curr_leaf(l), curr_slot(s)
        {
        }

        //! Copy-constructor from a mutable iterator
        const_iterator(const iterator& it)
            : curr_leaf(it.curr_leaf), curr_slot(it.curr_slot)
        {
        }

        //! Copy-constructor from a mutable reverse iterator
        const_iterator(const reverse_iterator& it)
            : curr_leaf(it.curr_leaf), curr_slot(it.curr_slot)
        {
        }

        //! Copy-constructor from a const reverse iterator
        const_iterator(const const_reverse_iterator& it)
            : curr_leaf(it.curr_leaf), curr_slot(it.curr_slot)
        {
        }

        //! Dereference the iterator.
        reference operator*() const
        {
            return curr_leaf->slotdata[curr_slot];
        }

        //! Dereference the iterator.
        pointer operator->() const
        {
            return &curr_leaf->slotdata[curr_slot];
        }

        //! Key of the current slot.
        const key_type& key() const
        {
            return curr_leaf->key(curr_slot);
        }

        difference_type operator-(const_iterator rhs) const
        {
            difference_type retval = 0;

            while (rhs.curr_leaf != this->curr_leaf) {
                retval += rhs.curr_leaf->slotuse - rhs.curr_slot;
                rhs.curr_leaf = rhs.curr_leaf->next_leaf;
                rhs.curr_slot = 0;
            }

            retval += this->curr_slot - rhs.curr_slot;

            return retval;
        }

        //! Prefix++ advance the iterator to the next slot.
        const_iterator& operator++()
        {
            if (curr_slot + 1U < curr_leaf->slotuse)
            {
                ++curr_slot;
            }
            else if (curr_leaf->next_leaf != nullptr)
            {
                curr_leaf = curr_leaf->next_leaf;
                curr_slot = 0;
            }
            else
            {
                // this is end()
                curr_slot = curr_leaf->slotuse;
            }

            return *this;
        }

        //! Postfix++ advance the iterator to the next slot.
        const_iterator operator++(int)
        {
            const_iterator tmp = *this; // copy ourselves

            if (curr_slot + 1U < curr_leaf->slotuse)
            {
                ++curr_slot;
            }
            else if (curr_leaf->next_leaf != nullptr)
            {
                curr_leaf = curr_leaf->next_leaf;
                curr_slot = 0;
            }
            else
            {
                // this is end()
                curr_slot = curr_leaf->slotuse;
            }

            return tmp;
        }

        //! Prefix-- backstep the iterator to the last slot.
        const_iterator& operator--()
        {
            if (curr_slot > 0)
            {
                --curr_slot;
            }
            else if (curr_leaf->prev_leaf != nullptr)
            {
                curr_leaf = curr_leaf->prev_leaf;
                curr_slot = curr_leaf->slotuse - 1;
            }
            else
            {
                // this is begin()
                curr_slot = 0;
            }

            return *this;
        }

        //! Postfix-- backstep the iterator to the last slot.
        const_iterator operator--(int)
        {
            const_iterator tmp = *this; // copy ourselves

            if (curr_slot > 0)
            {
                --curr_slot;
            }
            else if (curr_leaf->prev_leaf != nullptr)
            {
                curr_leaf = curr_leaf->prev_leaf;
                curr_slot = curr_leaf->slotuse - 1;
            }
            else
            {
                // this is begin()
                curr_slot = 0;
            }

            return tmp;
        }

        //! Equality of iterators.
        bool operator==(const const_iterator& x) const
        {
            return (x.curr_leaf == curr_leaf) && (x.curr_slot == curr_slot);
        }

        //! Inequality of iterators.
        bool operator!=(const const_iterator& x) const
        {
            return (x.curr_leaf != curr_leaf) || (x.curr_slot != curr_slot);
        }
    };

    //! STL-like mutable reverse iterator object for B+ tree items. The
    //! iterator points to a specific slot number in a leaf.
    class reverse_iterator
    {
    public:
        // *** Types

        //! The key type of the btree. Returned by key().
        typedef typename BTree::key_type key_type;

        //! The value type of the btree. Returned by operator*().
        typedef typename BTree::value_type value_type;

        //! Reference to the value_type. STL required.
        typedef value_type& reference;

        //! Pointer to the value_type. STL required.
        typedef value_type* pointer;

        //! STL-magic iterator category
        typedef std::bidirectional_iterator_tag iterator_category;

        //! STL-magic
        typedef ptrdiff_t difference_type;

        //! Our own type
        typedef reverse_iterator self;

    private:
        // *** Members

        //! The currently referenced leaf node of the tree
        typename BTree::LeafNode* curr_leaf;

        //! One slot past the current key/data slot referenced.
        unsigned short curr_slot;

        //! Friendly to the const_iterator, so it may access the two data items
        //! directly
        friend class iterator;

        //! Also friendly to the const_iterator, so it may access the two data
        //! items directly
        friend class const_iterator;

        //! Also friendly to the const_iterator, so it may access the two data
        //! items directly
        friend class const_reverse_iterator;

        // The macro TLX_BTREE_FRIENDS can be used by outside class to access
        // the B+ tree internals. This was added for wxBTreeDemo to be able to
        // draw the tree.
        TLX_BTREE_FRIENDS;

    public:
        // *** Methods

        //! Default-Constructor of a reverse iterator
        reverse_iterator() : curr_leaf(nullptr), curr_slot(0)
        {
        }

        //! Initializing-Constructor of a mutable reverse iterator
        reverse_iterator(typename BTree::LeafNode* l, unsigned short s)
            : curr_leaf(l), curr_slot(s)
        {
        }

        //! Copy-constructor from a mutable iterator
        reverse_iterator(const iterator& it)
            : curr_leaf(it.curr_leaf), curr_slot(it.curr_slot)
        {
        }

        //! Dereference the iterator.
        reference operator*() const
        {
            TLX_BTREE_ASSERT(curr_slot > 0);
            return curr_leaf->slotdata[curr_slot - 1];
        }

        //! Dereference the iterator.
        pointer operator->() const
        {
            TLX_BTREE_ASSERT(curr_slot > 0);
            return &curr_leaf->slotdata[curr_slot - 1];
        }

        //! Key of the current slot.
        const key_type& key() const
        {
            TLX_BTREE_ASSERT(curr_slot > 0);
            return curr_leaf->key(curr_slot - 1);
        }

        //! Prefix++ advance the iterator to the next slot.
        reverse_iterator& operator++()
        {
            if (curr_slot > 1)
            {
                --curr_slot;
            }
            else if (curr_leaf->prev_leaf != nullptr)
            {
                curr_leaf = curr_leaf->prev_leaf;
                curr_slot = curr_leaf->slotuse;
            }
            else
            {
                // this is begin() == rend()
                curr_slot = 0;
            }

            return *this;
        }

        //! Postfix++ advance the iterator to the next slot.
        reverse_iterator operator++(int)
        {
            reverse_iterator tmp = *this; // copy ourselves

            if (curr_slot > 1)
            {
                --curr_slot;
            }
            else if (curr_leaf->prev_leaf != nullptr)
            {
                curr_leaf = curr_leaf->prev_leaf;
                curr_slot = curr_leaf->slotuse;
            }
            else
            {
                // this is begin() == rend()
                curr_slot = 0;
            }

            return tmp;
        }

        //! Prefix-- backstep the iterator to the last slot.
        reverse_iterator& operator--()
        {
            if (curr_slot < curr_leaf->slotuse)
            {
                ++curr_slot;
            }
            else if (curr_leaf->next_leaf != nullptr)
            {
                curr_leaf = curr_leaf->next_leaf;
                curr_slot = 1;
            }
            else
            {
                // this is end() == rbegin()
                curr_slot = curr_leaf->slotuse;
            }

            return *this;
        }

        //! Postfix-- backstep the iterator to the last slot.
        reverse_iterator operator--(int)
        {
            reverse_iterator tmp = *this; // copy ourselves

            if (curr_slot < curr_leaf->slotuse)
            {
                ++curr_slot;
            }
            else if (curr_leaf->next_leaf != nullptr)
            {
                curr_leaf = curr_leaf->next_leaf;
                curr_slot = 1;
            }
            else
            {
                // this is end() == rbegin()
                curr_slot = curr_leaf->slotuse;
            }

            return tmp;
        }

        //! Equality of iterators.
        bool operator==(const reverse_iterator& x) const
        {
            return (x.curr_leaf == curr_leaf) && (x.curr_slot == curr_slot);
        }

        //! Inequality of iterators.
        bool operator!=(const reverse_iterator& x) const
        {
            return (x.curr_leaf != curr_leaf) || (x.curr_slot != curr_slot);
        }
    };

    //! STL-like read-only reverse iterator object for B+ tree items. The
    //! iterator points to a specific slot number in a leaf.
    class const_reverse_iterator
    {
    public:
        // *** Types

        //! The key type of the btree. Returned by key().
        typedef typename BTree::key_type key_type;

        //! The value type of the btree. Returned by operator*().
        typedef typename BTree::value_type value_type;

        //! Reference to the value_type. STL required.
        typedef const value_type& reference;

        //! Pointer to the value_type. STL required.
        typedef const value_type* pointer;

        //! STL-magic iterator category
        typedef std::bidirectional_iterator_tag iterator_category;

        //! STL-magic
        typedef ptrdiff_t difference_type;

        //! Our own type
        typedef const_reverse_iterator self;

    private:
        // *** Members

        //! The currently referenced leaf node of the tree
        const typename BTree::LeafNode* curr_leaf;

        //! One slot past the current key/data slot referenced.
        unsigned short curr_slot;

        //! Friendly to the const_iterator, so it may access the two data items
        //! directly.
        friend class reverse_iterator;

        // The macro TLX_BTREE_FRIENDS can be used by outside class to access
        // the B+ tree internals. This was added for wxBTreeDemo to be able to
        // draw the tree.
        TLX_BTREE_FRIENDS;

    public:
        // *** Methods

        //! Default-Constructor of a const reverse iterator.
        const_reverse_iterator() : curr_leaf(nullptr), curr_slot(0)
        {
        }

        //! Initializing-Constructor of a const reverse iterator.
        const_reverse_iterator(const typename BTree::LeafNode* l,
                               unsigned short s)
            : curr_leaf(l), curr_slot(s)
        {
        }

        //! Copy-constructor from a mutable iterator.
        const_reverse_iterator(const iterator& it)
            : curr_leaf(it.curr_leaf), curr_slot(it.curr_slot)
        {
        }

        //! Copy-constructor from a const iterator.
        const_reverse_iterator(const const_iterator& it)
            : curr_leaf(it.curr_leaf), curr_slot(it.curr_slot)
        {
        }

        //! Copy-constructor from a mutable reverse iterator.
        const_reverse_iterator(const reverse_iterator& it)
            : curr_leaf(it.curr_leaf), curr_slot(it.curr_slot)
        {
        }

        //! Dereference the iterator.
        reference operator*() const
        {
            TLX_BTREE_ASSERT(curr_slot > 0);
            return curr_leaf->slotdata[curr_slot - 1];
        }

        //! Dereference the iterator.
        pointer operator->() const
        {
            TLX_BTREE_ASSERT(curr_slot > 0);
            return &curr_leaf->slotdata[curr_slot - 1];
        }

        //! Key of the current slot.
        const key_type& key() const
        {
            TLX_BTREE_ASSERT(curr_slot > 0);
            return curr_leaf->key(curr_slot - 1);
        }

        //! Prefix++ advance the iterator to the previous slot.
        const_reverse_iterator& operator++()
        {
            if (curr_slot > 1)
            {
                --curr_slot;
            }
            else if (curr_leaf->prev_leaf != nullptr)
            {
                curr_leaf = curr_leaf->prev_leaf;
                curr_slot = curr_leaf->slotuse;
            }
            else
            {
                // this is begin() == rend()
                curr_slot = 0;
            }

            return *this;
        }

        //! Postfix++ advance the iterator to the previous slot.
        const_reverse_iterator operator++(int)
        {
            const_reverse_iterator tmp = *this; // copy ourselves

            if (curr_slot > 1)
            {
                --curr_slot;
            }
            else if (curr_leaf->prev_leaf != nullptr)
            {
                curr_leaf = curr_leaf->prev_leaf;
                curr_slot = curr_leaf->slotuse;
            }
            else
            {
                // this is begin() == rend()
                curr_slot = 0;
            }

            return tmp;
        }

        //! Prefix-- backstep the iterator to the next slot.
        const_reverse_iterator& operator--()
        {
            if (curr_slot < curr_leaf->slotuse)
            {
                ++curr_slot;
            }
            else if (curr_leaf->next_leaf != nullptr)
            {
                curr_leaf = curr_leaf->next_leaf;
                curr_slot = 1;
            }
            else
            {
                // this is end() == rbegin()
                curr_slot = curr_leaf->slotuse;
            }

            return *this;
        }

        //! Postfix-- backstep the iterator to the next slot.
        const_reverse_iterator operator--(int)
        {
            const_reverse_iterator tmp = *this; // copy ourselves

            if (curr_slot < curr_leaf->slotuse)
            {
                ++curr_slot;
            }
            else if (curr_leaf->next_leaf != nullptr)
            {
                curr_leaf = curr_leaf->next_leaf;
                curr_slot = 1;
            }
            else
            {
                // this is end() == rbegin()
                curr_slot = curr_leaf->slotuse;
            }

            return tmp;
        }

        //! Equality of iterators.
        bool operator==(const const_reverse_iterator& x) const
        {
            return (x.curr_leaf == curr_leaf) && (x.curr_slot == curr_slot);
        }

        //! Inequality of iterators.
        bool operator!=(const const_reverse_iterator& x) const
        {
            return (x.curr_leaf != curr_leaf) || (x.curr_slot != curr_slot);
        }
    };

    //! \}

public:
    //! \name Small Statistics Structure
    //! \{

    /*!
     * A small struct containing basic statistics about the B+ tree. It can be
     * fetched using get_stats().
     */
    struct tree_stats
    {
        //! Number of items in the B+ tree
        size_type size = 0;

        //! Number of leaves in the B+ tree
        size_type leaves = 0;

        //! Number of inner nodes in the B+ tree
        size_type inner_nodes = 0;

        //! Base B+ tree parameter: The number of key/data slots in each leaf
        static const unsigned short leaf_slots = Self::leaf_slotmax;

        //! Base B+ tree parameter: The number of key slots in each inner node.
        static const unsigned short inner_slots = Self::inner_slotmax;

        //! Return the total number of nodes
        size_type nodes() const
        {
            return inner_nodes + leaves;
        }

        //! Return the average fill of leaves
        double avgfill_leaves() const
        {
            return static_cast<double>(size) / (leaves * leaf_slots);
        }
    };

    //! \}

private:
    //! \name Tree Object Data Members
    //! \{

    //! Pointer to the B+ tree's root node, either leaf or inner node.
    node* root_;

    //! Pointer to first leaf in the double linked leaf chain.
    LeafNode* head_leaf_;

    //! Pointer to last leaf in the double linked leaf chain.
    LeafNode* tail_leaf_;

    //! Other small statistics about the B+ tree.
    tree_stats stats_;

    //! Key comparison object. More comparison functions are generated from
    //! this < relation.
    key_compare key_less_;

    //! Memory allocator.
    allocator_type allocator_;

    //! \}

public:
    //! \name Constructors and Destructor
    //! \{

    //! Default constructor initializing an empty B+ tree with the standard key
    //! comparison function.
    explicit BTree(const allocator_type& alloc = allocator_type())
        : root_(nullptr),
          head_leaf_(nullptr),
          tail_leaf_(nullptr),
          allocator_(alloc)
    {
    }

    //! Constructor initializing an empty B+ tree with a special key
    //! comparison object.
    explicit BTree(const key_compare& kcf,
                   const allocator_type& alloc = allocator_type())
        : root_(nullptr),
          head_leaf_(nullptr),
          tail_leaf_(nullptr),
          key_less_(kcf),
          allocator_(alloc)
    {
    }

    //! Constructor initializing a B+ tree with the range [first,last). The
    //! range need not be sorted. To create a B+ tree from a sorted range, use
    //! bulk_load().
    template <class InputIterator>
    BTree(InputIterator first, InputIterator last,
          const allocator_type& alloc = allocator_type())
        : root_(nullptr),
          head_leaf_(nullptr),
          tail_leaf_(nullptr),
          allocator_(alloc)
    {
        insert(first, last);
    }

    //! Constructor initializing a B+ tree with the range [first,last) and a
    //! special key comparison object.  The range need not be sorted. To create
    //! a B+ tree from a sorted range, use bulk_load().
    template <class InputIterator>
    BTree(InputIterator first, InputIterator last, const key_compare& kcf,
          const allocator_type& alloc = allocator_type())
        : root_(nullptr),
          head_leaf_(nullptr),
          tail_leaf_(nullptr),
          key_less_(kcf),
          allocator_(alloc)
    {
        insert(first, last);
    }

    //! Frees up all used B+ tree memory pages
    ~BTree()
    {
        clear();
    }

    //! Fast swapping of two identical B+ tree objects.
    void swap(BTree& from) noexcept
    {
        std::swap(root_, from.root_);
        std::swap(head_leaf_, from.head_leaf_);
        std::swap(tail_leaf_, from.tail_leaf_);
        std::swap(stats_, from.stats_);
        std::swap(key_less_, from.key_less_);
        std::swap(allocator_, from.allocator_);
    }

    //! \}

public:
    //! \name Key and Value Comparison Function Objects
    //! \{

    //! Function class to compare value_type objects. Required by the STL
    class value_compare
    {
    private:
        //! Key comparison function from the template parameter
        key_compare key_comp;

        //! Constructor called from BTree::value_comp()
        explicit value_compare(key_compare kc) : key_comp(kc)
        {
        }

        //! Friendly to the btree class so it may call the constructor
        friend class BTree<key_type, value_type, key_of_value, key_compare,
                           traits, allow_duplicates, allocator_type>;

    public:
        //! Function call "less"-operator resulting in true if x < y.
        bool operator()(const value_type& x, const value_type& y) const
        {
            return key_comp(x.first, y.first);
        }
    };

    //! Constant access to the key comparison object sorting the B+ tree.
    key_compare key_comp() const
    {
        return key_less_;
    }

    //! Constant access to a constructed value_type comparison object. Required
    //! by the STL.
    value_compare value_comp() const
    {
        return value_compare(key_less_);
    }

    //! \}

private:
    //! \name Convenient Key Comparison Functions Generated From key_less
    //! \{

    //! True if a < b ? "constructed" from key_less_()
    bool key_less(const key_type& a, const key_type& b) const
    {
        return key_less_(a, b);
    }

    //! True if a <= b ? constructed from key_less()
    bool key_lessequal(const key_type& a, const key_type& b) const
    {
        return !key_less_(b, a);
    }

    //! True if a > b ? constructed from key_less()
    bool key_greater(const key_type& a, const key_type& b) const
    {
        return key_less_(b, a);
    }

    //! True if a >= b ? constructed from key_less()
    bool key_greaterequal(const key_type& a, const key_type& b) const
    {
        return !key_less_(a, b);
    }

    //! True if a == b ? constructed from key_less(). This requires the <
    //! relation to be a total order, otherwise the B+ tree cannot be sorted.
    bool key_equal(const key_type& a, const key_type& b) const
    {
        return !key_less_(a, b) && !key_less_(b, a);
    }

    //! \}

public:
    //! \name Allocators
    //! \{

    //! Return the base node allocator provided during construction.
    allocator_type get_allocator() const
    {
        return allocator_;
    }

    //! \}

private:
    //! \name Node Object Allocation and Deallocation Functions
    //! \{

    //! Return an allocator for LeafNode objects.
    typename LeafNode::alloc_type leaf_node_allocator()
    {
        return typename LeafNode::alloc_type(allocator_);
    }

    //! Return an allocator for InnerNode objects.
    typename InnerNode::alloc_type inner_node_allocator()
    {
        return typename InnerNode::alloc_type(allocator_);
    }

    //! Allocate and initialize a leaf node
    LeafNode* allocate_leaf()
    {
        LeafNode* n = new (leaf_node_allocator().allocate(1)) LeafNode();
        n->initialize();
        stats_.leaves++;
        return n;
    }

    //! Allocate and initialize an inner node
    InnerNode* allocate_inner(unsigned short level)
    {
        InnerNode* n = new (inner_node_allocator().allocate(1)) InnerNode();
        n->initialize(level);
        stats_.inner_nodes++;
        return n;
    }

    //! Correctly free either inner or leaf node, destructs all contained key
    //! and value objects.
    void free_node(node* n)
    {
        if (n->is_leafnode())
        {
            LeafNode* ln = static_cast<LeafNode*>(n);
            typename LeafNode::alloc_type a(leaf_node_allocator());
            std::allocator_traits<typename LeafNode::alloc_type>::destroy(a,
                                                                          ln);
            std::allocator_traits<typename LeafNode::alloc_type>::deallocate(
                a, ln, 1);
            stats_.leaves--;
        }
        else
        {
            InnerNode* in = static_cast<InnerNode*>(n);
            typename InnerNode::alloc_type a(inner_node_allocator());
            std::allocator_traits<typename InnerNode::alloc_type>::destroy(a,
                                                                           in);
            std::allocator_traits<typename InnerNode::alloc_type>::deallocate(
                a, in, 1);
            stats_.inner_nodes--;
        }
    }

    //! \}

public:
    //! \name Fast Destruction of the B+ Tree
    //! \{

    //! Frees all key/data pairs and all nodes of the tree.
    void clear()
    {
        if (root_)
        {
            clear_recursive(root_);
            free_node(root_);

            root_ = nullptr;
            head_leaf_ = tail_leaf_ = nullptr;

            stats_ = tree_stats();
        }

        TLX_BTREE_ASSERT(stats_.size == 0);
    }

private:
    //! Recursively free up nodes.
    void clear_recursive(node* n)
    {
        if (n->is_leafnode())
        {
            LeafNode* leafnode = static_cast<LeafNode*>(n);

            for (unsigned short slot = 0; slot < leafnode->slotuse; ++slot)
            {
                // data objects are deleted by LeafNode's destructor
            }
        }
        else
        {
            InnerNode* innernode = static_cast<InnerNode*>(n);

            for (unsigned short slot = 0; slot < innernode->slotuse + 1; ++slot)
            {
                clear_recursive(innernode->childid[slot]);
                free_node(innernode->childid[slot]);
            }
        }
    }

    //! \}

public:
    //! \name STL Iterator Construction Functions
    //! \{

    //! Constructs a read/data-write iterator that points to the first slot in
    //! the first leaf of the B+ tree.
    iterator begin()
    {
        return iterator(head_leaf_, 0);
    }

    //! Constructs a read/data-write iterator that points to the first invalid
    //! slot in the last leaf of the B+ tree.
    iterator end()
    {
        return iterator(tail_leaf_, tail_leaf_ ? tail_leaf_->slotuse : 0);
    }

    //! Constructs a read-only constant iterator that points to the first slot
    //! in the first leaf of the B+ tree.
    const_iterator begin() const
    {
        return const_iterator(head_leaf_, 0);
    }

    //! Constructs a read-only constant iterator that points to the first
    //! invalid slot in the last leaf of the B+ tree.
    const_iterator end() const
    {
        return const_iterator(tail_leaf_, tail_leaf_ ? tail_leaf_->slotuse : 0);
    }

    //! Constructs a read/data-write reverse iterator that points to the first
    //! invalid slot in the last leaf of the B+ tree. Uses STL magic.
    reverse_iterator rbegin()
    {
        return reverse_iterator(end());
    }

    //! Constructs a read/data-write reverse iterator that points to the first
    //! slot in the first leaf of the B+ tree. Uses STL magic.
    reverse_iterator rend()
    {
        return reverse_iterator(begin());
    }

    //! Constructs a read-only reverse iterator that points to the first
    //! invalid slot in the last leaf of the B+ tree. Uses STL magic.
    const_reverse_iterator rbegin() const
    {
        return const_reverse_iterator(end());
    }

    //! Constructs a read-only reverse iterator that points to the first slot
    //! in the first leaf of the B+ tree. Uses STL magic.
    const_reverse_iterator rend() const
    {
        return const_reverse_iterator(begin());
    }

    //! \}

private:
    //! \name B+ Tree Node Binary Search Functions
    //! \{

    //! Searches for the first key in the node n greater or equal to key. Uses
    //! binary search with an optional linear self-verification. This is a
    //! template function, because the slotkey array is located at different
    //! places in LeafNode and InnerNode.
    template <typename node_type>
    unsigned short find_lower(const node_type* n, const key_type& key) const
    {
        if (sizeof(*n) > traits::binsearch_threshold)
        {
            if (n->slotuse == 0)
                return 0;

            unsigned short lo = 0, hi = n->slotuse;

            while (lo < hi)
            {
                unsigned short mid = (lo + hi) >> 1;

                if (key_lessequal(key, n->key(mid)))
                {
                    hi = mid; // key <= mid
                }
                else
                {
                    lo = mid + 1; // key > mid
                }
            }

            TLX_BTREE_PRINT("BTree::find_lower: on " << n << " key " << key
                                                     << " -> " << lo << " / "
                                                     << hi);

            // verify result using simple linear search
            if (self_verify)
            {
                unsigned short i = 0;
                while (i < n->slotuse && key_less(n->key(i), key))
                    ++i;

                TLX_BTREE_PRINT("BTree::find_lower: testfind: " << i);
                TLX_BTREE_ASSERT(i == lo);
            }

            return lo;
        }

        // for nodes <= binsearch_threshold do linear search.
        unsigned short lo = 0;
        while (lo < n->slotuse && key_less(n->key(lo), key))
            ++lo;
        return lo;
    }

    //! Searches for the first key in the node n greater than key. Uses binary
    //! search with an optional linear self-verification. This is a template
    //! function, because the slotkey array is located at different places in
    //! LeafNode and InnerNode.
    template <typename node_type>
    unsigned short find_upper(const node_type* n, const key_type& key) const
    {
        if (sizeof(*n) > traits::binsearch_threshold)
        {
            if (n->slotuse == 0)
                return 0;

            unsigned short lo = 0, hi = n->slotuse;

            while (lo < hi)
            {
                unsigned short mid = (lo + hi) >> 1;

                if (key_less(key, n->key(mid)))
                {
                    hi = mid; // key < mid
                }
                else
                {
                    lo = mid + 1; // key >= mid
                }
            }

            TLX_BTREE_PRINT("BTree::find_upper: on " << n << " key " << key
                                                     << " -> " << lo << " / "
                                                     << hi);

            // verify result using simple linear search
            if (self_verify)
            {
                unsigned short i = 0;
                while (i < n->slotuse && key_lessequal(n->key(i), key))
                    ++i;

                TLX_BTREE_PRINT("BTree::find_upper testfind: " << i);
                TLX_BTREE_ASSERT(i == hi);
            }

            return lo;
        }

        // for nodes <= binsearch_threshold do linear search.
        unsigned short lo = 0;
        while (lo < n->slotuse && key_lessequal(n->key(lo), key))
            ++lo;
        return lo;
    }

    //! \}

public:
    //! \name Access Functions to the Item Count
    //! \{

    //! Return the number of key/data pairs in the B+ tree
    size_type size() const
    {
        return stats_.size;
    }

    //! Returns true if there is at least one key/data pair in the B+ tree
    bool empty() const
    {
        return (size() == size_type(0));
    }

    //! Returns the largest possible size of the B+ Tree. This is just a
    //! function required by the STL standard, the B+ Tree can hold more items.
    size_type max_size() const
    {
        return size_type(-1);
    }

    //! Return a const reference to the current statistics.
    const struct tree_stats& get_stats() const
    {
        return stats_;
    }

    //! \}

public:
    //! \name STL Access Functions Querying the Tree by Descending to a Leaf
    //! \{

    //! Non-STL function checking whether a key is in the B+ tree. The same as
    //! (find(k) != end()) or (count() != 0).
    bool exists(const key_type& key) const
    {
        const node* n = root_;
        if (!n)
            return false;

        while (!n->is_leafnode())
        {
            const InnerNode* inner = static_cast<const InnerNode*>(n);
            unsigned short slot = find_lower(inner, key);

            n = inner->childid[slot];
        }

        const LeafNode* leaf = static_cast<const LeafNode*>(n);

        unsigned short slot = find_lower(leaf, key);
        return (slot < leaf->slotuse && key_equal(key, leaf->key(slot)));
    }

    //! Tries to locate a key in the B+ tree and returns an iterator to the
    //! key/data slot if found. If unsuccessful it returns end().
    iterator find(const key_type& key)
    {
        node* n = root_;
        if (!n)
            return end();

        while (!n->is_leafnode())
        {
            const InnerNode* inner = static_cast<const InnerNode*>(n);
            unsigned short slot = find_lower(inner, key);

            n = inner->childid[slot];
        }

        LeafNode* leaf = static_cast<LeafNode*>(n);

        unsigned short slot = find_lower(leaf, key);
        return (slot < leaf->slotuse && key_equal(key, leaf->key(slot))) ?
                   iterator(leaf, slot) :
                   end();
    }

    //! Tries to locate a key in the B+ tree and returns an constant iterator to
    //! the key/data slot if found. If unsuccessful it returns end().
    const_iterator find(const key_type& key) const
    {
        const node* n = root_;
        if (!n)
            return end();

        while (!n->is_leafnode())
        {
            const InnerNode* inner = static_cast<const InnerNode*>(n);
            unsigned short slot = find_lower(inner, key);

            n = inner->childid[slot];
        }

        const LeafNode* leaf = static_cast<const LeafNode*>(n);

        unsigned short slot = find_lower(leaf, key);
        return (slot < leaf->slotuse && key_equal(key, leaf->key(slot))) ?
                   const_iterator(leaf, slot) :
                   end();
    }

    //! Tries to locate a key in the B+ tree and returns the number of identical
    //! key entries found.
    size_type count(const key_type& key) const
    {
        const node* n = root_;
        if (!n)
            return 0;

        while (!n->is_leafnode())
        {
            const InnerNode* inner = static_cast<const InnerNode*>(n);
            unsigned short slot = find_lower(inner, key);

            n = inner->childid[slot];
        }

        const LeafNode* leaf = static_cast<const LeafNode*>(n);

        unsigned short slot = find_lower(leaf, key);
        size_type num = 0;

        while (leaf && slot < leaf->slotuse && key_equal(key, leaf->key(slot)))
        {
            ++num;
            if (++slot >= leaf->slotuse)
            {
                leaf = leaf->next_leaf;
                slot = 0;
            }
        }

        return num;
    }

    //! Searches the B+ tree and returns an iterator to the first pair equal to
    //! or greater than key, or end() if all keys are smaller.
    iterator lower_bound(const key_type& key)
    {
        node* n = root_;
        if (!n)
            return end();

        while (!n->is_leafnode())
        {
            const InnerNode* inner = static_cast<const InnerNode*>(n);
            unsigned short slot = find_lower(inner, key);

            n = inner->childid[slot];
        }

        LeafNode* leaf = static_cast<LeafNode*>(n);

        unsigned short slot = find_lower(leaf, key);
        return iterator(leaf, slot);
    }

    //! Searches the B+ tree and returns a constant iterator to the first pair
    //! equal to or greater than key, or end() if all keys are smaller.
    const_iterator lower_bound(const key_type& key) const
    {
        const node* n = root_;
        if (!n)
            return end();

        while (!n->is_leafnode())
        {
            const InnerNode* inner = static_cast<const InnerNode*>(n);
            unsigned short slot = find_lower(inner, key);

            n = inner->childid[slot];
        }

        const LeafNode* leaf = static_cast<const LeafNode*>(n);

        unsigned short slot = find_lower(leaf, key);
        return const_iterator(leaf, slot);
    }

    //! Searches the B+ tree and returns an iterator to the first pair greater
    //! than key, or end() if all keys are smaller or equal.
    iterator upper_bound(const key_type& key)
    {
        node* n = root_;
        if (!n)
            return end();

        while (!n->is_leafnode())
        {
            const InnerNode* inner = static_cast<const InnerNode*>(n);
            unsigned short slot = find_upper(inner, key);

            n = inner->childid[slot];
        }

        LeafNode* leaf = static_cast<LeafNode*>(n);

        unsigned short slot = find_upper(leaf, key);
        return iterator(leaf, slot);
    }

    //! Searches the B+ tree and returns a constant iterator to the first pair
    //! greater than key, or end() if all keys are smaller or equal.
    const_iterator upper_bound(const key_type& key) const
    {
        const node* n = root_;
        if (!n)
            return end();

        while (!n->is_leafnode())
        {
            const InnerNode* inner = static_cast<const InnerNode*>(n);
            unsigned short slot = find_upper(inner, key);

            n = inner->childid[slot];
        }

        const LeafNode* leaf = static_cast<const LeafNode*>(n);

        unsigned short slot = find_upper(leaf, key);
        return const_iterator(leaf, slot);
    }

    //! Searches the B+ tree and returns both lower_bound() and upper_bound().
    std::pair<iterator, iterator> equal_range(const key_type& key)
    {
        return std::pair<iterator, iterator>(lower_bound(key),
                                             upper_bound(key));
    }

    //! Searches the B+ tree and returns both lower_bound() and upper_bound().
    std::pair<const_iterator, const_iterator> equal_range(
        const key_type& key) const
    {
        return std::pair<const_iterator, const_iterator>(lower_bound(key),
                                                         upper_bound(key));
    }

    //! \}

public:
    //! \name B+ Tree Object Comparison Functions
    //! \{

    //! Equality relation of B+ trees of the same type. B+ trees of the same
    //! size and equal elements (both key and data) are considered equal. Beware
    //! of the random ordering of duplicate keys.
    bool operator==(const BTree& other) const
    {
        return (size() == other.size()) &&
               std::equal(begin(), end(), other.begin());
    }

    //! Inequality relation. Based on operator==.
    bool operator!=(const BTree& other) const
    {
        return !(*this == other);
    }

    //! Total ordering relation of B+ trees of the same type. It uses
    //! std::lexicographical_compare() for the actual comparison of elements.
    bool operator<(const BTree& other) const
    {
        return std::lexicographical_compare(begin(), end(), other.begin(),
                                            other.end());
    }

    //! Greater relation. Based on operator<.
    bool operator>(const BTree& other) const
    {
        return other < *this;
    }

    //! Less-equal relation. Based on operator<.
    bool operator<=(const BTree& other) const
    {
        return !(other < *this);
    }

    //! Greater-equal relation. Based on operator<.
    bool operator>=(const BTree& other) const
    {
        return !(*this < other);
    }

    //! \}

public:
    //! \name Fast Copy: Assign Operator and Copy Constructors
    //! \{

    //! Assignment operator. All the key/data pairs are copied.
    BTree& operator=(const BTree& other)
    {
        if (this != &other)
        {
            clear();

            key_less_ = other.key_comp();
            allocator_ = other.get_allocator();

            if (other.size() != 0)
            {
                stats_.leaves = stats_.inner_nodes = 0;
                if (other.root_)
                {
                    root_ = copy_recursive(other.root_);
                }
                stats_ = other.stats_;
            }

            if (self_verify)
                verify();
        }
        return *this;
    }

    //! Copy constructor. The newly initialized B+ tree object will contain a
    //! copy of all key/data pairs.
    BTree(const BTree& other)
        : root_(nullptr),
          head_leaf_(nullptr),
          tail_leaf_(nullptr),
          stats_(other.stats_),
          key_less_(other.key_comp()),
          allocator_(other.get_allocator())
    {
        if (size() > 0)
        {
            stats_.leaves = stats_.inner_nodes = 0;
            if (other.root_)
            {
                root_ = copy_recursive(other.root_);
            }
            if (self_verify)
                verify();
        }
    }

private:
    //! Recursively copy nodes from another B+ tree object
    struct node* copy_recursive(const node* n)
    {
        if (n->is_leafnode())
        {
            const LeafNode* leaf = static_cast<const LeafNode*>(n);
            LeafNode* newleaf = allocate_leaf();

            newleaf->slotuse = leaf->slotuse;
            std::copy(leaf->slotdata, leaf->slotdata + leaf->slotuse,
                      newleaf->slotdata);

            if (head_leaf_ == nullptr)
            {
                head_leaf_ = tail_leaf_ = newleaf;
                newleaf->prev_leaf = newleaf->next_leaf = nullptr;
            }
            else
            {
                newleaf->prev_leaf = tail_leaf_;
                tail_leaf_->next_leaf = newleaf;
                tail_leaf_ = newleaf;
            }

            return newleaf;
        }

        const InnerNode* inner = static_cast<const InnerNode*>(n);
        InnerNode* newinner = allocate_inner(inner->level);

        newinner->slotuse = inner->slotuse;
        std::copy(inner->slotkey, inner->slotkey + inner->slotuse,
                  newinner->slotkey);

        for (unsigned short slot = 0; slot <= inner->slotuse; ++slot)
            newinner->childid[slot] = copy_recursive(inner->childid[slot]);

        return newinner;
    }

    //! \}

public:
    //! \name Public Insertion Functions
    //! \{

    //! Attempt to insert a key/data pair into the B+ tree. If the tree does not
    //! allow duplicate keys, then the insert may fail if it is already present.
    std::pair<iterator, bool> insert(const value_type& x)
    {
        return insert_start(key_of_value::get(x), x);
    }

    //! Attempt to insert a key/data pair into the B+ tree. The iterator hint is
    //! currently ignored by the B+ tree insertion routine.
    iterator insert(iterator /* hint */, const value_type& x)
    {
        return insert_start(key_of_value::get(x), x).first;
    }

    //! Attempt to insert the range [first,last) of value_type pairs into the B+
    //! tree. Each key/data pair is inserted individually; to bulk load the
    //! tree, use a constructor with range.
    template <typename InputIterator>
    void insert(InputIterator first, InputIterator last)
    {
        InputIterator iter = first;
        while (iter != last)
        {
            insert(*iter);
            ++iter;
        }
    }

    //! \}

private:
    //! \name Private Insertion Functions
    //! \{

    //! Start the insertion descent at the current root and handle root splits.
    //! Returns true if the item was inserted
    std::pair<iterator, bool> insert_start(const key_type& key,
                                           const value_type& value)
    {
        node* newchild = nullptr;
        key_type newkey = key_type();

        if (root_ == nullptr)
        {
            root_ = head_leaf_ = tail_leaf_ = allocate_leaf();
        }

        std::pair<iterator, bool> r =
            insert_descend(root_, key, value, &newkey, &newchild);

        if (newchild)
        {
            // this only occurs if insert_descend() could not insert the key
            // into the root node, this mean the root is full and a new root
            // needs to be created.
            InnerNode* newroot = allocate_inner(root_->level + 1);
            newroot->slotkey[0] = newkey;

            newroot->childid[0] = root_;
            newroot->childid[1] = newchild;

            newroot->slotuse = 1;

            root_ = newroot;
        }

        // increment size if the item was inserted
        if (r.second)
            ++stats_.size;

#ifdef TLX_BTREE_DEBUG
        if (debug)
            print(std::cout);
#endif

        if (self_verify)
        {
            verify();
            TLX_BTREE_ASSERT(exists(key));
        }

        return r;
    }

    /*!
     * Insert an item into the B+ tree.
     *
     * Descend down the nodes to a leaf, insert the key/data pair in a free
     * slot. If the node overflows, then it must be split and the new split node
     * inserted into the parent. Unroll / this splitting up to the root.
     */
    std::pair<iterator, bool> insert_descend(node* n, const key_type& key,
                                             const value_type& value,
                                             key_type* splitkey,
                                             node** splitnode)
    {
        if (!n->is_leafnode())
        {
            InnerNode* inner = static_cast<InnerNode*>(n);

            key_type newkey = key_type();
            node* newchild = nullptr;

            unsigned short slot = find_lower(inner, key);

            TLX_BTREE_PRINT("BTree::insert_descend into "
                            << inner->childid[slot]);

            std::pair<iterator, bool> r = insert_descend(
                inner->childid[slot], key, value, &newkey, &newchild);

            if (newchild)
            {
                TLX_BTREE_PRINT("BTree::insert_descend newchild"
                                << " with key " << newkey << " node "
                                << newchild << " at slot " << slot);

                if (inner->is_full())
                {
                    split_inner_node(inner, splitkey, splitnode, slot);

                    TLX_BTREE_PRINT("BTree::insert_descend done split_inner:"
                                    << " putslot: " << slot << " putkey: "
                                    << newkey << " upkey: " << *splitkey);

#ifdef TLX_BTREE_DEBUG
                    if (debug)
                    {
                        print_node(std::cout, inner);
                        print_node(std::cout, *splitnode);
                    }
#endif

                    // check if insert slot is in the split sibling node
                    TLX_BTREE_PRINT("BTree::insert_descend switch: "
                                    << slot << " > " << inner->slotuse + 1);

                    if (slot == inner->slotuse + 1 &&
                        inner->slotuse < (*splitnode)->slotuse)
                    {
                        // special case when the insert slot matches the split
                        // place between the two nodes, then the insert key
                        // becomes the split key.

                        TLX_BTREE_ASSERT(inner->slotuse + 1 < inner_slotmax);

                        InnerNode* split = static_cast<InnerNode*>(*splitnode);

                        // move the split key and it's datum into the left node
                        inner->slotkey[inner->slotuse] = *splitkey;
                        inner->childid[inner->slotuse + 1] = split->childid[0];
                        inner->slotuse++;

                        // set new split key and move corresponding datum into
                        // right node
                        split->childid[0] = newchild;
                        *splitkey = newkey;

                        return r;
                    }

                    if (slot >= inner->slotuse + 1)
                    {
                        // in case the insert slot is in the newly create split
                        // node, we reuse the code below.

                        slot -= inner->slotuse + 1;
                        inner = static_cast<InnerNode*>(*splitnode);
                        TLX_BTREE_PRINT("BTree::insert_descend switching to "
                                        "splitted node "
                                        << inner << " slot " << slot);
                    }
                }

                // move items and put pointer to child node into correct slot
                TLX_BTREE_ASSERT(slot >= 0 && slot <= inner->slotuse);

                std::copy_backward(inner->slotkey + slot,
                                   inner->slotkey + inner->slotuse,
                                   inner->slotkey + inner->slotuse + 1);
                std::copy_backward(inner->childid + slot,
                                   inner->childid + inner->slotuse + 1,
                                   inner->childid + inner->slotuse + 2);

                inner->slotkey[slot] = newkey;
                inner->childid[slot + 1] = newchild;
                inner->slotuse++;
            }

            return r;
        }

        // n->is_leafnode() == true
        LeafNode* leaf = static_cast<LeafNode*>(n);

        unsigned short slot = find_lower(leaf, key);

        if (!allow_duplicates && slot < leaf->slotuse &&
            key_equal(key, leaf->key(slot)))
        {
            return std::pair<iterator, bool>(iterator(leaf, slot), false);
        }

        if (leaf->is_full())
        {
            split_leaf_node(leaf, splitkey, splitnode);

            // check if insert slot is in the split sibling node
            if (slot >= leaf->slotuse)
            {
                slot -= leaf->slotuse;
                leaf = static_cast<LeafNode*>(*splitnode);
            }
        }

        // move items and put data item into correct data slot
        TLX_BTREE_ASSERT(slot >= 0 && slot <= leaf->slotuse);

        std::copy_backward(leaf->slotdata + slot,
                           leaf->slotdata + leaf->slotuse,
                           leaf->slotdata + leaf->slotuse + 1);

        leaf->slotdata[slot] = value;
        leaf->slotuse++;

        if (splitnode && leaf != *splitnode && slot == leaf->slotuse - 1)
        {
            // special case: the node was split, and the insert is at the
            // last slot of the old node. then the splitkey must be updated.
            *splitkey = key;
        }

        return std::pair<iterator, bool>(iterator(leaf, slot), true);
    }

    //! Split up a leaf node into two equally-filled sibling leaves. Returns the
    //! new nodes and it's insertion key in the two parameters.
    void split_leaf_node(LeafNode* leaf, key_type* out_newkey,
                         node** out_newleaf)
    {
        TLX_BTREE_ASSERT(leaf->is_full());

        unsigned short mid = (leaf->slotuse >> 1);

        TLX_BTREE_PRINT("BTree::split_leaf_node on " << leaf);

        LeafNode* newleaf = allocate_leaf();

        newleaf->slotuse = leaf->slotuse - mid;

        newleaf->next_leaf = leaf->next_leaf;
        if (newleaf->next_leaf == nullptr)
        {
            TLX_BTREE_ASSERT(leaf == tail_leaf_);
            tail_leaf_ = newleaf;
        }
        else
        {
            newleaf->next_leaf->prev_leaf = newleaf;
        }

        std::copy(leaf->slotdata + mid, leaf->slotdata + leaf->slotuse,
                  newleaf->slotdata);

        leaf->slotuse = mid;
        leaf->next_leaf = newleaf;
        newleaf->prev_leaf = leaf;

        *out_newkey = leaf->key(leaf->slotuse - 1);
        *out_newleaf = newleaf;
    }

    //! Split up an inner node into two equally-filled sibling nodes. Returns
    //! the new nodes and it's insertion key in the two parameters. Requires the
    //! slot of the item will be inserted, so the nodes will be the same size
    //! after the insert.
    void split_inner_node(InnerNode* inner, key_type* out_newkey,
                          node** out_newinner, unsigned int addslot)
    {
        TLX_BTREE_ASSERT(inner->is_full());

        unsigned short mid = (inner->slotuse >> 1);

        TLX_BTREE_PRINT("BTree::split_inner: mid " << mid << " addslot "
                                                   << addslot);

        // if the split is uneven and the overflowing item will be put into the
        // larger node, then the smaller split node may underflow
        if (addslot <= mid && mid > inner->slotuse - (mid + 1))
            mid--;

        TLX_BTREE_PRINT("BTree::split_inner: mid " << mid << " addslot "
                                                   << addslot);

        TLX_BTREE_PRINT("BTree::split_inner_node on "
                        << inner << " into two nodes " << mid << " and "
                        << inner->slotuse - (mid + 1) << " sized");

        InnerNode* newinner = allocate_inner(inner->level);

        newinner->slotuse = inner->slotuse - (mid + 1);

        std::copy(inner->slotkey + mid + 1, inner->slotkey + inner->slotuse,
                  newinner->slotkey);
        std::copy(inner->childid + mid + 1, inner->childid + inner->slotuse + 1,
                  newinner->childid);

        inner->slotuse = mid;

        *out_newkey = inner->key(mid);
        *out_newinner = newinner;
    }

    //! \}

public:
    //! \name Bulk Loader - Construct Tree from Sorted Sequence
    //! \{

    //! Bulk load a sorted range. Loads items into leaves and constructs a
    //! B-tree above them. The tree must be empty when calling this function.
    template <typename Iterator>
    void bulk_load(Iterator ibegin, Iterator iend)
    {
        TLX_BTREE_ASSERT(empty());

        stats_.size = iend - ibegin;

        // calculate number of leaves needed, round up.
        size_t num_items = iend - ibegin;
        size_t num_leaves = (num_items + leaf_slotmax - 1) / leaf_slotmax;

        TLX_BTREE_PRINT("BTree::bulk_load, level 0: "
                        << stats_.size << " items into " << num_leaves
                        << " leaves with up to "
                        << ((iend - ibegin + num_leaves - 1) / num_leaves)
                        << " items per leaf.");

        Iterator it = ibegin;
        for (size_t i = 0; i < num_leaves; ++i)
        {
            // allocate new leaf node
            LeafNode* leaf = allocate_leaf();

            // copy keys or (key,value) pairs into leaf nodes, uses template
            // switch leaf->set_slot().
            leaf->slotuse = static_cast<int>(num_items / (num_leaves - i));
            for (size_t s = 0; s < leaf->slotuse; ++s, ++it)
                leaf->set_slot(s, *it);

            if (tail_leaf_ != nullptr)
            {
                tail_leaf_->next_leaf = leaf;
                leaf->prev_leaf = tail_leaf_;
            }
            else
            {
                head_leaf_ = leaf;
            }
            tail_leaf_ = leaf;

            num_items -= leaf->slotuse;
        }

        TLX_BTREE_ASSERT(it == iend && num_items == 0);

        // if the btree is so small to fit into one leaf, then we're done.
        if (head_leaf_ == tail_leaf_)
        {
            root_ = head_leaf_;
            return;
        }

        TLX_BTREE_ASSERT(stats_.leaves == num_leaves);

        // create first level of inner nodes, pointing to the leaves.
        size_t num_parents =
            (num_leaves + (inner_slotmax + 1) - 1) / (inner_slotmax + 1);

        TLX_BTREE_PRINT("BTree::bulk_load, level 1: "
                        << num_leaves << " leaves in " << num_parents
                        << " inner nodes with up to "
                        << ((num_leaves + num_parents - 1) / num_parents)
                        << " leaves per inner node.");

        // save inner nodes and maxkey for next level.
        typedef std::pair<InnerNode*, const key_type*> nextlevel_type;
        nextlevel_type* nextlevel = new nextlevel_type[num_parents];

        LeafNode* leaf = head_leaf_;
        for (size_t i = 0; i < num_parents; ++i)
        {
            // allocate new inner node at level 1
            InnerNode* n = allocate_inner(1);

            n->slotuse = static_cast<int>(num_leaves / (num_parents - i));
            TLX_BTREE_ASSERT(n->slotuse > 0);
            // this counts keys, but an inner node has keys+1 children.
            --n->slotuse;

            // copy last key from each leaf and set child
            for (unsigned short s = 0; s < n->slotuse; ++s)
            {
                n->slotkey[s] = leaf->key(leaf->slotuse - 1);
                n->childid[s] = leaf;
                leaf = leaf->next_leaf;
            }
            n->childid[n->slotuse] = leaf;

            // track max key of any descendant.
            nextlevel[i].first = n;
            nextlevel[i].second = &leaf->key(leaf->slotuse - 1);

            leaf = leaf->next_leaf;
            num_leaves -= n->slotuse + 1;
        }

        TLX_BTREE_ASSERT(leaf == nullptr && num_leaves == 0);

        // recursively build inner nodes pointing to inner nodes.
        for (int level = 2; num_parents != 1; ++level)
        {
            size_t num_children = num_parents;
            num_parents =
                (num_children + (inner_slotmax + 1) - 1) / (inner_slotmax + 1);

            TLX_BTREE_PRINT("BTree::bulk_load, level "
                            << level << ": " << num_children << " children in "
                            << num_parents << " inner nodes with up to "
                            << ((num_children + num_parents - 1) / num_parents)
                            << " children per inner node.");

            size_t inner_index = 0;
            for (size_t i = 0; i < num_parents; ++i)
            {
                // allocate new inner node at level
                InnerNode* n = allocate_inner(level);

                n->slotuse = static_cast<int>(num_children / (num_parents - i));
                TLX_BTREE_ASSERT(n->slotuse > 0);
                // this counts keys, but an inner node has keys+1 children.
                --n->slotuse;

                // copy children and maxkeys from nextlevel
                for (unsigned short s = 0; s < n->slotuse; ++s)
                {
                    n->slotkey[s] = *nextlevel[inner_index].second;
                    n->childid[s] = nextlevel[inner_index].first;
                    ++inner_index;
                }
                n->childid[n->slotuse] = nextlevel[inner_index].first;

                // reuse nextlevel array for parents, because we can overwrite
                // slots we've already consumed.
                nextlevel[i].first = n;
                nextlevel[i].second = nextlevel[inner_index].second;

                ++inner_index;
                num_children -= n->slotuse + 1;
            }

            TLX_BTREE_ASSERT(num_children == 0);
        }

        root_ = nextlevel[0].first;
        delete[] nextlevel;

        if (self_verify)
            verify();
    }

    //! \}

private:
    //! \name Support Class Encapsulating Deletion Results
    //! \{

    //! Result flags of recursive deletion.
    enum result_flags_t
    {
        //! Deletion successful and no fix-ups necessary.
        btree_ok = 0,

        //! Deletion not successful because key was not found.
        btree_not_found = 1,

        //! Deletion successful, the last key was updated so parent slotkeys
        //! need updates.
        btree_update_lastkey = 2,

        //! Deletion successful, children nodes were merged and the parent needs
        //! to remove the empty node.
        btree_fixmerge = 4
    };

    //! B+ tree recursive deletion has much information which is needs to be
    //! passed upward.
    struct result_t
    {
        //! Merged result flags
        result_flags_t flags;

        //! The key to be updated at the parent's slot
        key_type lastkey;

        //! Constructor of a result with a specific flag, this can also be used
        //! as for implicit conversion.
        result_t(result_flags_t f = btree_ok) : flags(f), lastkey()
        {
        }

        //! Constructor with a lastkey value.
        result_t(result_flags_t f, const key_type& k) : flags(f), lastkey(k)
        {
        }

        //! Test if this result object has a given flag set.
        bool has(result_flags_t f) const
        {
            return (flags & f) != 0;
        }

        //! Merge two results OR-ing the result flags and overwriting lastkeys.
        result_t& operator|=(const result_t& other)
        {
            flags = result_flags_t(flags | other.flags);

            // we overwrite existing lastkeys on purpose
            if (other.has(btree_update_lastkey))
                lastkey = other.lastkey;

            return *this;
        }
    };

    //! \}

public:
    //! \name Public Erase Functions
    //! \{

    //! Erases one (the first) of the key/data pairs associated with the given
    //! key.
    bool erase_one(const key_type& key)
    {
        TLX_BTREE_PRINT("BTree::erase_one(" << key << ") on btree size "
                                            << size());

        if (self_verify)
            verify();

        if (!root_)
            return false;

        result_t result = erase_one_descend(key, root_, nullptr, nullptr,
                                            nullptr, nullptr, nullptr, 0);

        if (!result.has(btree_not_found))
            --stats_.size;

#ifdef TLX_BTREE_DEBUG
        if (debug)
            print(std::cout);
#endif
        if (self_verify)
            verify();

        return !result.has(btree_not_found);
    }

    //! Erases all the key/data pairs associated with the given key. This is
    //! implemented using erase_one().
    size_type erase(const key_type& key)
    {
        size_type c = 0;

        while (erase_one(key))
        {
            ++c;
            if (!allow_duplicates)
                break;
        }

        return c;
    }

    //! Erase the key/data pair referenced by the iterator.
    void erase(iterator iter)
    {
        TLX_BTREE_PRINT("BTree::erase_iter(" << iter.curr_leaf << ","
                                             << iter.curr_slot
                                             << ") on btree size " << size());

        if (self_verify)
            verify();

        if (!root_)
            return;

        result_t result = erase_iter_descend(iter, root_, nullptr, nullptr,
                                             nullptr, nullptr, nullptr, 0);

        if (!result.has(btree_not_found))
            --stats_.size;

#ifdef TLX_BTREE_DEBUG
        if (debug)
            print(std::cout);
#endif
        if (self_verify)
            verify();
    }

#ifdef BTREE_TODO
    //! Erase all key/data pairs in the range [first,last). This function is
    //! currently not implemented by the B+ Tree.
    void erase(iterator /* first */, iterator /* last */)
    {
        abort();
    }
#endif

    //! \}

private:
    //! \name Private Erase Functions
    //! \{

    /*!
     * Erase one (the first) key/data pair in the B+ tree matching key.
     *
     * Descends down the tree in search of key. During the descent the parent,
     * left and right siblings and their parents are computed and passed
     * down. Once the key/data pair is found, it is removed from the leaf. If
     * the leaf underflows 6 different cases are handled. These cases resolve
     * the underflow by shifting key/data pairs from adjacent sibling nodes,
     * merging two sibling nodes or trimming the tree.
     */
    result_t erase_one_descend(const key_type& key, node* curr, node* left,
                               node* right, InnerNode* left_parent,
                               InnerNode* right_parent, InnerNode* parent,
                               unsigned int parentslot)
    {
        if (curr->is_leafnode())
        {
            LeafNode* leaf = static_cast<LeafNode*>(curr);
            LeafNode* left_leaf = static_cast<LeafNode*>(left);
            LeafNode* right_leaf = static_cast<LeafNode*>(right);

            unsigned short slot = find_lower(leaf, key);

            if (slot >= leaf->slotuse || !key_equal(key, leaf->key(slot)))
            {
                TLX_BTREE_PRINT("Could not find key " << key << " to erase.");

                return btree_not_found;
            }

            TLX_BTREE_PRINT("Found key in leaf " << curr << " at slot "
                                                 << slot);

            std::copy(leaf->slotdata + slot + 1, leaf->slotdata + leaf->slotuse,
                      leaf->slotdata + slot);

            leaf->slotuse--;

            result_t myres = btree_ok;

            // if the last key of the leaf was changed, the parent is notified
            // and updates the key of this leaf
            if (slot == leaf->slotuse)
            {
                if (parent && parentslot < parent->slotuse)
                {
                    TLX_BTREE_ASSERT(parent->childid[parentslot] == curr);
                    parent->slotkey[parentslot] = leaf->key(leaf->slotuse - 1);
                }
                else
                {
                    if (leaf->slotuse >= 1)
                    {
                        TLX_BTREE_PRINT("Scheduling lastkeyupdate: key "
                                        << leaf->key(leaf->slotuse - 1));
                        myres |= result_t(btree_update_lastkey,
                                          leaf->key(leaf->slotuse - 1));
                    }
                    else
                    {
                        TLX_BTREE_ASSERT(leaf == root_);
                    }
                }
            }

            if (leaf->is_underflow() && !(leaf == root_ && leaf->slotuse >= 1))
            {
                // determine what to do about the underflow

                // case : if this empty leaf is the root, then delete all nodes
                // and set root to nullptr.
                if (left_leaf == nullptr && right_leaf == nullptr)
                {
                    TLX_BTREE_ASSERT(leaf == root_);
                    TLX_BTREE_ASSERT(leaf->slotuse == 0);

                    free_node(root_);

                    root_ = leaf = nullptr;
                    head_leaf_ = tail_leaf_ = nullptr;

                    // will be decremented soon by insert_start()
                    TLX_BTREE_ASSERT(stats_.size == 1);
                    TLX_BTREE_ASSERT(stats_.leaves == 0);
                    TLX_BTREE_ASSERT(stats_.inner_nodes == 0);

                    return btree_ok;
                }
                // case : if both left and right leaves would underflow in case
                // of a shift, then merging is necessary. choose the more local
                // merger with our parent
                if ((left_leaf == nullptr || left_leaf->is_few()) &&
                    (right_leaf == nullptr || right_leaf->is_few()))
                {
                    if (left_parent == parent)
                        myres |= merge_leaves(left_leaf, leaf, left_parent);
                    else
                        myres |= merge_leaves(leaf, right_leaf, right_parent);
                }
                // case : the right leaf has extra data, so balance right with
                // current
                else if ((left_leaf != nullptr && left_leaf->is_few()) &&
                         (right_leaf != nullptr && !right_leaf->is_few()))
                {
                    if (right_parent == parent)
                        myres |= shift_left_leaf(leaf, right_leaf, right_parent,
                                                 parentslot);
                    else
                        myres |= merge_leaves(left_leaf, leaf, left_parent);
                }
                // case : the left leaf has extra data, so balance left with
                // current
                else if ((left_leaf != nullptr && !left_leaf->is_few()) &&
                         (right_leaf != nullptr && right_leaf->is_few()))
                {
                    if (left_parent == parent)
                        shift_right_leaf(left_leaf, leaf, left_parent,
                                         parentslot - 1);
                    else
                        myres |= merge_leaves(leaf, right_leaf, right_parent);
                }
                // case : both the leaf and right leaves have extra data and our
                // parent, choose the leaf with more data
                else if (left_parent == right_parent)
                {
                    if (left_leaf->slotuse <= right_leaf->slotuse)
                        myres |= shift_left_leaf(leaf, right_leaf, right_parent,
                                                 parentslot);
                    else
                        shift_right_leaf(left_leaf, leaf, left_parent,
                                         parentslot - 1);
                }
                else
                {
                    if (left_parent == parent)
                        shift_right_leaf(left_leaf, leaf, left_parent,
                                         parentslot - 1);
                    else
                        myres |= shift_left_leaf(leaf, right_leaf, right_parent,
                                                 parentslot);
                }
            }

            return myres;
        }

        // !curr->is_leafnode()
        InnerNode* inner = static_cast<InnerNode*>(curr);
        InnerNode* left_inner = static_cast<InnerNode*>(left);
        InnerNode* right_inner = static_cast<InnerNode*>(right);

        node *myleft, *myright;
        InnerNode *myleft_parent, *myright_parent;

        unsigned short slot = find_lower(inner, key);

        if (slot == 0)
        {
            myleft =
                (left == nullptr) ?
                    nullptr :
                    static_cast<InnerNode*>(left)->childid[left->slotuse - 1];
            myleft_parent = left_parent;
        }
        else
        {
            myleft = inner->childid[slot - 1];
            myleft_parent = inner;
        }

        if (slot == inner->slotuse)
        {
            myright = (right == nullptr) ?
                          nullptr :
                          static_cast<InnerNode*>(right)->childid[0];
            myright_parent = right_parent;
        }
        else
        {
            myright = inner->childid[slot + 1];
            myright_parent = inner;
        }

        TLX_BTREE_PRINT("erase_one_descend into " << inner->childid[slot]);

        result_t result =
            erase_one_descend(key, inner->childid[slot], myleft, myright,
                              myleft_parent, myright_parent, inner, slot);

        result_t myres = btree_ok;

        if (result.has(btree_not_found))
        {
            return result;
        }

        if (result.has(btree_update_lastkey))
        {
            if (parent && parentslot < parent->slotuse)
            {
                TLX_BTREE_PRINT("Fixing lastkeyupdate: key "
                                << result.lastkey << " into parent " << parent
                                << " at parentslot " << parentslot);

                TLX_BTREE_ASSERT(parent->childid[parentslot] == curr);
                parent->slotkey[parentslot] = result.lastkey;
            }
            else
            {
                TLX_BTREE_PRINT("Forwarding lastkeyupdate: key "
                                << result.lastkey);
                myres |= result_t(btree_update_lastkey, result.lastkey);
            }
        }

        if (result.has(btree_fixmerge))
        {
            // either the current node or the next is empty and should be
            // removed
            if (inner->childid[slot]->slotuse != 0)
                slot++;

            // this is the child slot invalidated by the merge
            TLX_BTREE_ASSERT(inner->childid[slot]->slotuse == 0);

            free_node(inner->childid[slot]);

            std::copy(inner->slotkey + slot, inner->slotkey + inner->slotuse,
                      inner->slotkey + slot - 1);
            std::copy(inner->childid + slot + 1,
                      inner->childid + inner->slotuse + 1,
                      inner->childid + slot);

            inner->slotuse--;

            if (inner->level == 1)
            {
                // fix split key for children leaves
                slot--;
                LeafNode* child = static_cast<LeafNode*>(inner->childid[slot]);
                inner->slotkey[slot] = child->key(child->slotuse - 1);
            }
        }

        if (inner->is_underflow() && !(inner == root_ && inner->slotuse >= 1))
        {
            // case: the inner node is the root and has just one child. that
            // child becomes the new root
            if (left_inner == nullptr && right_inner == nullptr)
            {
                TLX_BTREE_ASSERT(inner == root_);
                TLX_BTREE_ASSERT(inner->slotuse == 0);

                root_ = inner->childid[0];

                inner->slotuse = 0;
                free_node(inner);

                return btree_ok;
            }
            // case : if both left and right leaves would underflow in case
            // of a shift, then merging is necessary. choose the more local
            // merger with our parent
            if ((left_inner == nullptr || left_inner->is_few()) &&
                (right_inner == nullptr || right_inner->is_few()))
            {
                if (left_parent == parent)
                    myres |= merge_inner(left_inner, inner, left_parent,
                                         parentslot - 1);
                else
                    myres |= merge_inner(inner, right_inner, right_parent,
                                         parentslot);
            }
            // case : the right leaf has extra data, so balance right with
            // current
            else if ((left_inner != nullptr && left_inner->is_few()) &&
                     (right_inner != nullptr && !right_inner->is_few()))
            {
                if (right_parent == parent)
                    shift_left_inner(inner, right_inner, right_parent,
                                     parentslot);
                else
                    myres |= merge_inner(left_inner, inner, left_parent,
                                         parentslot - 1);
            }
            // case : the left leaf has extra data, so balance left with
            // current
            else if ((left_inner != nullptr && !left_inner->is_few()) &&
                     (right_inner != nullptr && right_inner->is_few()))
            {
                if (left_parent == parent)
                    shift_right_inner(left_inner, inner, left_parent,
                                      parentslot - 1);
                else
                    myres |= merge_inner(inner, right_inner, right_parent,
                                         parentslot);
            }
            // case : both the leaf and right leaves have extra data and our
            // parent, choose the leaf with more data
            else if (left_parent == right_parent)
            {
                if (left_inner->slotuse <= right_inner->slotuse)
                    shift_left_inner(inner, right_inner, right_parent,
                                     parentslot);
                else
                    shift_right_inner(left_inner, inner, left_parent,
                                      parentslot - 1);
            }
            else
            {
                if (left_parent == parent)
                    shift_right_inner(left_inner, inner, left_parent,
                                      parentslot - 1);
                else
                    shift_left_inner(inner, right_inner, right_parent,
                                     parentslot);
            }
        }

        return myres;
    }

    /*!
     * Erase one key/data pair referenced by an iterator in the B+ tree.
     *
     * Descends down the tree in search of an iterator. During the descent the
     * parent, left and right siblings and their parents are computed and passed
     * down. The difficulty is that the iterator contains only a pointer to a
     * LeafNode, which means that this function must do a recursive depth first
     * search for that leaf node in the subtree containing all pairs of the same
     * key. This subtree can be very large, even the whole tree, though in
     * practice it would not make sense to have so many duplicate keys.
     *
     * Once the referenced key/data pair is found, it is removed from the leaf
     * and the same underflow cases are handled as in erase_one_descend.
     */
    result_t erase_iter_descend(const iterator& iter, node* curr, node* left,
                                node* right, InnerNode* left_parent,
                                InnerNode* right_parent, InnerNode* parent,
                                unsigned int parentslot)
    {
        if (curr->is_leafnode())
        {
            LeafNode* leaf = static_cast<LeafNode*>(curr);
            LeafNode* left_leaf = static_cast<LeafNode*>(left);
            LeafNode* right_leaf = static_cast<LeafNode*>(right);

            // if this is not the correct leaf, get next step in recursive
            // search
            if (leaf != iter.curr_leaf)
            {
                return btree_not_found;
            }

            if (iter.curr_slot >= leaf->slotuse)
            {
                TLX_BTREE_PRINT("Could not find iterator ("
                                << iter.curr_leaf << "," << iter.curr_slot
                                << ") to erase. Invalid leaf node?");

                return btree_not_found;
            }

            unsigned short slot = iter.curr_slot;

            TLX_BTREE_PRINT("Found iterator in leaf " << curr << " at slot "
                                                      << slot);

            std::copy(leaf->slotdata + slot + 1, leaf->slotdata + leaf->slotuse,
                      leaf->slotdata + slot);

            leaf->slotuse--;

            result_t myres = btree_ok;

            // if the last key of the leaf was changed, the parent is notified
            // and updates the key of this leaf
            if (slot == leaf->slotuse)
            {
                if (parent && parentslot < parent->slotuse)
                {
                    TLX_BTREE_ASSERT(parent->childid[parentslot] == curr);
                    parent->slotkey[parentslot] = leaf->key(leaf->slotuse - 1);
                }
                else
                {
                    if (leaf->slotuse >= 1)
                    {
                        TLX_BTREE_PRINT("Scheduling lastkeyupdate: key "
                                        << leaf->key(leaf->slotuse - 1));
                        myres |= result_t(btree_update_lastkey,
                                          leaf->key(leaf->slotuse - 1));
                    }
                    else
                    {
                        TLX_BTREE_ASSERT(leaf == root_);
                    }
                }
            }

            if (leaf->is_underflow() && !(leaf == root_ && leaf->slotuse >= 1))
            {
                // determine what to do about the underflow

                // case : if this empty leaf is the root, then delete all nodes
                // and set root to nullptr.
                if (left_leaf == nullptr && right_leaf == nullptr)
                {
                    TLX_BTREE_ASSERT(leaf == root_);
                    TLX_BTREE_ASSERT(leaf->slotuse == 0);

                    free_node(root_);

                    root_ = leaf = nullptr;
                    head_leaf_ = tail_leaf_ = nullptr;

                    // will be decremented soon by insert_start()
                    TLX_BTREE_ASSERT(stats_.size == 1);
                    TLX_BTREE_ASSERT(stats_.leaves == 0);
                    TLX_BTREE_ASSERT(stats_.inner_nodes == 0);

                    return btree_ok;
                }
                // case : if both left and right leaves would underflow in case
                // of a shift, then merging is necessary. choose the more local
                // merger with our parent
                if ((left_leaf == nullptr || left_leaf->is_few()) &&
                    (right_leaf == nullptr || right_leaf->is_few()))
                {
                    if (left_parent == parent)
                        myres |= merge_leaves(left_leaf, leaf, left_parent);
                    else
                        myres |= merge_leaves(leaf, right_leaf, right_parent);
                }
                // case : the right leaf has extra data, so balance right with
                // current
                else if ((left_leaf != nullptr && left_leaf->is_few()) &&
                         (right_leaf != nullptr && !right_leaf->is_few()))
                {
                    if (right_parent == parent)
                    {
                        myres |= shift_left_leaf(leaf, right_leaf, right_parent,
                                                 parentslot);
                    }
                    else
                    {
                        myres |= merge_leaves(left_leaf, leaf, left_parent);
                    }
                }
                // case : the left leaf has extra data, so balance left with
                // current
                else if ((left_leaf != nullptr && !left_leaf->is_few()) &&
                         (right_leaf != nullptr && right_leaf->is_few()))
                {
                    if (left_parent == parent)
                    {
                        shift_right_leaf(left_leaf, leaf, left_parent,
                                         parentslot - 1);
                    }
                    else
                    {
                        myres |= merge_leaves(leaf, right_leaf, right_parent);
                    }
                }
                // case : both the leaf and right leaves have extra data and our
                // parent, choose the leaf with more data
                else if (left_parent == right_parent)
                {
                    if (left_leaf->slotuse <= right_leaf->slotuse)
                    {
                        myres |= shift_left_leaf(leaf, right_leaf, right_parent,
                                                 parentslot);
                    }
                    else
                    {
                        shift_right_leaf(left_leaf, leaf, left_parent,
                                         parentslot - 1);
                    }
                }
                else
                {
                    if (left_parent == parent)
                    {
                        shift_right_leaf(left_leaf, leaf, left_parent,
                                         parentslot - 1);
                    }
                    else
                    {
                        myres |= shift_left_leaf(leaf, right_leaf, right_parent,
                                                 parentslot);
                    }
                }
            }

            return myres;
        }

        // !curr->is_leafnode()
        InnerNode* inner = static_cast<InnerNode*>(curr);
        InnerNode* left_inner = static_cast<InnerNode*>(left);
        InnerNode* right_inner = static_cast<InnerNode*>(right);

        // find first slot below which the searched iterator might be
        // located.

        result_t result;
        unsigned short slot = find_lower(inner, iter.key());

        while (slot <= inner->slotuse)
        {
            node *myleft, *myright;
            InnerNode *myleft_parent, *myright_parent;

            if (slot == 0)
            {
                myleft = (left == nullptr) ? nullptr :
                                             static_cast<InnerNode*>(left)
                                                 ->childid[left->slotuse - 1];
                myleft_parent = left_parent;
            }
            else
            {
                myleft = inner->childid[slot - 1];
                myleft_parent = inner;
            }

            if (slot == inner->slotuse)
            {
                myright = (right == nullptr) ?
                              nullptr :
                              static_cast<InnerNode*>(right)->childid[0];
                myright_parent = right_parent;
            }
            else
            {
                myright = inner->childid[slot + 1];
                myright_parent = inner;
            }

            TLX_BTREE_PRINT("erase_iter_descend into " << inner->childid[slot]);

            result =
                erase_iter_descend(iter, inner->childid[slot], myleft, myright,
                                   myleft_parent, myright_parent, inner, slot);

            if (!result.has(btree_not_found))
                break;

            // continue recursive search for leaf on next slot

            if (slot < inner->slotuse &&
                key_less(inner->slotkey[slot], iter.key()))
                return btree_not_found;

            ++slot;
        }

            if (slot > inner->slotuse)
                return btree_not_found;

            result_t myres = btree_ok;

            if (result.has(btree_update_lastkey))
            {
                if (parent && parentslot < parent->slotuse)
                {
                    TLX_BTREE_PRINT("Fixing lastkeyupdate: key "
                                    << result.lastkey << " into parent "
                                    << parent << " at parentslot "
                                    << parentslot);

                    TLX_BTREE_ASSERT(parent->childid[parentslot] == curr);
                    parent->slotkey[parentslot] = result.lastkey;
                }
                else
                {
                    TLX_BTREE_PRINT("Forwarding lastkeyupdate: key "
                                    << result.lastkey);
                    myres |= result_t(btree_update_lastkey, result.lastkey);
                }
            }

            if (result.has(btree_fixmerge))
            {
                // either the current node or the next is empty and should be
                // removed
                if (inner->childid[slot]->slotuse != 0)
                    slot++;

                // this is the child slot invalidated by the merge
                TLX_BTREE_ASSERT(inner->childid[slot]->slotuse == 0);

                free_node(inner->childid[slot]);

                std::copy(inner->slotkey + slot,
                          inner->slotkey + inner->slotuse,
                          inner->slotkey + slot - 1);
                std::copy(inner->childid + slot + 1,
                          inner->childid + inner->slotuse + 1,
                          inner->childid + slot);

                inner->slotuse--;

                if (inner->level == 1)
                {
                    // fix split key for children leaves
                    slot--;
                    LeafNode* child =
                        static_cast<LeafNode*>(inner->childid[slot]);
                    inner->slotkey[slot] = child->key(child->slotuse - 1);
                }
            }

            if (inner->is_underflow() &&
                !(inner == root_ && inner->slotuse >= 1))
            {
                // case: the inner node is the root and has just one
                // child. that child becomes the new root
                if (left_inner == nullptr && right_inner == nullptr)
                {
                    TLX_BTREE_ASSERT(inner == root_);
                    TLX_BTREE_ASSERT(inner->slotuse == 0);

                    root_ = inner->childid[0];

                    inner->slotuse = 0;
                    free_node(inner);

                    return btree_ok;
                }
                // case : if both left and right leaves would underflow in case
                // of a shift, then merging is necessary. choose the more local
                // merger with our parent
                if ((left_inner == nullptr || left_inner->is_few()) &&
                    (right_inner == nullptr || right_inner->is_few()))
                {
                    if (left_parent == parent)
                    {
                        myres |= merge_inner(left_inner, inner, left_parent,
                                             parentslot - 1);
                    }
                    else
                    {
                        myres |= merge_inner(inner, right_inner, right_parent,
                                             parentslot);
                    }
                }
                // case : the right leaf has extra data, so balance right with
                // current
                else if ((left_inner != nullptr && left_inner->is_few()) &&
                         (right_inner != nullptr && !right_inner->is_few()))
                {
                    if (right_parent == parent)
                    {
                        shift_left_inner(inner, right_inner, right_parent,
                                         parentslot);
                    }
                    else
                    {
                        myres |= merge_inner(left_inner, inner, left_parent,
                                             parentslot - 1);
                    }
                }
                // case : the left leaf has extra data, so balance left with
                // current
                else if ((left_inner != nullptr && !left_inner->is_few()) &&
                         (right_inner != nullptr && right_inner->is_few()))
                {
                    if (left_parent == parent)
                    {
                        shift_right_inner(left_inner, inner, left_parent,
                                          parentslot - 1);
                    }
                    else
                    {
                        myres |= merge_inner(inner, right_inner, right_parent,
                                             parentslot);
                    }
                }
                // case : both the leaf and right leaves have extra data and our
                // parent, choose the leaf with more data
                else if (left_parent == right_parent)
                {
                    if (left_inner->slotuse <= right_inner->slotuse)
                    {
                        shift_left_inner(inner, right_inner, right_parent,
                                         parentslot);
                    }
                    else
                    {
                        shift_right_inner(left_inner, inner, left_parent,
                                          parentslot - 1);
                    }
                }
                else
                {
                    if (left_parent == parent)
                    {
                        shift_right_inner(left_inner, inner, left_parent,
                                          parentslot - 1);
                    }
                    else
                    {
                        shift_left_inner(inner, right_inner, right_parent,
                                         parentslot);
                    }
                }
            }

            return myres;
    }

    //! Merge two leaf nodes. The function moves all key/data pairs from right
    //! to left and sets right's slotuse to zero. The right slot is then removed
    //! by the calling parent node.
    result_t merge_leaves(LeafNode* left, LeafNode* right, InnerNode* parent)
    {
        TLX_BTREE_PRINT("Merge leaf nodes " << left << " and " << right
                                            << " with common parent " << parent
                                            << ".");
        (void) parent;

        TLX_BTREE_ASSERT(left->is_leafnode() && right->is_leafnode());
        TLX_BTREE_ASSERT(parent->level == 1);

        TLX_BTREE_ASSERT(left->slotuse + right->slotuse < leaf_slotmax);

        std::copy(right->slotdata, right->slotdata + right->slotuse,
                  left->slotdata + left->slotuse);

        left->slotuse += right->slotuse;

        left->next_leaf = right->next_leaf;
        if (left->next_leaf)
            left->next_leaf->prev_leaf = left;
        else
            tail_leaf_ = left;

        right->slotuse = 0;

        return btree_fixmerge;
    }

    //! Merge two inner nodes. The function moves all key/childid pairs from
    //! right to left and sets right's slotuse to zero. The right slot is then
    //! removed by the calling parent node.
    static result_t merge_inner(InnerNode* left, InnerNode* right,
                                InnerNode* parent, unsigned int parentslot)
    {
        TLX_BTREE_PRINT("Merge inner nodes " << left << " and " << right
                                             << " with common parent " << parent
                                             << ".");

        TLX_BTREE_ASSERT(left->level == right->level);
        TLX_BTREE_ASSERT(parent->level == left->level + 1);

        TLX_BTREE_ASSERT(parent->childid[parentslot] == left);

        TLX_BTREE_ASSERT(left->slotuse + right->slotuse < inner_slotmax);

        if (self_verify)
        {
            // find the left node's slot in the parent's children
            unsigned int leftslot = 0;
            while (leftslot <= parent->slotuse &&
                   parent->childid[leftslot] != left)
                ++leftslot;

            TLX_BTREE_ASSERT(leftslot < parent->slotuse);
            TLX_BTREE_ASSERT(parent->childid[leftslot] == left);
            TLX_BTREE_ASSERT(parent->childid[leftslot + 1] == right);

            TLX_BTREE_ASSERT(parentslot == leftslot);
        }

        // retrieve the decision key from parent
        left->slotkey[left->slotuse] = parent->slotkey[parentslot];
        left->slotuse++;

        // copy over keys and children from right
        std::copy(right->slotkey, right->slotkey + right->slotuse,
                  left->slotkey + left->slotuse);
        std::copy(right->childid, right->childid + right->slotuse + 1,
                  left->childid + left->slotuse);

        left->slotuse += right->slotuse;
        right->slotuse = 0;

        return btree_fixmerge;
    }

    //! Balance two leaf nodes. The function moves key/data pairs from right to
    //! left so that both nodes are equally filled. The parent node is updated
    //! if possible.
    static result_t shift_left_leaf(LeafNode* left, LeafNode* right,
                                    InnerNode* parent, unsigned int parentslot)
    {
        TLX_BTREE_ASSERT(left->is_leafnode() && right->is_leafnode());
        TLX_BTREE_ASSERT(parent->level == 1);

        TLX_BTREE_ASSERT(left->next_leaf == right);
        TLX_BTREE_ASSERT(left == right->prev_leaf);

        TLX_BTREE_ASSERT(left->slotuse < right->slotuse);
        TLX_BTREE_ASSERT(parent->childid[parentslot] == left);

        unsigned int shiftnum = (right->slotuse - left->slotuse) >> 1;

        TLX_BTREE_PRINT("Shifting (leaf) " << shiftnum << " entries to left "
                                           << left << " from right " << right
                                           << " with common parent " << parent
                                           << ".");

        TLX_BTREE_ASSERT(left->slotuse + shiftnum < leaf_slotmax);

        // copy the first items from the right node to the last slot in the left
        // node.

        std::copy(right->slotdata, right->slotdata + shiftnum,
                  left->slotdata + left->slotuse);

        left->slotuse += shiftnum;

        // shift all slots in the right node to the left

        std::copy(right->slotdata + shiftnum, right->slotdata + right->slotuse,
                  right->slotdata);

        right->slotuse -= shiftnum;

        // fixup parent
        if (parentslot < parent->slotuse)
        {
            parent->slotkey[parentslot] = left->key(left->slotuse - 1);
            return btree_ok;
        }

        // the update is further up the tree
        return result_t(btree_update_lastkey, left->key(left->slotuse - 1));
    }

    //! Balance two inner nodes. The function moves key/data pairs from right to
    //! left so that both nodes are equally filled. The parent node is updated
    //! if possible.
    static void shift_left_inner(InnerNode* left, InnerNode* right,
                                 InnerNode* parent, unsigned int parentslot)
    {
        TLX_BTREE_ASSERT(left->level == right->level);
        TLX_BTREE_ASSERT(parent->level == left->level + 1);

        TLX_BTREE_ASSERT(left->slotuse < right->slotuse);
        TLX_BTREE_ASSERT(parent->childid[parentslot] == left);

        unsigned int shiftnum = (right->slotuse - left->slotuse) >> 1;

        TLX_BTREE_PRINT("Shifting (inner) " << shiftnum << " entries to left "
                                            << left << " from right " << right
                                            << " with common parent " << parent
                                            << ".");

        TLX_BTREE_ASSERT(left->slotuse + shiftnum < inner_slotmax);

        if (self_verify)
        {
            // find the left node's slot in the parent's children and compare to
            // parentslot

            unsigned int leftslot = 0;
            while (leftslot <= parent->slotuse &&
                   parent->childid[leftslot] != left)
                ++leftslot;

            TLX_BTREE_ASSERT(leftslot < parent->slotuse);
            TLX_BTREE_ASSERT(parent->childid[leftslot] == left);
            TLX_BTREE_ASSERT(parent->childid[leftslot + 1] == right);

            TLX_BTREE_ASSERT(leftslot == parentslot);
        }

        // copy the parent's decision slotkey and childid to the first new key
        // on the left
        left->slotkey[left->slotuse] = parent->slotkey[parentslot];
        left->slotuse++;

        // copy the other items from the right node to the last slots in the
        // left node.
        std::copy(right->slotkey, right->slotkey + shiftnum - 1,
                  left->slotkey + left->slotuse);
        std::copy(right->childid, right->childid + shiftnum,
                  left->childid + left->slotuse);

        left->slotuse += shiftnum - 1;

        // fixup parent
        parent->slotkey[parentslot] = right->slotkey[shiftnum - 1];

        // shift all slots in the right node
        std::copy(right->slotkey + shiftnum, right->slotkey + right->slotuse,
                  right->slotkey);
        std::copy(right->childid + shiftnum,
                  right->childid + right->slotuse + 1, right->childid);

        right->slotuse -= shiftnum;
    }

    //! Balance two leaf nodes. The function moves key/data pairs from left to
    //! right so that both nodes are equally filled. The parent node is updated
    //! if possible.
    static void shift_right_leaf(LeafNode* left, LeafNode* right,
                                 InnerNode* parent, unsigned int parentslot)
    {
        TLX_BTREE_ASSERT(left->is_leafnode() && right->is_leafnode());
        TLX_BTREE_ASSERT(parent->level == 1);

        TLX_BTREE_ASSERT(left->next_leaf == right);
        TLX_BTREE_ASSERT(left == right->prev_leaf);
        TLX_BTREE_ASSERT(parent->childid[parentslot] == left);

        TLX_BTREE_ASSERT(left->slotuse > right->slotuse);

        unsigned int shiftnum = (left->slotuse - right->slotuse) >> 1;

        TLX_BTREE_PRINT("Shifting (leaf) " << shiftnum << " entries to right "
                                           << right << " from left " << left
                                           << " with common parent " << parent
                                           << ".");

        if (self_verify)
        {
            // find the left node's slot in the parent's children
            unsigned int leftslot = 0;
            while (leftslot <= parent->slotuse &&
                   parent->childid[leftslot] != left)
                ++leftslot;

            TLX_BTREE_ASSERT(leftslot < parent->slotuse);
            TLX_BTREE_ASSERT(parent->childid[leftslot] == left);
            TLX_BTREE_ASSERT(parent->childid[leftslot + 1] == right);

            TLX_BTREE_ASSERT(leftslot == parentslot);
        }

        // shift all slots in the right node

        TLX_BTREE_ASSERT(right->slotuse + shiftnum < leaf_slotmax);

        std::copy_backward(right->slotdata, right->slotdata + right->slotuse,
                           right->slotdata + right->slotuse + shiftnum);

        right->slotuse += shiftnum;

        // copy the last items from the left node to the first slot in the right
        // node.
        std::copy(left->slotdata + left->slotuse - shiftnum,
                  left->slotdata + left->slotuse, right->slotdata);

        left->slotuse -= shiftnum;

        parent->slotkey[parentslot] = left->key(left->slotuse - 1);
    }

    //! Balance two inner nodes. The function moves key/data pairs from left to
    //! right so that both nodes are equally filled. The parent node is updated
    //! if possible.
    static void shift_right_inner(InnerNode* left, InnerNode* right,
                                  InnerNode* parent, unsigned int parentslot)
    {
        TLX_BTREE_ASSERT(left->level == right->level);
        TLX_BTREE_ASSERT(parent->level == left->level + 1);

        TLX_BTREE_ASSERT(left->slotuse > right->slotuse);
        TLX_BTREE_ASSERT(parent->childid[parentslot] == left);

        unsigned int shiftnum = (left->slotuse - right->slotuse) >> 1;

        TLX_BTREE_PRINT("Shifting (leaf) " << shiftnum << " entries to right "
                                           << right << " from left " << left
                                           << " with common parent " << parent
                                           << ".");

        if (self_verify)
        {
            // find the left node's slot in the parent's children
            unsigned int leftslot = 0;
            while (leftslot <= parent->slotuse &&
                   parent->childid[leftslot] != left)
                ++leftslot;

            TLX_BTREE_ASSERT(leftslot < parent->slotuse);
            TLX_BTREE_ASSERT(parent->childid[leftslot] == left);
            TLX_BTREE_ASSERT(parent->childid[leftslot + 1] == right);

            TLX_BTREE_ASSERT(leftslot == parentslot);
        }

        // shift all slots in the right node

        TLX_BTREE_ASSERT(right->slotuse + shiftnum < inner_slotmax);

        std::copy_backward(right->slotkey, right->slotkey + right->slotuse,
                           right->slotkey + right->slotuse + shiftnum);
        std::copy_backward(right->childid, right->childid + right->slotuse + 1,
                           right->childid + right->slotuse + 1 + shiftnum);

        right->slotuse += shiftnum;

        // copy the parent's decision slotkey and childid to the last new key on
        // the right
        right->slotkey[shiftnum - 1] = parent->slotkey[parentslot];

        // copy the remaining last items from the left node to the first slot in
        // the right node.
        std::copy(left->slotkey + left->slotuse - shiftnum + 1,
                  left->slotkey + left->slotuse, right->slotkey);
        std::copy(left->childid + left->slotuse - shiftnum + 1,
                  left->childid + left->slotuse + 1, right->childid);

        // copy the first to-be-removed key from the left node to the parent's
        // decision slot
        parent->slotkey[parentslot] = left->slotkey[left->slotuse - shiftnum];

        left->slotuse -= shiftnum;
    }

    //! \}

#ifdef TLX_BTREE_DEBUG

public:
    //! \name Debug Printing
    //! \{

    //! Print out the B+ tree structure with keys onto the given ostream. This
    //! function requires that the header is compiled with TLX_BTREE_DEBUG and
    //! that key_type is printable via std::ostream.
    void print(std::ostream& os) const
    {
        if (root_)
        {
            print_node(os, root_, 0, true);
        }
    }

    //! Print out only the leaves via the double linked list.
    void print_leaves(std::ostream& os) const
    {
        os << "leaves:" << std::endl;

        const LeafNode* n = head_leaf_;

        while (n)
        {
            os << "  " << n << std::endl;

            n = n->next_leaf;
        }
    }

private:
    //! Recursively descend down the tree and print out nodes.
    static void print_node(std::ostream& os, const node* node,
                           unsigned int depth = 0, bool recursive = false)
    {
        for (unsigned int i = 0; i < depth; i++)
            os << "  ";

        os << "node " << node << " level " << node->level << " slotuse "
           << node->slotuse << std::endl;

        if (node->is_leafnode())
        {
            const LeafNode* leafnode = static_cast<const LeafNode*>(node);

            for (unsigned int i = 0; i < depth; i++)
                os << "  ";
            os << "  leaf prev " << leafnode->prev_leaf << " next "
               << leafnode->next_leaf << std::endl;

            for (unsigned int i = 0; i < depth; i++)
                os << "  ";

            for (unsigned short slot = 0; slot < leafnode->slotuse; ++slot)
            {
                // os << leafnode->key(slot) << " "
                //    << "(data: " << leafnode->slotdata[slot] << ") ";
                os << leafnode->key(slot) << "  ";
            }
            os << std::endl;
        }
        else
        {
            const InnerNode* innernode = static_cast<const InnerNode*>(node);

            for (unsigned int i = 0; i < depth; i++)
                os << "  ";

            for (unsigned short slot = 0; slot < innernode->slotuse; ++slot)
            {
                os << "(" << innernode->childid[slot] << ") "
                   << innernode->slotkey[slot] << " ";
            }
            os << "(" << innernode->childid[innernode->slotuse] << ")"
               << std::endl;

            if (recursive)
            {
                for (unsigned short slot = 0; slot < innernode->slotuse + 1;
                     ++slot)
                {
                    print_node(os, innernode->childid[slot], depth + 1,
                               recursive);
                }
            }
        }
    }

    //! \}
#endif

public:
    //! \name Verification of B+ Tree Invariants
    //! \{

    //! Run a thorough verification of all B+ tree invariants. The program
    //! aborts via tlx_die_unless() if something is wrong.
    void verify() const
    {
        key_type minkey, maxkey;
        tree_stats vstats;

        if (root_)
        {
            verify_node(root_, &minkey, &maxkey, vstats);

            tlx_die_unless(vstats.size == stats_.size);
            tlx_die_unless(vstats.leaves == stats_.leaves);
            tlx_die_unless(vstats.inner_nodes == stats_.inner_nodes);

            verify_leaflinks();
        }
    }

private:
    //! Recursively descend down the tree and verify each node
    void verify_node(const node* n, key_type* minkey, key_type* maxkey,
                     tree_stats& vstats) const
    {
        TLX_BTREE_PRINT("verifynode " << n);

        if (n->is_leafnode())
        {
            const LeafNode* leaf = static_cast<const LeafNode*>(n);

            tlx_die_unless(leaf == root_ || !leaf->is_underflow());
            tlx_die_unless(leaf->slotuse > 0);

            for (unsigned short slot = 0; slot < leaf->slotuse - 1; ++slot)
            {
                tlx_die_unless(
                    key_lessequal(leaf->key(slot), leaf->key(slot + 1)));
            }

            *minkey = leaf->key(0);
            *maxkey = leaf->key(leaf->slotuse - 1);

            vstats.leaves++;
            vstats.size += leaf->slotuse;
        }
        else // !n->is_leafnode()
        {
            const InnerNode* inner = static_cast<const InnerNode*>(n);
            vstats.inner_nodes++;

            tlx_die_unless(inner == root_ || !inner->is_underflow());
            tlx_die_unless(inner->slotuse > 0);

            for (unsigned short slot = 0; slot < inner->slotuse - 1; ++slot)
            {
                tlx_die_unless(
                    key_lessequal(inner->key(slot), inner->key(slot + 1)));
            }

            for (unsigned short slot = 0; slot <= inner->slotuse; ++slot)
            {
                const node* subnode = inner->childid[slot];
                key_type subminkey = key_type();
                key_type submaxkey = key_type();

                tlx_die_unless(subnode->level + 1 == inner->level);
                verify_node(subnode, &subminkey, &submaxkey, vstats);

                TLX_BTREE_PRINT("verify subnode " << subnode << ": "
                                                  << subminkey << " - "
                                                  << submaxkey);

                if (slot == 0)
                    *minkey = subminkey;
                else
                    tlx_die_unless(
                        key_greaterequal(subminkey, inner->key(slot - 1)));

                if (slot == inner->slotuse)
                    *maxkey = submaxkey;
                else
                    tlx_die_unless(key_equal(inner->key(slot), submaxkey));

                if (inner->level == 1 && slot < inner->slotuse)
                {
                    // children are leaves and must be linked together in the
                    // correct order
                    const LeafNode* leafa =
                        static_cast<const LeafNode*>(inner->childid[slot]);
                    const LeafNode* leafb =
                        static_cast<const LeafNode*>(inner->childid[slot + 1]);

                    tlx_die_unless(leafa->next_leaf == leafb);
                    tlx_die_unless(leafa == leafb->prev_leaf);
                }
                if (inner->level == 2 && slot < inner->slotuse)
                {
                    // verify leaf links between the adjacent inner nodes
                    const InnerNode* parenta =
                        static_cast<const InnerNode*>(inner->childid[slot]);
                    const InnerNode* parentb =
                        static_cast<const InnerNode*>(inner->childid[slot + 1]);

                    const LeafNode* leafa = static_cast<const LeafNode*>(
                        parenta->childid[parenta->slotuse]);
                    const LeafNode* leafb =
                        static_cast<const LeafNode*>(parentb->childid[0]);

                    tlx_die_unless(leafa->next_leaf == leafb);
                    tlx_die_unless(leafa == leafb->prev_leaf);
                }
            }
        }
    }

    //! Verify the double linked list of leaves.
    void verify_leaflinks() const
    {
        const LeafNode* n = head_leaf_;

        tlx_die_unless(n->level == 0);
        tlx_die_unless(!n || n->prev_leaf == nullptr);

        unsigned int testcount = 0;

        while (n)
        {
            tlx_die_unless(n->level == 0);
            tlx_die_unless(n->slotuse > 0);

            for (unsigned short slot = 0; slot < n->slotuse - 1; ++slot)
            {
                tlx_die_unless(key_lessequal(n->key(slot), n->key(slot + 1)));
            }

            testcount += n->slotuse;

            if (n->next_leaf)
            {
                tlx_die_unless(key_lessequal(n->key(n->slotuse - 1),
                                             n->next_leaf->key(0)));

                tlx_die_unless(n == n->next_leaf->prev_leaf);
            }
            else
            {
                tlx_die_unless(tail_leaf_ == n);
            }

            n = n->next_leaf;
        }

        tlx_die_unless(testcount == size());
    }

    //! \}
};

//! \}
//! \}

} // namespace tlx

#endif // !TLX_CONTAINER_BTREE_HEADER

/******************************************************************************/
