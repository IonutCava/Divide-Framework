/*
   Copyright (c) 2018 DIVIDE-Studio
   Copyright (c) 2009 Ionut Cava

   This file is part of DIVIDE Framework.

   Permission is hereby granted, free of charge, to any person obtaining a copy
   of this software
   and associated documentation files (the "Software"), to deal in the Software
   without restriction,
   including without limitation the rights to use, copy, modify, merge, publish,
   distribute, sublicense,
   and/or sell copies of the Software, and to permit persons to whom the
   Software is furnished to do so,
   subject to the following conditions:

   The above copyright notice and this permission notice shall be included in
   all copies or substantial portions of the Software.

   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
   IMPLIED,
   INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
   PARTICULAR PURPOSE AND NONINFRINGEMENT.
   IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
   DAMAGES OR OTHER LIABILITY,
   WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR
   IN CONNECTION WITH THE SOFTWARE
   OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

 */

#pragma once
#ifndef _PLATFORM_MEMORY_DEFINES_H_
#define _PLATFORM_MEMORY_DEFINES_H_
#include "PlatformDataTypes.h"

void* malloc_aligned(size_t size, size_t alignment, size_t offset = 0u);
void  free_aligned(void*& ptr);

void* operator new[](   size_t size, const char* pName, int flags, unsigned int debugFlags, const char* file, int line);
void  operator delete[](void* ptr,   const char* pName, int flags, unsigned int debugFlags, const char* file, int line);

void* operator new[](   size_t size, size_t alignment, size_t alignmentOffset, const char* pName, int flags, unsigned int debugFlags, const char* file, int line);
void  operator delete[](void* ptr,   size_t alignment, size_t alignmentOffset, const char* pName, int flags, unsigned int debugFlags, const char* file, int line);

void* operator new(     size_t size, const char* zFile, size_t nLine);
void  operator delete(  void* ptr,   const char* zFile, size_t nLine);
void* operator new[](   size_t size, const char* zFile, size_t nLine);
void  operator delete[](void* ptr,   const char* zFile, size_t nLine);

#if !defined(MemoryManager_NEW)
#define MemoryManager_NEW new (__FILE__, __LINE__)
#endif

namespace Divide {
    namespace MemoryManager {

        void log_delete(void* p);

        template <typename T>
        void SAFE_FREE(T*& ptr) {
            if (ptr != nullptr) {
                free(ptr);
                ptr = nullptr;
            }
        }

        /// Deletes and nullifies the specified pointer
        template <typename T>
        void DELETE(T*& ptr) {
            log_delete(ptr);
            delete ptr;
            ptr = nullptr;
        }

        /// Deletes and nullifies the specified pointer
        template <typename T>
        void SAFE_DELETE(T*& ptr) {
            if (ptr != nullptr) {
                DELETE(ptr);
            }
        }

        /// Deletes and nullifies the specified array pointer
        template <typename T>
        void DELETE_ARRAY(T*& ptr) {
            log_delete(ptr);

            delete[] ptr;
            ptr = nullptr;
        }

        /// Deletes and nullifies the specified array pointer
        template <typename T>
        void SAFE_DELETE_ARRAY(T*& ptr) {
            if (ptr != nullptr) {
                DELETE_ARRAY(ptr);
            }
        }

#define SET_DELETE_FRIEND \
    template <typename T> \
    friend void MemoryManager::DELETE(T*& ptr); \

#define SET_SAFE_DELETE_FRIEND \
    SET_DELETE_FRIEND \
    template <typename T>      \
    friend void MemoryManager::SAFE_DELETE(T*& ptr);


#define SET_DELETE_ARRAY_FRIEND \
    template <typename T>       \
    friend void MemoryManager::DELETE_ARRAY(T*& ptr);

#define SET_SAFE_DELETE_ARRAY_FRIEND \
    SET_DELETE_ARRAY_FRIEND \
    template <typename T>            \
    friend void MemoryManager::DELETE_ARRAY(T*& ptr);

        /// Deletes every element from the vector and clears it at the end
        template <template <typename, typename> class Container,
            typename Value,
            typename Allocator = std::allocator<Value>>
            void DELETE_CONTAINER(Container<Value*, Allocator>& container) {
            for (Value* iter : container) {
                log_delete(iter);
                delete iter;
            }

            container.clear();
        }

#define SET_DELETE_CONTAINER_FRIEND \
    template <template <typename, typename> class Container, \
              typename Value, \
              typename Allocator> \
    friend void MemoryManager::DELETE_CONTAINER(Container<Value*, Allocator>& container);

#define SET_DELETE_HASHMAP_FRIEND                       \
    template <typename K, typename V, typename HashFun> \
    friend void MemoryManager::DELETE_HASHMAP(hashMap<K, V, HashFun>& map);

        /// Deletes the object pointed to by "OLD" and redirects that pointer to the
        /// object pointed by "NEW"
        /// "NEW" must be a derived (or same) class of OLD
        template <typename Base, typename Derived>
        void SAFE_UPDATE(Base*& OLD, Derived* const NEW) {
            static_assert(std::is_base_of<Base, Derived>::value,
                "SAFE_UPDATE error: New must be a descendant of Old");
            SAFE_DELETE(OLD);
            OLD = NEW;
        }
#define SET_SAFE_UPDATE_FRIEND                 \
    template <typename Base, typename Derived> \
    friend void MemoryManager::SAFE_UPDATE(Base*& OLD, Derived* const NEW);

    };  // namespace MemoryManager
}; //namespace Divide

#endif //_PLATFORM_MEMORY_DEFINES_H_
