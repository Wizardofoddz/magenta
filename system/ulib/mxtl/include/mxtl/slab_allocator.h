// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <magenta/compiler.h>
#include <mxtl/algorithm.h>
#include <mxtl/auto_lock.h>
#include <mxtl/deleter.h>
#include <mxtl/intrusive_single_list.h>
#include <mxtl/mutex.h>
#include <mxtl/null_lock.h>
#include <mxtl/ref_ptr.h>
#include <mxtl/type_support.h>
#include <mxtl/unique_ptr.h>
#include <stdlib.h>

// Usage Notes:
//
// mxtl::SlabAllocator<> is a utility class which implements a slab-style
// allocator for a given object type.  It can be used to dispense either managed
// or unmanaged pointer types.  Managed pointers automatically return to the
// allocator when they go completely out of scope, while unmanaged pointers must
// be manually returned to the allocator they came from.  Allocators may be
// "static" (meaning that there is only one allocator for the type for the
// entire process), or "instanced" meaning that there may be multiple instances
// of the allocator for the type, each with independent quotas.
//
// :: SlabAllocatorTraits<> ::
// The properties and behavior of a type of slab allocator is controlled using
// the SlabAllocatorTraits<> struct.  Things which can be controlled include...
//
// ++ The type of object and pointer created by the allocator.
// ++ The size of the slabs of memory which get allocated.
// ++ The synchronization primitive used to achieve thread safety.
// ++ The static/instanced nature of the allocator.
//
// Details on each of these items are included in the sections below.
//
// :: Memory limits and allocation behavior ::
//
// Slab allocators allocate large regions of memory (slabs) and carve them into
// properly aligned regions just large enough to hold an instance of the
// allocator's object type.  Internally, the allocator maintains a list of the
// slabs it has allocated as well as a free list of currently unused blocks of
// object memory.
//
// When allocations are performed...
// 1) Objects from the free list are used first.
// 2) If the free list is empty and the currently active slab has not been
//    completely used, a block of object memory is taken from the currently
//    active slab.
// 3) If the currently active slab has no more space, and the slab allocation
//    limit has not been reached, a new slab will be allocated using malloc and
//    single block of object memory will be carved out of it.
// 4) If all of the above fail, the allocation fails an nullptr is returned.
//
// Typically, allocation operations are O(1), but occasionally will be O(malloc)
// if a new slab needs to be allocated.  Free operations are always O(1).
//
// Slab size is determined by the SLAB_SIZE parameter of the
// SlabAllocatorTraits<> struct and  defaults to 16KB.  The maximum number of
// slabs the allocator is allow to create is determined at construction time.
// Additionally, an optional bool (default == false) may be passed to the
// constructor telling it to attempt to allocate at least one slab up front.
// Setting the slab limit to 1 and pre-allocating that slab during construction
// will ensure O(1) for all allocations.
//
// :: Thread Safety ::
//
// By default, SlabAllocators use a mxtl::Mutex is used internally to ensure
// that allocation and free operations are thread safe.  The only external
// function called while holding the internal mutex is ::malloc.
//
// This behavior may be changed by changing the LockType template parameter of
// the SlabAllocatorTraits<> struct to be the name of the class which will
// implement the locking behavior.  The class chosen must be compatible with
// mxtl::AutoLock.  mxtl::NullLock may be used if no locking is what is wanted.
// UnlockedInstancedSlabAllocatorTraits or UnlockedStaticSlabAllocatorTraits may
// be used as a shorthand for this.
//
// ** Example **
//
// using MyAllocatorTraits =
//     mxtl::SlabAllocatorTraits<mxtl::unique_ptr<MyObject>,
//                               mxtl::DEFAULT_SLAB_ALLOCATOR_SLAB_SIZE,
//                               mxtl::NullLock,
//                               true>;
// mxtl::SlabAllocator<MyAllocatorTraits> allocator;
//
// or...
//
// mxtl::SlabAllocator<UnlockedStaticSlabAllocator<mxtl::unique_ptr<MyObject>> allocator;
//
// :: Object Requirements ::
//
// Objects must be small enough that at least 1 can be allocated from a slab
// after taking alignment and internal slab bookkeeping into account.  If the
// object is too large (unlikely) the slab size must be increased.  This
// requirement is enforced with a static_assert, so any problems here should be
// caught at compile time.  MyAllocatorType::AllocsPerSlab is a constexpr which
// reports the number of allocations the compiler was able to carve out of each
// slab.
//
// All objects must derive from SlabAllocated<T> where T are the same
// SlabAllocatorTraits<> used to create the SlabAllocator itself.
//
// Deriving from SlabAllocated<> automatically provides the custom deletion
// behavior which allows the pointer to automatically return to the proper
// allocator when delete is called on the pointer (in the case of unmanaged
// pointers) or when the pointer goes completely out of scope (in the case of
// managed pointers).
//
// In the case of instanced slab allocators, the SlabAllocated<> class also
// provides storage for the pointer which will be used for the allocation to
// find its way back to its originating allocator.
//
// In the case of static slab allocators, the SlabAllocated<> class introduces
// no storage overhead to the object, it just supplies the type information
// needed for the object to automatically return to its allocator.
//
// :: Static Allocator Storage ::
//
// Static slab allocators require that the storage required for the allocator to
// function be declared somewhere in the program.
//
// Given the precondition...
//
// class MyObject;
// using SATraits = mxtl::StaticAllocatorTraits<mxtl::unique_ptr<MyObject>>;
//
// The formal syntax for declaring the allocator storage would be...
//
// template<>
// typename mxtl::SlabAllocator<SATraits>::InternalAllocatorType
// mxtl::SlabAllocator<SATraits>::allocator_(ctor_args...);
//
// To ease some of this pain, a helper macro is provided.  Using it looks
// like...
//
// DECLARE_STATIC_SLAB_ALLOCATOR_STORAGE(SATraits, ctor_args...);
//
// :: API ::
//
// The slab allocator API consists of 2 methods.
// ++ Ctor(size_t, bool)
// ++ New(...)
//
// The allocator constructor takes two arguments.  The first is the maximum
// number of slabs the allocator is permitted to allocate.  The second is a bool
// which specifies whether or not an attempt should be made to pre-allocate the
// first slab.  By default, this defaults to false.  As noted earlier, limiting
// the total number of slabs to 1 and pre-allocating the slab during
// construction guarantees O(1) allocations during operation.
//
// New(...) is used to construct and return a pointer (of designated type) to an
// object allocated from slab memory.  An appropriate form of nullptr will be
// returned if the allocator has reached its allocation limit.  New(...) will
// accept any set of parameters compatible with one of an object's constructors.
//
// ***********************
// ** Unmanaged Example **
// ***********************
//
// class MyObject : public mxtl::SinglyLinkedListable<MyObject*> {
// public:
//     explicit MyObject(int val) : my_int_(val) { }
//     explicit MyObject(const char* val) : my_string_(val) { }
// private:
//     int my_int_ = 0;
//     const char* my_string_ = nullptr;
// };
//
// /* Make an instanced slab allocator with 4KB slabs which dispenses
//  * unmanaged pointers and uses no locks */
// using AllocatorTraits = mxtl::UnlockedSlabAllocatorTraits<MyObject*, 4096u>;
// using AllocatorType   = mxtl::SlabAllocator<AllocatorTraits>;
// using ObjListType     = mxtl::SinglyLinkedList<MyObject*>;
//
// void my_function() {
//     AllocatorType allocator(1, true);   /* one pre-allocated slab */
//     ObjListType list;
//
//     /* Allocate a slab's worth of objects and put them on a list.  Use both
//      * constructors. */
//     for (size_t i = 0; i < AllocatorType::AllocsPerSlab; ++i) {
//         auto ptr = FlipACoin()
//                  ? allocator.New(5)                     /* int form */
//                  : allocator.New("this is a string");   /* string form */
//         list.push_front(ptr);
//     }
//
//     /* Do something with all of our objects */
//     for (auto& obj_ref : list)
//         DoSomething(obj_ref);
//
//     /* Return all of the objects to the allocator */
//     while(!list.is_empty())
//         delete list.pop_front();
// }
//
// ********************
// ** RefPtr Example **
// ********************
//
// /* Make a static slab allocator with default (16KB) sized slabs which
//  * dispenses RefPtr<>s and uses default (mxtl::Mutex) locking.  Give the
//  * allocator permission to allocate up to 64 slabs, but do not attempt to
//  * pre-allocate the first.
//  */
// class MyObject;
// using AllocatorTraits = mxtl::StaticSlabAllocatorTraits<mxtl::RefPtr<MyObject>>;
// using AllocatorType   = mxtl::SlabAllocator<AllocatorTraits>;
// using ObjListType     = mxtl::SinglyLinkedList<mxtl::RefPtr<MyObject>>;
//
// DECLARE_STATIC_SLAB_ALLOCATOR_STORAGE(AllocatorTraits, 64);
//
// class MyObject : public mxtl::SlabAllocated<AllocatorTraits>,
//                  public mxtl::RefCounted<MyObject>,
//                  public mxtl::SinglyLinkedListable<mxtl::RefCounted<MyObject>> {
// public:
//     explicit MyObject(int val) : my_int_(val) { }
//     explicit MyObject(const char* val) : my_string_(val) { }
// private:
//     int my_int_ = 0;
//     const char* my_string_ = nullptr;
// };
//
// void my_function() {
//     ObjListType list;
//
//     /* Allocate two slabs' worth of objects and put them on a list.  Use both
//      * constructors. */
//     for (size_t i = 0; i < (2 * AllocatorType::AllocsPerSlab); ++i) {
//         auto ptr = FlipACoin()
//                  ? AllocatorType::New(5)                     /* int form */
//                  : AllocatorType::New("this is a string");   /* string form */
//         list.push_front(ptr);
//     }
//
//     /* Do something with all of our objects */
//     for (auto& obj_ref : list)
//         DoSomething(obj_ref);
//
//     /* Clear the list and automatically return all of our objects */
//     list.clear();
// }
//
namespace mxtl {

// fwd decls
template <typename T,
          size_t   SLAB_SIZE,
          typename LockType,
          bool     IsStaticAllocator> struct SlabAllocatorTraits;
template <typename SATraits, typename = void> class SlabAllocator;
template <typename SATraits, typename = void> class SlabAllocated;

constexpr size_t DEFAULT_SLAB_ALLOCATOR_SLAB_SIZE = (16 << 10u);

namespace internal {

// internal fwd-decls
template <typename T> struct SlabAllocatorPtrTraits;
template <typename SATraits> class SlabAllocator;

// Support for raw pointers
template <typename T>
struct SlabAllocatorPtrTraits<T*> {
    using ObjType = T;
    using PtrType = T*;

    static constexpr PtrType CreatePtr(ObjType* ptr) { return ptr; }
};

// Support for unique_ptr
template <typename T>
struct SlabAllocatorPtrTraits<unique_ptr<T>> {
    using ObjType = T;
    using PtrType = unique_ptr<T>;

    static constexpr PtrType CreatePtr(ObjType* ptr) { return PtrType(ptr); }
};

// Support for RefPtr
template <typename T>
struct SlabAllocatorPtrTraits<RefPtr<T>> {
    using ObjType = T;
    using PtrType = RefPtr<T>;

    static constexpr PtrType CreatePtr(ObjType* ptr) { return AdoptRef<ObjType>(ptr); }
};

// Trait class used to set the origin of a slab allocated object, if needed.
template <typename SATraits, typename = void>
struct SlabOriginSetter {
    static inline void SetOrigin(typename SATraits::ObjType* ptr,
                                 internal::SlabAllocator<SATraits>* origin) {
        DEBUG_ASSERT((ptr != nullptr) && (origin != nullptr));
        ptr->slab_origin_ = origin;
    }
};

template <typename SATraits>
struct SlabOriginSetter<SATraits,
                        typename enable_if<SATraits::IsStaticAllocator == true>::type> {
    static inline void SetOrigin(typename SATraits::ObjType* ptr,
                                 internal::SlabAllocator<SATraits>* origin) { }
};

template <typename SATraits>
class SlabAllocator {
public:
    using PtrTraits = typename SATraits::PtrTraits;
    using PtrType   = typename SATraits::PtrType;
    using ObjType   = typename SATraits::ObjType;

    // Slab allocated objects must derive from SlabAllocated<SATraits>.
    static_assert(is_base_of<SlabAllocated<SATraits>, ObjType>::value,
                  "Objects which are slab allocated from an allocator of type "
                  "SlabAllocator<T> must derive from SlabAllocated<T>.");

    DISALLOW_COPY_ASSIGN_AND_MOVE(SlabAllocator);

    explicit SlabAllocator(size_t max_slabs, bool alloc_initial = false)
        : max_slabs_(max_slabs) {
        // Attempt to ensure that at least one slab has been allocated before
        // finishing construction if the user has asked us to do so.  In some
        // situations, this can help to ensure that allocation performance is
        // always O(1), provided that the slab limit has been configured to be
        // 1.
        if (alloc_initial) {
            void* first_alloc = Allocate();
            if (first_alloc != nullptr)
                this->ReturnToFreeList(first_alloc);
        }
    }

    ~SlabAllocator() {
        // Don't bother taking the alloc_lock_ here.  If we need to hold the
        // lock, that means someone is already accessing us while we are
        // destructing, and we are already screwed.
        __UNUSED size_t allocated_count = 0;

        while (!slab_list_.is_empty()) {
            Slab* free_me = slab_list_.pop_front();
            allocated_count += free_me->alloc_count();
            free(reinterpret_cast<void*>(free_me));
        }

        // Make sure that everything which was ever allocated had been returned
        // to the free list before we were destroyed.
        DEBUG_ASSERT(this->free_list_size_ == allocated_count);

        // null out the free list so that it does not assert that we left
        // unmanaged pointers on it as we destruct.
        this->free_list_.clear_unsafe();
    }

    template <typename... ConstructorSignature>
    PtrType New(ConstructorSignature&&... args) {
        void* mem = Allocate();

        if (mem == nullptr)
            return nullptr;

        // Construct the object, then record the slab allocator it came from so
        // it can be returned later on.
        ObjType* obj = new (mem) ObjType(mxtl::forward<ConstructorSignature>(args)...);
        SlabOriginSetter<SATraits>::SetOrigin(obj, this);

        return PtrTraits::CreatePtr(obj);
    }

    size_t max_slabs() const { return max_slabs_; }

protected:
    friend class ::mxtl::SlabAllocator<SATraits>;
    friend class ::mxtl::SlabAllocated<SATraits>;

    static constexpr size_t SLAB_SIZE = SATraits::SLAB_SIZE;

    struct FreeListEntry : public SinglyLinkedListable<FreeListEntry*> { };

    class Slab {
    public:
        static constexpr size_t RequiredSize  = max(sizeof(FreeListEntry), sizeof(ObjType));
        static constexpr size_t RequiredAlign = max(alignof(FreeListEntry), alignof(ObjType));

        struct Allocation {
            uint8_t data[RequiredSize];
        } __ALIGNED(RequiredAlign);

        void* Allocate();
        size_t alloc_count() const { return alloc_count_; }

    private:
        friend class  SlabAllocator;
        friend struct DefaultSinglyLinkedListTraits<Slab*>;
        SinglyLinkedListNodeState<Slab*> sll_node_state_;
        size_t                           alloc_count_ = 0;
        Allocation                       allocs_[];
    };

    static constexpr size_t SlabOverhead        = offsetof(Slab, allocs_);
    static constexpr size_t SlabAllocationCount = (SLAB_SIZE - SlabOverhead)
                                                / sizeof(typename Slab::Allocation);

    static_assert((sizeof(Slab) < SLAB_SIZE) || (SlabOverhead < SLAB_SIZE),
                  "SLAB_SIZE too small to hold slab bookkeeping");
    static_assert(SlabAllocationCount > 0, "SLAB_SIZE too small to hold even 1 chunk");

    void* Allocate() {
        AutoLock alloc_lock(this->alloc_lock_);

        // If we can alloc from the free list, do so.
        if (!this->free_list_.is_empty()) {
            this->dec_free_list_size();
            return this->free_list_.pop_front();
        }

        // If we can allocate from the currently active slab, do so.
        if (!slab_list_.is_empty()) {
            auto& active_slab = slab_list_.front();
            void* mem = active_slab.Allocate();
            if (mem)
                return mem;
        }

        // If we are allowed to allocate new slabs, try to do so.
        if (slab_count_ < max_slabs_) {
            void* slab_mem = aligned_alloc(alignof(Slab), SLAB_SIZE);
            if (slab_mem != nullptr) {
                Slab* slab = new (slab_mem) Slab();

                slab_count_++;
                slab_list_.push_front(slab);

                return slab->Allocate();
            }
        }

        // Looks like we have run out of resources.
        return nullptr;
    }

    void ReturnToFreeList(void* ptr) {
        FreeListEntry* free_obj = new (ptr) FreeListEntry;
        {
            AutoLock alloc_lock(alloc_lock_);
            inc_free_list_size();
            free_list_.push_front(free_obj);
        }
    }

    typename SATraits::LockType      alloc_lock_;
    SinglyLinkedList<FreeListEntry*> free_list_;
    SinglyLinkedList<Slab*>          slab_list_;
    const size_t                     max_slabs_;
    size_t                           slab_count_ = 0;

#if (LK_DEBUGLEVEL > 1)
    inline void inc_free_list_size() { ++free_list_size_; }
    inline void dec_free_list_size() { --free_list_size_; }
    size_t free_list_size_ = 0;
#else
    inline void inc_free_list_size() { }
    inline void dec_free_list_size() { }
#endif

public:
    // Publicly exported number of allocations possible per slab.  Used during testing.
    static constexpr size_t AllocsPerSlab = SlabAllocationCount;
};

template <typename SATraits>
inline void* SlabAllocator<SATraits>::Slab::Allocate() {
    return (alloc_count_ >= SlabAllocator<SATraits>::SlabAllocationCount)
        ? nullptr : allocs_[alloc_count_++].data;
}

}  // namespace internal

////////////////////////////////////////////////////////////////////////////////
//
// Fundamental traits which control the properties of a slab allocator.
//
// Parameters:
// ++ T
//  The pointer type of the object to be created by the allocator.  Must be one
//  of the following...
//  ++ ObjectType*
//  ++ mxtl::unique_ptr<ObjectType>
//  ++ mxtl::RefPtr<ObjectType>
//
// ++ SLAB_SIZE
//  The size (in bytes) of an individual slab.  Defaults to 16KB
//
// ++ LockType
//  The mxtl::AutoLock compatible class which will handle synchronization.
//
// ++ IsStaticAllocator
//  Selects between a static or instanced allocator type.
//
// Instanced allocators allow multiple allocation pools to be created, each with
// their own quota, but require an extra pointer-per-object of overhead
// (provided by SlabAllocated<>) when used with managed pointers in order to be
// able to return to their allocator origin.
//
// Static allocators are global for a process and distinguishable only by type,
// but do not require any extra per-object overhead when used with managed
// pointers.
//
////////////////////////////////////////////////////////////////////////////////
template <typename T,
          size_t   _SLAB_SIZE         = DEFAULT_SLAB_ALLOCATOR_SLAB_SIZE,
          typename _LockType          = ::mxtl::Mutex,
          bool     _IsStaticAllocator = false>
struct SlabAllocatorTraits {
    using PtrTraits     = internal::SlabAllocatorPtrTraits<T>;
    using PtrType       = typename PtrTraits::PtrType;
    using ObjType       = typename PtrTraits::ObjType;
    using LockType      = _LockType;

    static constexpr size_t SLAB_SIZE = _SLAB_SIZE;
    static constexpr bool   IsStaticAllocator = _IsStaticAllocator;
};

////////////////////////////////////////////////////////////////////////////////
//
// Implementation of an instanced slab allocator.
//
////////////////////////////////////////////////////////////////////////////////
template <typename SATraits>
class SlabAllocator<SATraits,
                    typename enable_if<SATraits::IsStaticAllocator == false>::type>
      : public internal::SlabAllocator<SATraits> {
public:
    using PtrTraits         = typename SATraits::PtrTraits;
    using PtrType           = typename SATraits::PtrType;
    using ObjType           = typename SATraits::ObjType;
    using BaseAllocatorType = internal::SlabAllocator<SATraits>;

    static constexpr size_t AllocsPerSlab = BaseAllocatorType::AllocsPerSlab;

    explicit SlabAllocator(size_t max_slabs, bool alloc_initial = false)
        : BaseAllocatorType(max_slabs, alloc_initial) { }

    ~SlabAllocator() { }
};

template <typename SATraits>
class SlabAllocated<SATraits,
                    typename enable_if<SATraits::IsStaticAllocator == false>::type> {
public:
    using AllocatorType = internal::SlabAllocator<SATraits>;
    using ObjType       = typename SATraits::ObjType;

     SlabAllocated() { }
    ~SlabAllocated() { }

    DISALLOW_COPY_ASSIGN_AND_MOVE(SlabAllocated);

    void operator delete(void* ptr) {
        // Note: this is a bit sketchy...  We have been destructed at this point
        // in time, but we are about to access our slab_origin_ member variable.
        // The *only* reason that this is OK is that we know that our destructor
        // does not touch slab_origin_, and no one else in our hierarchy should
        // be able to modify slab_origin_ because it is private.
        ObjType* obj_ptr = reinterpret_cast<ObjType*>(ptr);

        DEBUG_ASSERT(obj_ptr != nullptr);
        DEBUG_ASSERT(obj_ptr->slab_origin_ != nullptr);
        obj_ptr->slab_origin_->ReturnToFreeList(obj_ptr);
    }

private:
    friend struct internal::SlabOriginSetter<SATraits>;
    AllocatorType* slab_origin_ = nullptr;
};

// Shorthand for declaring the properties of an instanced allocator (somewhat
// superfluous as the default is instanced)
template <typename T,
          size_t   SLAB_SIZE = DEFAULT_SLAB_ALLOCATOR_SLAB_SIZE,
          typename LockType  = ::mxtl::Mutex>
using InstancedSlabAllocatorTraits = SlabAllocatorTraits<T, SLAB_SIZE, LockType, false>;

template <typename T,
          size_t   SLAB_SIZE = DEFAULT_SLAB_ALLOCATOR_SLAB_SIZE>
using UnlockedInstancedSlabAllocatorTraits =
    SlabAllocatorTraits<T, SLAB_SIZE, ::mxtl::NullLock, false>;

template <typename T,
          size_t   SLAB_SIZE = DEFAULT_SLAB_ALLOCATOR_SLAB_SIZE>
using UnlockedSlabAllocatorTraits =
    SlabAllocatorTraits<T, SLAB_SIZE, ::mxtl::NullLock>;

////////////////////////////////////////////////////////////////////////////////
//
// Implementation of a static slab allocator.
//
////////////////////////////////////////////////////////////////////////////////
template <typename SATraits>
class SlabAllocator<SATraits,
                    typename enable_if<SATraits::IsStaticAllocator == true>::type> {
public:
    using PtrTraits             = typename SATraits::PtrTraits;
    using PtrType               = typename SATraits::PtrType;
    using ObjType               = typename SATraits::ObjType;
    using InternalAllocatorType = internal::SlabAllocator<SATraits>;

    static constexpr size_t AllocsPerSlab = InternalAllocatorType::AllocsPerSlab;

    // Do not allow instantiation of static slab allocators.
    SlabAllocator() = delete;

    template <typename... ConstructorSignature>
    static PtrType New(ConstructorSignature&&... args) {
        return allocator_.New(mxtl::forward<ConstructorSignature>(args)...);
    }

    static inline void Delete(ObjType* ptr) {
        return allocator_.Delete(ptr);
    }

    static size_t max_slabs() { return allocator_.max_slabs(); }

private:
    friend class SlabAllocated<SATraits>;
    static void ReturnToFreeList(void* ptr) { allocator_.ReturnToFreeList(ptr); }
    static InternalAllocatorType allocator_;
};

template <typename SATraits>
class SlabAllocated<SATraits,
                    typename enable_if<SATraits::IsStaticAllocator == true>::type> {
public:
    SlabAllocated() { }
    DISALLOW_COPY_ASSIGN_AND_MOVE(SlabAllocated);

    using AllocatorType = SlabAllocator<SATraits>;
    using ObjType       = typename SATraits::ObjType;

    void operator delete(void* ptr) {
        DEBUG_ASSERT(ptr != nullptr);
        AllocatorType::ReturnToFreeList(reinterpret_cast<ObjType*>(ptr));
    }
};

// Shorthand for declaring the properties of a static allocator
template <typename T,
          size_t   SLAB_SIZE = DEFAULT_SLAB_ALLOCATOR_SLAB_SIZE,
          typename LockType  = ::mxtl::Mutex>
using StaticSlabAllocatorTraits = SlabAllocatorTraits<T, SLAB_SIZE, LockType, true>;

template <typename T,
          size_t   SLAB_SIZE = DEFAULT_SLAB_ALLOCATOR_SLAB_SIZE>
using UnlockedStaticSlabAllocatorTraits = SlabAllocatorTraits<T, SLAB_SIZE, ::mxtl::NullLock, true>;

// Shorthand for declaring the global storage required for a static allocator
#define DECLARE_STATIC_SLAB_ALLOCATOR_STORAGE(ALLOC_TRAITS, ...) \
template<> ::mxtl::SlabAllocator<typename ALLOC_TRAITS>::InternalAllocatorType \
mxtl::SlabAllocator<typename ALLOC_TRAITS>::allocator_(__VA_ARGS__)

}  // namespace mxtl
