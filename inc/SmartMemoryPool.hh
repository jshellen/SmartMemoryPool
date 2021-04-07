#ifndef _SMART_MEMORY_POOL_
#define _SMART_MEMORY_POOL_

#include <iostream>
#include <atomic>
#include <memory>
#include <sstream>
#include <thread>
#include <optional>

namespace jshellen {

    template<typename B>
    class ISmartMemoryPool {
    public:
        virtual ~ISmartMemoryPool() = default;
    public:
        virtual void deallocate(B* p) = 0;
    };

    template<typename B>
    struct SmartDeleter {
    private:
        std::weak_ptr<ISmartMemoryPool<B>*> mParentPool;
    public:
        explicit SmartDeleter(std::weak_ptr<ISmartMemoryPool<B>*> pool) : mParentPool{pool} {};
    public:
        void operator()(B* ptr){
            if (auto pool = mParentPool.lock()){
                ptr->~B();
                (*pool.get())->deallocate(ptr);
            }
        }
    };

    template<typename DerivedType, typename BaseType>
    class SmartMemoryPool final : public ISmartMemoryPool<BaseType> {
    public:
        using value_type = DerivedType;
        using pointer    = DerivedType*;
        using unique_ptr = std::unique_ptr<value_type, SmartDeleter<BaseType>>;
    private:

        union MemoryBlock {
            std::aligned_storage_t<sizeof(value_type), alignof(value_type)> mStorage;
            MemoryBlock* mNext;
        };

        const uint64_t mCapacity;

        std::unique_ptr<MemoryBlock[]> mPool;

        std::atomic<MemoryBlock*> mNextFree{nullptr};

        std::atomic<uint64_t> mAvailable{mCapacity};

        std::shared_ptr<ISmartMemoryPool<BaseType>*> mThisPtr;

    public:
        SmartMemoryPool() = delete;
        SmartMemoryPool(SmartMemoryPool& other) = delete;
        SmartMemoryPool(SmartMemoryPool&& other) = delete;
    public:
        explicit SmartMemoryPool(uint64_t capacity);
        ~ SmartMemoryPool() final = default;
    private:
        [[nodiscard]] std::optional<pointer> allocate();
    public:
        void deallocate(BaseType* p) override;
    public:

        [[nodiscard]] uint64_t available();

        template<class ...Args>
        [[nodiscard]] unique_ptr construct(Args&& ...args);

        void destruct(std::unique_ptr<DerivedType, SmartDeleter<BaseType> > p) noexcept ;
        void destruct(DerivedType* p) noexcept ;
    };
}


namespace jshellen {
    template<typename DerivedType, typename BaseType>
    SmartMemoryPool<DerivedType, BaseType>::SmartMemoryPool(uint64_t capacity) :
            mCapacity{capacity},
            mPool{std::make_unique<MemoryBlock[]>(mCapacity)},
            mThisPtr{new ISmartMemoryPool<BaseType>*(this)}
    {
        for (auto i = 1; i < mCapacity; i++){
            mPool[i-1].mNext = &mPool[i];
        }

        mNextFree = &mPool[0];
    }
}

namespace jshellen {
    template<typename DerivedType, typename BaseType>
    std::optional<typename SmartMemoryPool<DerivedType, BaseType>::pointer>
    SmartMemoryPool<DerivedType, BaseType>::allocate() {

        auto item = mNextFree.load();

        while (item != nullptr && !mNextFree.compare_exchange_strong(item, item->mNext)){}

        if (item == nullptr)
            return std::nullopt;

        return reinterpret_cast<pointer>(&item->mStorage);
    }
}

namespace jshellen {
    template<typename DerivedType, typename BaseType>
    void SmartMemoryPool<DerivedType, BaseType>::deallocate(BaseType* p) {

        const auto item = reinterpret_cast<MemoryBlock*>(p);

        item->mNext = mNextFree.load();

        while (!mNextFree.compare_exchange_strong(item->mNext, item)){}

        mAvailable++;
    }
}

namespace jshellen {
    template<typename DerivedType, typename BaseType>
    template<class... Args>
    typename SmartMemoryPool<DerivedType, BaseType>::unique_ptr
            SmartMemoryPool<DerivedType, BaseType>::construct(Args &&... args) {

       auto maybe_mem = allocate();

        if (!maybe_mem.has_value()){
            auto weak_this_ptr = std::weak_ptr<ISmartMemoryPool<BaseType>*>(mThisPtr);
            unique_ptr new_unique_ptr(nullptr, SmartDeleter<BaseType>(std::move(weak_this_ptr)));
            return std::move(new_unique_ptr);
        }

        mAvailable--;

        auto* raw_ptr_T = new (maybe_mem.value()) value_type(std::forward<Args>(args)...);

        auto weak_this_ptr = std::weak_ptr<ISmartMemoryPool<BaseType>*>(mThisPtr);
        unique_ptr new_unique_ptr(raw_ptr_T, SmartDeleter<BaseType>(std::move(weak_this_ptr)));

        return std::move(new_unique_ptr);
    }
}

namespace jshellen {
    template<typename DerivedType, typename BaseType>
    void SmartMemoryPool<DerivedType, BaseType>::destruct(
            std::unique_ptr<DerivedType, SmartDeleter<BaseType> > p) noexcept {

        auto t_ptr = p.release();

        destruct(t_ptr);
    }
}

namespace jshellen {
    template<typename DerivedType, typename BaseType>
    void SmartMemoryPool<DerivedType, BaseType>::destruct(DerivedType* p) noexcept {

        if (p == nullptr){
            return;
        }

        p->~value_type();

        deallocate(p);

    }
}

namespace jshellen {
    template<typename DerivedType, typename BaseType>
    uint64_t SmartMemoryPool<DerivedType, BaseType>::available() {
        return mAvailable.load(std::memory_order_relaxed);
    }
}

#endif //_SMART_MEMORY_POOL_
