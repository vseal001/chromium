// -*- Mode: C++; c-basic-offset: 2; indent-tabs-mode: nil -*-
// Copyright (c) 2008, Google Inc.
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
//     * Redistributions of source code must retain the above copyright
// notice, this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above
// copyright notice, this list of conditions and the following disclaimer
// in the documentation and/or other materials provided with the
// distribution.
//     * Neither the name of Google Inc. nor the names of its
// contributors may be used to endorse or promote products derived from
// this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

// ---
// Author: Sanjay Ghemawat <opensource@google.com>
//
// Common definitions for tcmalloc code.

#ifndef TCMALLOC_COMMON_H_
#define TCMALLOC_COMMON_H_

#include "config.h"
#include <stddef.h>                     // for size_t
#ifdef HAVE_STDINT_H
#include <stdint.h>                     // for uintptr_t, uint64_t
#endif
#include "internal_logging.h"  // for ASSERT, etc
#include "base/basictypes.h"   // for LIKELY, etc

// Type that can hold a page number
typedef uintptr_t PageID;

// Type that can hold the length of a run of pages
typedef uintptr_t Length;

//-------------------------------------------------------------------
// Configuration
//-------------------------------------------------------------------

#if defined(TCMALLOC_ALIGN_8BYTES)
// Unless we force to use 8 bytes alignment we use an alignment of
// at least 16 bytes to statisfy requirements for some SSE types.
// Keep in mind when using the 16 bytes alignment you can have a space
// waste due alignment of 25%. (eg malloc of 24 bytes will get 32 bytes)
static const size_t kMinAlign   = 8;
#else
static const size_t kMinAlign   = 16;
#endif

// Using large pages speeds up the execution at a cost of larger memory use.
// Deallocation may speed up by a factor as the page map gets 8x smaller, so
// lookups in the page map result in fewer L2 cache misses, which translates to
// speedup for application/platform combinations with high L2 cache pressure.
// As the number of size classes increases with large pages, we increase
// the thread cache allowance to avoid passing more free ranges to and from
// central lists.  Also, larger pages are less likely to get freed.
// These two factors cause a bounded increase in memory use.
#if defined(TCMALLOC_32K_PAGES)
static const size_t kPageShift  = 15;
#elif defined(TCMALLOC_64K_PAGES)
static const size_t kPageShift  = 16;
#else
// Original TCMalloc code used kPageShift == 13.  In Chromium, we changed
// this to 12 (as was done in prior versions of TCMalloc).
static const size_t kPageShift = 12;
#endif

static const size_t kClassSizesMax = 96;

static const size_t kMaxThreadCacheSize = 4 << 20;

static const size_t kPageSize   = 1 << kPageShift;
// Original TCMalloc code used kMaxSize == 256 * 1024.  In Chromium, we
// changed this to 32K.
static const size_t kMaxSize = 32u * 1024;
static const size_t kAlignment  = 8;
// For all span-lengths <= kMaxPages we keep an exact-size list in PageHeap.
static const size_t kMaxPages = 1 << (20 - kPageShift);

// Default bound on the total amount of thread caches.
#ifdef TCMALLOC_SMALL_BUT_SLOW
// Make the overall thread cache no bigger than that of a single thread
// for the small memory footprint case.
static const size_t kDefaultOverallThreadCacheSize = kMaxThreadCacheSize;
#else
static const size_t kDefaultOverallThreadCacheSize = 8u * kMaxThreadCacheSize;
#endif

// Lower bound on the per-thread cache sizes
static const size_t kMinThreadCacheSize = kMaxSize * 2;

// The number of bytes one ThreadCache will steal from another when
// the first ThreadCache is forced to Scavenge(), delaying the
// next call to Scavenge for this thread.
static const size_t kStealAmount = 1 << 16;

// The number of times that a deallocation can cause a freelist to
// go over its max_length() before shrinking max_length().
static const int kMaxOverages = 3;

// Maximum length we allow a per-thread free-list to have before we
// move objects from it into the corresponding central free-list.  We
// want this big to avoid locking the central free-list too often.  It
// should not hurt to make this list somewhat big because the
// scavenging code will shrink it down when its contents are not in use.
static const int kMaxDynamicFreeListLength = 8192;

static const Length kMaxValidPages = (~static_cast<Length>(0)) >> kPageShift;

#if __aarch64__ || __x86_64__ || _M_AMD64 || _M_ARM64
// All current x86_64 processors only look at the lower 48 bits in
// virtual to physical address translation. The top 16 are all same as
// bit 47. And bit 47 value 1 reserved for kernel-space addresses in
// practice. So it is actually 47 usable bits from malloc
// perspective. This lets us use faster two level page maps on this
// architecture.
//
// There is very similar story on 64-bit arms except it has full 48
// bits for user-space. Because of that, and because in principle OSes
// can start giving some of highest-bit-set addresses to user-space,
// we don't bother to limit x86 to 47 bits.
//
// As of now there are published plans to add more bits to x86-64
// virtual address space, but since 48 bits has been norm for long
// time and lots of software is relying on it, it will be opt-in from
// OS perspective. So we can keep doing "48 bits" at least for now.
static const int kAddressBits = (sizeof(void*) < 8 ? (8 * sizeof(void*)) : 48);
#else
// mipsen and ppcs have more general hardware so we have to support
// full 64-bits of addresses.
static const int kAddressBits = 8 * sizeof(void*);
#endif

namespace tcmalloc {

// Convert byte size into pages.  This won't overflow, but may return
// an unreasonably large value if bytes is huge enough.
inline Length pages(size_t bytes) {
  return (bytes >> kPageShift) +
      ((bytes & (kPageSize - 1)) > 0 ? 1 : 0);
}

// For larger allocation sizes, we use larger memory alignments to
// reduce the number of size classes.
int AlignmentForSize(size_t size);

// Size-class information + mapping
class SizeMap {
 private:
  //-------------------------------------------------------------------
  // Mapping from size to size_class and vice versa
  //-------------------------------------------------------------------

  // Sizes <= 1024 have an alignment >= 8.  So for such sizes we have an
  // array indexed by ceil(size/8).  Sizes > 1024 have an alignment >= 128.
  // So for these larger sizes we have an array indexed by ceil(size/128).
  //
  // We flatten both logical arrays into one physical array and use
  // arithmetic to compute an appropriate index.  The constants used by
  // ClassIndex() were selected to make the flattening work.
  //
  // Examples:
  //   Size       Expression                      Index
  //   -------------------------------------------------------
  //   0          (0 + 7) / 8                     0
  //   1          (1 + 7) / 8                     1
  //   ...
  //   1024       (1024 + 7) / 8                  128
  //   1025       (1025 + 127 + (120<<7)) / 128   129
  //   ...
  //   32768      (32768 + 127 + (120<<7)) / 128  376
  static const int kMaxSmallSize = 1024;
  static const size_t kClassArraySize =
      ((kMaxSize + 127 + (120 << 7)) >> 7) + 1;
  unsigned char class_array_[kClassArraySize];

  static inline size_t SmallSizeClass(size_t s) {
    return (static_cast<uint32_t>(s) + 7) >> 3;
  }

  static inline size_t LargeSizeClass(size_t s) {
    return (static_cast<uint32_t>(s) + 127 + (120 << 7)) >> 7;
  }

  // If size is no more than kMaxSize, compute index of the
  // class_array[] entry for it, putting the class index in output
  // parameter idx and returning true. Otherwise return false.
  static inline bool ATTRIBUTE_ALWAYS_INLINE ClassIndexMaybe(size_t s,
                                                             uint32* idx) {
    if (PREDICT_TRUE(s <= kMaxSmallSize)) {
      *idx = (static_cast<uint32>(s) + 7) >> 3;
      return true;
    } else if (s <= kMaxSize) {
      *idx = (static_cast<uint32>(s) + 127 + (120 << 7)) >> 7;
      return true;
    }
    return false;
  }

  // Compute index of the class_array[] entry for a given size
  static inline size_t ClassIndex(size_t s) {
    // Use unsigned arithmetic to avoid unnecessary sign extensions.
    ASSERT(0 <= s);
    ASSERT(s <= kMaxSize);
    if (PREDICT_TRUE(s <= kMaxSmallSize)) {
      return SmallSizeClass(s);
    } else {
      return LargeSizeClass(s);
    }
  }

  // Number of objects to move between a per-thread list and a central
  // list in one shot.  We want this to be not too small so we can
  // amortize the lock overhead for accessing the central list.  Making
  // it too big may temporarily cause unnecessary memory wastage in the
  // per-thread free list until the scavenger cleans up the list.
  int num_objects_to_move_[kClassSizesMax];

  int NumMoveSize(size_t size);

  // Mapping from size class to max size storable in that class
  int32 class_to_size_[kClassSizesMax];

  // Mapping from size class to number of pages to allocate at a time
  size_t class_to_pages_[kClassSizesMax];

 public:
  size_t num_size_classes;

  // Constructor should do nothing since we rely on explicit Init()
  // call, which may or may not be called before the constructor runs.
  SizeMap() { }

  // Initialize the mapping arrays
  void Init();

  inline int SizeClass(size_t size) {
    return class_array_[ClassIndex(size)];
  }

  // Check if size is small enough to be representable by a size
  // class, and if it is, put matching size class into *cl. Returns
  // true iff matching size class was found.
  inline bool ATTRIBUTE_ALWAYS_INLINE GetSizeClass(size_t size, uint32* cl) {
    uint32 idx;
    if (!ClassIndexMaybe(size, &idx)) {
      return false;
    }
    *cl = class_array_[idx];
    return true;
  }

  // Get the byte-size for a specified class
  inline int32 ATTRIBUTE_ALWAYS_INLINE ByteSizeForClass(uint32 cl) {
    return class_to_size_[cl];
  }

  // Mapping from size class to max size storable in that class
  inline int32 class_to_size(uint32 cl) {
    return class_to_size_[cl];
  }

  // Mapping from size class to number of pages to allocate at a time
  inline size_t class_to_pages(uint32 cl) {
    return class_to_pages_[cl];
  }

  // Number of objects to move between a per-thread list and a central
  // list in one shot.  We want this to be not too small so we can
  // amortize the lock overhead for accessing the central list.  Making
  // it too big may temporarily cause unnecessary memory wastage in the
  // per-thread free list until the scavenger cleans up the list.
  inline int num_objects_to_move(uint32 cl) {
    return num_objects_to_move_[cl];
  }
};

// Allocates "bytes" worth of memory and returns it.  Increments
// metadata_system_bytes appropriately.  May return NULL if allocation
// fails.  Requires pageheap_lock is held.
void* MetaDataAlloc(size_t bytes);

// Returns the total number of bytes allocated from the system.
// Requires pageheap_lock is held.
uint64_t metadata_system_bytes();

// size/depth are made the same size as a pointer so that some generic
// code below can conveniently cast them back and forth to void*.
static const int kMaxStackDepth = 31;
struct StackTrace {
  uintptr_t size;          // Size of object
  uintptr_t depth;         // Number of PC values stored in array below
  void*     stack[kMaxStackDepth];
};

}  // namespace tcmalloc

#endif  // TCMALLOC_COMMON_H_
