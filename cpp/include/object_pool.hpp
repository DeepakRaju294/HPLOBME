#pragma once

#include <cstddef>
#include <memory>
#include <vector>

namespace lob {

// A free-list pool allocator, intended as a drop-in std::allocator
// replacement for node-based containers (std::list, std::map) whose
// nodes are otherwise round-tripped through malloc/free on every
// insert/erase. Profiling (results/profiling/callgrind_baseline_top_functions.txt)
// showed malloc/free machinery consuming ~42% of instructions in the
// matching hot path, dominated by per-order std::list node churn in
// PriceLevel -- this is the fix (spec section 9's "Preallocated Order
// Pool").
//
// One pool is shared (via a function-local static) across all
// PoolAllocator<T> instances for a given T, so memory freed by one
// container/engine instance is reused by the next -- appropriate for a
// single-threaded, single-process application (spec section 31). Not
// thread-safe.
template <typename T>
class PoolAllocator {
public:
    using value_type = T;

    PoolAllocator() noexcept = default;

    template <typename U>
    PoolAllocator(const PoolAllocator<U>&) noexcept {}

    template <typename U>
    struct rebind {
        using other = PoolAllocator<U>;
    };

    T* allocate(std::size_t n) {
        // Containers this pool targets (std::list/std::map nodes) always
        // request exactly one node at a time; anything else falls back
        // to ordinary allocation rather than complicating the pool.
        if (n != 1) {
            return static_cast<T*>(::operator new(n * sizeof(T)));
        }
        return static_cast<T*>(pool().allocate_one());
    }

    void deallocate(T* ptr, std::size_t n) noexcept {
        if (n != 1) {
            ::operator delete(ptr);
            return;
        }
        pool().deallocate_one(ptr);
    }

    template <typename U>
    bool operator==(const PoolAllocator<U>&) const noexcept {
        return true;
    }

    template <typename U>
    bool operator!=(const PoolAllocator<U>&) const noexcept {
        return false;
    }

private:
    struct Pool {
        struct FreeNode {
            FreeNode* next;
        };
        static constexpr std::size_t kSlotSize = sizeof(T) > sizeof(FreeNode) ? sizeof(T) : sizeof(FreeNode);
        static constexpr std::size_t kBlockSlotCount = 1024;

        FreeNode* free_list = nullptr;
        std::vector<std::unique_ptr<std::byte[]>> blocks;

        void* allocate_one() {
            if (free_list == nullptr) {
                refill();
            }
            FreeNode* node = free_list;
            free_list = free_list->next;
            return node;
        }

        void deallocate_one(void* ptr) noexcept {
            auto* node = static_cast<FreeNode*>(ptr);
            node->next = free_list;
            free_list = node;
        }

        void refill() {
            auto block = std::make_unique<std::byte[]>(kSlotSize * kBlockSlotCount);
            std::byte* base = block.get();
            blocks.push_back(std::move(block));
            for (std::size_t i = 0; i < kBlockSlotCount; ++i) {
                auto* node = reinterpret_cast<FreeNode*>(base + i * kSlotSize);
                node->next = free_list;
                free_list = node;
            }
        }
    };

    static Pool& pool() {
        static Pool instance;
        return instance;
    }
};

} // namespace lob
