/*******************************************************************************
 * tlx/container/btree_multiset.hpp
 *
 * Part of tlx - http://panthema.net/tlx
 *
 * Copyright (C) 2008-2017 Timo Bingmann <tb@panthema.net>
 *
 * All rights reserved. Published under the Boost Software License, Version 1.0
 ******************************************************************************/

#ifndef TLX_CONTAINER_BTREE_MULTISET_HEADER
#define TLX_CONTAINER_BTREE_MULTISET_HEADER

#include <tlx/container/btree.hpp>
#include <functional>
#include <memory>
#include <utility>

namespace tlx {

//! \addtogroup tlx_container_btree
//! \{

/*!
 * Specialized B+ tree template class implementing STL's multiset container.
 *
 * Implements the STL multiset using a B+ tree. It can be used as a drop-in
 * replacement for std::multiset. Not all asymptotic time requirements are met
 * in theory. The class has a traits class defining B+ tree properties like
 * slots and self-verification. Furthermore an allocator can be specified for
 * tree nodes.
 *
 * It is somewhat inefficient to implement a multiset using a B+ tree, a plain B
 * tree would hold fewer copies of the keys.
 */
template <typename Key_, typename Compare_ = std::less<Key_>,
          typename Traits_ = btree_default_traits<Key_, Key_>,
          typename Alloc_ = std::allocator<Key_> >
class btree_multiset
{
public:
    //! \name Template Parameter Types
    //! \{

    //! First template parameter: The key type of the btree. This is stored in
    //! inner nodes and leaves.
    typedef Key_ key_type;

    //! Second template parameter: Key comparison function object
    typedef Compare_ key_compare;

    //! Third template parameter: Traits object used to define more parameters
    //! of the B+ tree
    typedef Traits_ traits;

    //! Fourth template parameter: STL allocator
    typedef Alloc_ allocator_type;

    //! \}

    // The macro TLX_BTREE_FRIENDS can be used by outside class to access the B+
    // tree internals. This was added for wxBTreeDemo to be able to draw the
    // tree.
    TLX_BTREE_FRIENDS;

public:
    //! \name Constructed Types
    //! \{

    //! Construct the set value_type: the key_type.
    typedef key_type value_type;

    //! Typedef of our own type
    typedef btree_multiset<key_type, key_compare, traits, allocator_type> self;

    //! Key Extractor Struct
    struct key_of_value
    {
        //! pull first out of pair
        static const key_type& get(const value_type& v)
        {
            return v;
        }
    };

    //! Implementation type of the btree_base
    typedef BTree<key_type, value_type, key_of_value, key_compare, traits, true,
                  allocator_type>
        btree_impl;

    //! Function class comparing two value_type keys.
    typedef typename btree_impl::value_compare value_compare;

    //! Size type used to count keys
    typedef typename btree_impl::size_type size_type;

    //! Small structure containing statistics about the tree
    typedef typename btree_impl::tree_stats tree_stats;

    //! \}

public:
    //! \name Static Constant Options and Values of the B+ Tree
    //! \{

    //! Base B+ tree parameter: The number of key/data slots in each leaf
    static const unsigned short leaf_slotmax = btree_impl::leaf_slotmax;

    //! Base B+ tree parameter: The number of key slots in each inner node,
    //! this can differ from slots in each leaf.
    static const unsigned short inner_slotmax = btree_impl::inner_slotmax;

    //! Computed B+ tree parameter: The minimum number of key slots used in a
    //! leaf. If fewer slots are used, the leaf will be merged or slots shifted
    //! from it's siblings.
    static const unsigned short leaf_slotmin = btree_impl::leaf_slotmin;

    //! Computed B+ tree parameter: The minimum number of key slots used
    //! in an inner node. If fewer slots are used, the inner node will be
    //! merged or slots shifted from it's siblings.
    static const unsigned short inner_slotmin = btree_impl::inner_slotmin;

    //! Debug parameter: Enables expensive and thorough checking of the B+ tree
    //! invariants after each insert/erase operation.
    static const bool self_verify = btree_impl::self_verify;

    //! Debug parameter: Prints out lots of debug information about how the
    //! algorithms change the tree. Requires the header file to be compiled with
    //! TLX_BTREE_DEBUG and the key type must be std::ostream printable.
    static const bool debug = btree_impl::debug;

    //! Operational parameter: Allow duplicate keys in the btree.
    static const bool allow_duplicates = btree_impl::allow_duplicates;

    //! \}

public:
    //! \name Iterators and Reverse Iterators
    //! \{

    //! STL-like iterator object for B+ tree items. The iterator points to a
    //! specific slot number in a leaf.
    typedef typename btree_impl::iterator iterator;

    //! STL-like iterator object for B+ tree items. The iterator points to a
    //! specific slot number in a leaf.
    typedef typename btree_impl::const_iterator const_iterator;

    //! create mutable reverse iterator by using STL magic
    typedef typename btree_impl::reverse_iterator reverse_iterator;

    //! create constant reverse iterator by using STL magic
    typedef typename btree_impl::const_reverse_iterator const_reverse_iterator;

    //! \}

private:
    //! \name Tree Implementation Object
    //! \{

    //! The contained implementation object
    btree_impl tree_;

    //! \}

public:
    //! \name Constructors and Destructor
    //! \{

    //! Default constructor initializing an empty B+ tree with the standard key
    //! comparison function
    explicit btree_multiset(const allocator_type& alloc = allocator_type())
        : tree_(alloc)
    {
    }

    //! Constructor initializing an empty B+ tree with a special key
    //! comparison object
    explicit btree_multiset(const key_compare& kcf,
                            const allocator_type& alloc = allocator_type())
        : tree_(kcf, alloc)
    {
    }

    //! Constructor initializing a B+ tree with the range [first,last)
    template <class InputIterator>
    btree_multiset(InputIterator first, InputIterator last,
                   const allocator_type& alloc = allocator_type())
        : tree_(alloc)
    {
        insert(first, last);
    }

    //! Constructor initializing a B+ tree with the range [first,last) and a
    //! special key comparison object
    template <class InputIterator>
    btree_multiset(InputIterator first, InputIterator last,
                   const key_compare& kcf,
                   const allocator_type& alloc = allocator_type())
        : tree_(kcf, alloc)
    {
        insert(first, last);
    }

    //! Frees up all used B+ tree memory pages
    ~btree_multiset()
    {
    }

    //! Fast swapping of two identical B+ tree objects.
    void swap(btree_multiset& from) noexcept
    {
        std::swap(tree_, from.tree_);
    }

    //! \}

public:
    //! \name Key and Value Comparison Function Objects
    //! \{

    //! Constant access to the key comparison object sorting the B+ tree
    key_compare key_comp() const
    {
        return tree_.key_comp();
    }

    //! Constant access to a constructed value_type comparison object. Required
    //! by the STL
    value_compare value_comp() const
    {
        return tree_.value_comp();
    }

    //! \}

public:
    //! \name Allocators
    //! \{

    //! Return the base node allocator provided during construction.
    allocator_type get_allocator() const
    {
        return tree_.get_allocator();
    }

    //! \}

public:
    //! \name Fast Destruction of the B+ Tree
    //! \{

    //! Frees all keys and all nodes of the tree
    void clear()
    {
        tree_.clear();
    }

    //! \}

public:
    //! \name STL Iterator Construction Functions
    //! \{

    //! Constructs a read/data-write iterator that points to the first slot in
    //! the first leaf of the B+ tree.
    iterator begin()
    {
        return tree_.begin();
    }

    //! Constructs a read/data-write iterator that points to the first invalid
    //! slot in the last leaf of the B+ tree.
    iterator end()
    {
        return tree_.end();
    }

    //! Constructs a read-only constant iterator that points to the first slot
    //! in the first leaf of the B+ tree.
    const_iterator begin() const
    {
        return tree_.begin();
    }

    //! Constructs a read-only constant iterator that points to the first
    //! invalid slot in the last leaf of the B+ tree.
    const_iterator end() const
    {
        return tree_.end();
    }

    //! Constructs a read/data-write reverse iterator that points to the first
    //! invalid slot in the last leaf of the B+ tree. Uses STL magic.
    reverse_iterator rbegin()
    {
        return tree_.rbegin();
    }

    //! Constructs a read/data-write reverse iterator that points to the first
    //! slot in the first leaf of the B+ tree. Uses STL magic.
    reverse_iterator rend()
    {
        return tree_.rend();
    }

    //! Constructs a read-only reverse iterator that points to the first
    //! invalid slot in the last leaf of the B+ tree. Uses STL magic.
    const_reverse_iterator rbegin() const
    {
        return tree_.rbegin();
    }

    //! Constructs a read-only reverse iterator that points to the first slot
    //! in the first leaf of the B+ tree. Uses STL magic.
    const_reverse_iterator rend() const
    {
        return tree_.rend();
    }

    //! \}

public:
    //! \name Access Functions to the Item Count
    //! \{

    //! Return the number of keys in the B+ tree
    size_type size() const
    {
        return tree_.size();
    }

    //! Returns true if there is at least one key in the B+ tree
    bool empty() const
    {
        return tree_.empty();
    }

    //! Returns the largest possible size of the B+ Tree. This is just a
    //! function required by the STL standard, the B+ Tree can hold more items.
    size_type max_size() const
    {
        return tree_.max_size();
    }

    //! Return a const reference to the current statistics.
    const tree_stats& get_stats() const
    {
        return tree_.get_stats();
    }

    //! \}

public:
    //! \name STL Access Functions Querying the Tree by Descending to a Leaf
    //! \{

    //! Non-STL function checking whether a key is in the B+ tree. The same as
    //! (find(k) != end()) or (count() != 0).
    bool exists(const key_type& key) const
    {
        return tree_.exists(key);
    }

    //! Tries to locate a key in the B+ tree and returns an iterator to the key
    //! slot if found. If unsuccessful it returns end().
    iterator find(const key_type& key)
    {
        return tree_.find(key);
    }

    //! Tries to locate a key in the B+ tree and returns an constant iterator to
    //! the key slot if found. If unsuccessful it returns end().
    const_iterator find(const key_type& key) const
    {
        return tree_.find(key);
    }

    //! Tries to locate a key in the B+ tree and returns the number of identical
    //! key entries found.
    size_type count(const key_type& key) const
    {
        return tree_.count(key);
    }

    //! Searches the B+ tree and returns an iterator to the first pair equal to
    //! or greater than key, or end() if all keys are smaller.
    iterator lower_bound(const key_type& key)
    {
        return tree_.lower_bound(key);
    }

    //! Searches the B+ tree and returns a constant iterator to the first pair
    //! equal to or greater than key, or end() if all keys are smaller.
    const_iterator lower_bound(const key_type& key) const
    {
        return tree_.lower_bound(key);
    }

    //! Searches the B+ tree and returns an iterator to the first pair greater
    //! than key, or end() if all keys are smaller or equal.
    iterator upper_bound(const key_type& key)
    {
        return tree_.upper_bound(key);
    }

    //! Searches the B+ tree and returns a constant iterator to the first pair
    //! greater than key, or end() if all keys are smaller or equal.
    const_iterator upper_bound(const key_type& key) const
    {
        return tree_.upper_bound(key);
    }

    //! Searches the B+ tree and returns both lower_bound() and upper_bound().
    std::pair<iterator, iterator> equal_range(const key_type& key)
    {
        return tree_.equal_range(key);
    }

    //! Searches the B+ tree and returns both lower_bound() and upper_bound().
    std::pair<const_iterator, const_iterator> equal_range(
        const key_type& key) const
    {
        return tree_.equal_range(key);
    }

    //! \}

public:
    //! \name B+ Tree Object Comparison Functions
    //! \{

    //! Equality relation of B+ trees of the same type. B+ trees of the same
    //! size and equal key (counts) are considered equal.
    bool operator==(const btree_multiset& other) const
    {
        return (tree_ == other.tree_);
    }

    //! Inequality relation. Based on operator==.
    bool operator!=(const btree_multiset& other) const
    {
        return (tree_ != other.tree_);
    }

    //! Total ordering relation of B+ trees of the same type. It uses
    //! std::lexicographical_compare() for the actual comparison of elements.
    bool operator<(const btree_multiset& other) const
    {
        return (tree_ < other.tree_);
    }

    //! Greater relation. Based on operator<.
    bool operator>(const btree_multiset& other) const
    {
        return (tree_ > other.tree_);
    }

    //! Less-equal relation. Based on operator<.
    bool operator<=(const btree_multiset& other) const
    {
        return (tree_ <= other.tree_);
    }

    //! Greater-equal relation. Based on operator<.
    bool operator>=(const btree_multiset& other) const
    {
        return (tree_ >= other.tree_);
    }

    //! \}

public:
    //! \name Fast Copy: Assign Operator and Copy Constructors
    //! \{

    //! Assignment operator. All the keys are copied
    btree_multiset& operator=(const btree_multiset& other)
    {
        if (this != &other)
            tree_ = other.tree_;
        return *this;
    }

    //! Copy constructor. The newly initialized B+ tree object will contain a
    //! copy of all keys.
    btree_multiset(const btree_multiset& other) : tree_(other.tree_)
    {
    }

    //! \}

public:
    //! \name Public Insertion Functions
    //! \{

    //! Attempt to insert a key into the B+ tree. As this set allows duplicates,
    //! this function never fails.
    iterator insert(const key_type& x)
    {
        return tree_.insert(x).first;
    }

    //! Attempt to insert a key into the B+ tree. The iterator hint is currently
    //! ignored by the B+ tree insertion routine.
    iterator insert(iterator hint, const key_type& x)
    {
        return tree_.insert(hint, x);
    }

    //! Attempt to insert the range [first,last) of key_type into the B+
    //! tree. Each key is inserted individually.
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

    //! Bulk load a sorted range [first,last). Loads items into leaves and
    //! constructs a B-tree above them. The tree must be empty when calling this
    //! function.
    template <typename Iterator>
    void bulk_load(Iterator first, Iterator last)
    {
        return tree_.bulk_load(first, last);
    }

    //! \}

public:
    //! \name Public Erase Functions
    //! \{

    //! Erases one (the first) entry of the given key.
    bool erase_one(const key_type& key)
    {
        return tree_.erase_one(key);
    }

    //! Erases all the entries of the given key. This is implemented using
    //! erase_one() and thus not very efficient.
    size_type erase(const key_type& key)
    {
        return tree_.erase(key);
    }

    //! Erase the key/data pair referenced by the iterator.
    void erase(iterator iter)
    {
        return tree_.erase(iter);
    }

#ifdef TLX_BTREE_TODO
    //! Erase all keys in the range [first,last). This function is currently
    //! not implemented by the B+ Tree.
    void erase(iterator /* first */, iterator /* last */)
    {
        abort();
    }
#endif

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
        tree_.print(os);
    }

    //! Print out only the leaves via the double linked list.
    void print_leaves(std::ostream& os) const
    {
        tree_.print_leaves(os);
    }

    //! \}
#endif

public:
    //! \name Verification of B+ Tree Invariants
    //! \{

    //! Run a thorough verification of all B+ tree invariants. The program
    //! aborts via TLX_BTREE_ASSERT() if something is wrong.
    void verify() const
    {
        tree_.verify();
    }

    //! \}
};

//! \}

} // namespace tlx

#endif // !TLX_CONTAINER_BTREE_MULTISET_HEADER

/******************************************************************************/
