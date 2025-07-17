//
// Created by Lynnette on 2025/6/24.
//

/*
 * 规则应该是这样：
 * 对于一切的自定义类，直接传入jobject_to_luatable。可以把里面
 * 所有基础类型都转换出来，也可以修改，并写回去。
 * 但对于一切稍微复杂的类型，比如List，Map，以及嵌套的数组和自定义类，这时候
 * 不进行解析，不然嵌套太多，而是直接显示jobject指针。
 * jobject指针，可以根据类型，让用户自己再用各种处理函数
 *
 * 到最后应用修改的时候，传递进入的jobejct，应该只能保存本层基础类型的修改
 * 保留不了嵌套类、嵌套数组的修改。这些内容应该全部传入嵌套的jobject单独进行应用
 */

#include "WrappedC_LuaFunction.h"


//补充一下要用到的art::accouting内容
static constexpr size_t kObjectAlignmentShift = 3;
static constexpr size_t kObjectAlignment = 1u << kObjectAlignmentShift;
static constexpr size_t kMinPageSize = 4096;
static constexpr size_t kBitsPerByte = 8;
static constexpr size_t kBitsPerByteLog2 = 3;
static constexpr int kBitsPerIntPtrT = sizeof(intptr_t) * kBitsPerByte;

namespace accounting {
    class MemMap{
    public:
        std::string name_;
        uint8_t* begin_ = nullptr;    // Start of data. May be changed by AlignBy.
        size_t size_ = 0u;            // Length of data.
        void* base_begin_ = nullptr;  // Page-aligned base address. May be changed by AlignBy.
        size_t base_size_ = 0u;       // Length of mapping. May be changed by RemapAtEnd (ie Zygote).
        int prot_ = 0;                // Protection of the map.
        // When reuse_ is true, this is just a view of an existing mapping
        // and we do not take ownership and are not responsible for
        // unmapping.
        bool reuse_ = false;
        // When already_unmapped_ is true the destructor will not call munmap.
        bool already_unmapped_ = false;
        size_t redzone_size_ = 0u;
    };


    template<typename T>
    constexpr int CTZ(T x) {
        static_assert(std::is_integral_v<T>, "T must be integral");
        // It is not unreasonable to ask for trailing zeros in a negative number. As such, do not check
        // that T is an unsigned type.
        static_assert(sizeof(T) == sizeof(uint64_t) || sizeof(T) <= sizeof(uint32_t),
                      "Unsupported sizeof(T)");
        return (sizeof(T) == sizeof(uint64_t)) ? __builtin_ctzll(x) : __builtin_ctz(x);
    }
    template<size_t kAlignment>
    class SpaceBitmap {
    public:
        MemMap mem_map_;
        // This bitmap itself, word sized for efficiency in scanning.
        std::atomic <uintptr_t> *bitmap_begin_ = nullptr;
        // Size of this bitmap.
        size_t bitmap_size_ = 0u;
        // The start address of the memory covered by the bitmap, which corresponds to the word
        // containing the first bit in the bitmap.
        uintptr_t heap_begin_ = 0u;
        // The end address of the memory covered by the bitmap. This may not be on a word boundary.
        uintptr_t heap_limit_ = 0u;
        // Name of this bitmap.
        std::string name_;
        static constexpr size_t OffsetToIndex(size_t offset) {
            return offset / kAlignment / kBitsPerIntPtrT;
        }
        template<typename T>
        static constexpr T IndexToOffset(T index) {
            return static_cast<T>(index * kAlignment * kBitsPerIntPtrT);
        }

        void VisitMarkedRange(uintptr_t visit_begin,uintptr_t visit_end,std::vector<void*>& result,bool kVisitOnce = false) {
            //DCHECK_LE(visit_begin, visit_end);
            //DCHECK_LE(heap_begin_, visit_begin);
            //DCHECK_LE(visit_end, HeapLimit());
            const uintptr_t offset_start = visit_begin - heap_begin_;
            const uintptr_t offset_end = visit_end - heap_begin_;
            const uintptr_t index_start = OffsetToIndex(offset_start);
            const uintptr_t index_end = OffsetToIndex(offset_end);
            const size_t bit_start = (offset_start / kAlignment) % kBitsPerIntPtrT;
            const size_t bit_end = (offset_end / kAlignment) % kBitsPerIntPtrT;
            // Index(begin)  ...    Index(end)
            // [xxxxx???][........][????yyyy]
            //      ^                   ^
            //      |                   #---- Bit of visit_end
            //      #---- Bit of visit_begin
            //
            // Left edge.
            uintptr_t left_edge = bitmap_begin_[index_start];
            // Mark of lower bits that are not in range.
            left_edge &= ~((static_cast<uintptr_t>(1) << bit_start) - 1);
            // Right edge. Either unique, or left_edge.
            uintptr_t right_edge;
            if (index_start < index_end) {
                // Left edge != right edge.
                // Traverse left edge.
                if (left_edge != 0) {
                    const uintptr_t ptr_base = IndexToOffset(index_start) + heap_begin_;
                    do {
                        const size_t shift = CTZ(left_edge);
                        auto obj = reinterpret_cast<void*>(ptr_base + shift * kAlignment);//mirror::Object*
                        result.push_back(obj);
                        if (kVisitOnce) {
                            return;
                        }
                        left_edge ^= (static_cast<uintptr_t>(1)) << shift;
                    } while (left_edge != 0);
                }
                // Traverse the middle, full part.
                for (size_t i = index_start + 1; i < index_end; ++i) {
                    uintptr_t w = bitmap_begin_[i].load(std::memory_order_relaxed);
                    if (w != 0) {
                        const uintptr_t ptr_base = IndexToOffset(i) + heap_begin_;
                        // Iterate on the bits set in word `w`, from the least to the most significant bit.
                        do {
                            const size_t shift = CTZ(w);
                            auto obj = reinterpret_cast<void*>(ptr_base + shift * kAlignment);//mirror::Object*
                            result.push_back(obj);
                            if (kVisitOnce) {
                                return;
                            }
                            w ^= (static_cast<uintptr_t>(1)) << shift;
                        } while (w != 0);
                    }
                }
                // Right edge is unique.
                // But maybe we don't have anything to do: visit_end starts in a new word...
                if (bit_end == 0) {
                    // Do not read memory, as it could be after the end of the bitmap.
                    right_edge = 0;
                } else {
                    right_edge = bitmap_begin_[index_end];
                }
            } else {
                // Right edge = left edge.
                right_edge = left_edge;
            }
            // Right edge handling.
            right_edge &= ((static_cast<uintptr_t>(1) << bit_end) - 1);
            if (right_edge != 0) {
                const uintptr_t ptr_base = IndexToOffset(index_end) + heap_begin_;
                // Iterate on the bits set in word `right_edge`, from the least to the most significant bit.
                do {
                    const size_t shift = CTZ(right_edge);
                    auto obj = reinterpret_cast<void*>(ptr_base + shift * kAlignment);//mirror::Object*
                    result.push_back(obj);
                    if (kVisitOnce) {
                        return;
                    }
                    right_edge ^= (static_cast<uintptr_t>(1)) << shift;
                } while (right_edge != 0);
            }
        }

    };

    class AtomicStack {
    public:
        // Name of the mark stack.
        std::string name_;
        // Memory mapping of the atomic stack.
        MemMap mem_map_;
        // Back index (index after the last element pushed).
        int32_t back_index_;//AtomicInteger
        // Front index, used for implementing PopFront.
        int32_t front_index_;//AtomicInteger
        // Base of the atomic stack.
        void* begin_;//StackReference<T>*
        // Current maximum which we can push back to, must be <= capacity_.
        size_t growth_limit_;
        // Maximum number of elements.
        size_t capacity_;
        // Whether or not the stack is sorted, only updated in debug mode to avoid performance overhead.
        bool debug_is_sorted_;
    };

    enum GcRetentionPolicy {
        // Objects are retained forever with this policy for a space.
        kGcRetentionPolicyNeverCollect,
        // Every GC cycle will attempt to collect objects in this space.
        kGcRetentionPolicyAlwaysCollect,
        // Objects will be considered for collection only in "full" GC cycles, ie faster partial
        // collections won't scan these areas such as the Zygote.
        kGcRetentionPolicyFullCollect,
    };
    class Space {
        // 成员变量
        std::string name_;               // 空间名称
        GcRetentionPolicy gc_retention_policy_;  // GC回收策略
        virtual void Dump(std::ostream& os) const;//确保开头有虚函数表
    };


    class ContinuousSpace : public Space {
    protected:

        // 成员变量
        uint8_t* begin_;          // 起始地址
        uint8_t* end_;   // 当前结束地址（原子变量）
        uint8_t* limit_;          // 最大限制地址
        uint8_t* Begin() const {
            return begin_;
        }
        // Current address at which the space ends, which may vary as the space is filled.
        uint8_t* End() const {
            return end_;
        }
    };

    class MemMapSpace : public ContinuousSpace {
        MemMap mem_map_;
        virtual void Dump2(std::ostream& os) const;//pad
    };

    class AllocSpace {
    public:
        // Number of bytes currently allocated.
        virtual uint64_t GetBytesAllocated() = 0;
    };

    using ContinuousSpaceBitmap = accounting::SpaceBitmap<kObjectAlignment>;
    using LargeObjectBitmap = accounting::SpaceBitmap<kMinPageSize>;

    class ContinuousMemMapAllocSpace : public MemMapSpace, public AllocSpace {
        virtual void Dump3(std::ostream& os) const;//pad
    public:
        ContinuousSpaceBitmap live_bitmap_;
        ContinuousSpaceBitmap mark_bitmap_;
        ContinuousSpaceBitmap temp_bitmap_;
    };

    class BaseMutex {
        virtual bool IsMutex() const { return false; }
    protected:
        const char* const name_;

        struct ContentionLogEntry {
            ContentionLogEntry() : blocked_tid(0), owner_tid(0) {}
            uint64_t blocked_tid;
            uint64_t owner_tid;
            int32_t count;//AtomicInteger
        };

        struct ContentionLogData {
            ContentionLogEntry contention_log[4];//constexpr size_t kContentionLogSize = 4;
            int32_t cur_content_log_entry;
            int32_t contention_count;
            uint64_t wait_time;//Atomic
            ContentionLogData() : wait_time(0) {}
        };

        ContentionLogData contention_log_data_[0];//constexpr size_t kContentionLogDataSize = kLogLockContentions ? 1 : 0;
        const uint8_t level_;//pad LockLevel
        bool should_respond_to_empty_checkpoint_request_;
    };
    class Mutex : public BaseMutex {
    private:
        int32_t state_and_contenders_;//AtomicInteger
        pid_t exclusive_owner_;//Atomic<pid_t>
        unsigned int recursion_count_;
        const bool recursive_;
        bool enable_monitor_timeout_ = false;
        uint32_t monitor_id_;
    };


    bool IsLikelyValidObject(void* obj) {//mirror::Object
        if (obj == nullptr) return false;

        uintptr_t* raw = reinterpret_cast<uintptr_t*>(obj);
        uintptr_t klass_ptr = raw[0];  // klass_ 通常是第一个字段

        // 检查 klass_ 是否非空、对齐
        if (klass_ptr == 0 || klass_ptr % kObjectAlignment != 0) {
            return false;
        }

        return true;
    }

    inline uintptr_t RoundDown(uintptr_t x, size_t n) {
        return x & ~(n - 1);
    }

    template<typename T>
    constexpr T RoundUp(T x, std::remove_reference_t<T> n) {
        return RoundDown(x + n - 1, n);
    }

    bool isValidObj(void* obj){
        return *(uint32_t*)obj > 0x70000000;
    }

    class BumpPointerSpace final : public ContinuousMemMapAllocSpace{
    public:
        static constexpr size_t kAlignment = kObjectAlignment;
        // 成员变量
        uint8_t* growth_end_;
        int32_t objects_allocated_;       // Accumulated from revoked thread local regions.
        int32_t bytes_allocated_;         // Accumulated from revoked thread local regions.
        Mutex lock_;
        size_t main_block_size_ ;
        std::deque<size_t> block_sizes_ ;
        size_t black_dense_region_size_ = 0;

        inline void* GetNextObject(void* obj) {//mirror::Object
            const uintptr_t position = reinterpret_cast<uintptr_t>(obj) + 8;
            return reinterpret_cast<void*>(RoundUp(position, kAlignment));//mirror::Object
        }

        inline void Walk(std::vector<void*>& result) {
            uint8_t* pos = Begin();
            uint8_t* end = End();
            uint8_t* main_end = pos;
            size_t black_dense_size;
            std::unique_ptr<std::vector<size_t>> block_sizes_copy;
            // Internal indirection w/ NO_THREAD_SAFETY_ANALYSIS. Optimally, we'd like to have an annotation
            // like
            //   REQUIRES_AS(visitor.operator(mirror::Object*))
            // on Walk to expose the interprocedural nature of locks here without having to duplicate the
            // function.
            //
            // NO_THREAD_SAFETY_ANALYSIS is a workaround. The problem with the workaround of course is that
            // it doesn't complain at the callsite. However, that is strictly not worse than the
            // ObjectCallback version it replaces.
            auto no_thread_safety_analysis_visit = [&](void* obj){
                result.push_back(obj);
            };
            {
                //MutexLock mu(Thread::Current(), lock_);
                // If we have 0 blocks then we need to update the main header since we have bump pointer style
                // allocation into an unbounded region (actually bounded by Capacity()).
                if (block_sizes_.empty()) {
                    //   UpdateMainBlock();
                }
                main_end = Begin() + main_block_size_;
                if (block_sizes_.empty()) {
                    // We don't have any other blocks, this means someone else may be allocating into the main
                    // block. In this case, we don't want to try and visit the other blocks after the main block
                    // since these could actually be part of the main block.
                    end = main_end;
                } else {
                    block_sizes_copy.reset(new std::vector<size_t>(block_sizes_.begin(), block_sizes_.end()));
                }
                black_dense_size = black_dense_region_size_;
            }
            // black_dense_region_size_ will be non-zero only in case of moving-space of CMC GC.
            if (black_dense_size > 0) {
                // Objects are not packed in this case, and therefore the bitmap is needed
                // to walk this part of the space.
                // Remember the last object visited using bitmap to be able to fetch its size.
                void* last_obj = nullptr;
                auto back1 = result.back();
                this->mark_bitmap_.VisitMarkedRange(reinterpret_cast<uintptr_t>(pos),
                                                    reinterpret_cast<uintptr_t>(pos + black_dense_size),
                                                    result, false);
                last_obj = result.back() != back1 ? result.back() : nullptr;
                pos += black_dense_size;
                if (last_obj != nullptr) {
                    // If the last object visited using bitmap was large enough to go past the
                    // black-dense region, then we need to adjust for that to be able to visit
                    // objects one after the other below.
                    pos = std::max(pos, reinterpret_cast<uint8_t*>(GetNextObject(last_obj)));
                }
            }
            // Walk all of the objects in the main block first.
            while (pos < main_end) {
                void* obj = reinterpret_cast<void*>(pos);
                // No read barrier because obj may not be a valid object.
                if (!isValidObj(obj)) {
                    // There is a race condition where a thread has just allocated an object but not set the
                    // class. We can't know the size of this object, so we don't visit it and break the loop
                    pos = main_end;
                    break;
                } else {
                    no_thread_safety_analysis_visit(obj);
                    pos = reinterpret_cast<uint8_t*>(GetNextObject(obj));
                }
            }
            // Walk the other blocks (currently only TLABs).
            if (block_sizes_copy != nullptr) {
                size_t iter = 0;
                size_t num_blks = block_sizes_copy->size();
                // Skip blocks which are already visited above as part of black-dense region.
                for (uint8_t* ptr = main_end; iter < num_blks; iter++) {
                    size_t block_size = (*block_sizes_copy)[iter];
                    ptr += block_size;
                    if (ptr > pos) {
                        // Adjust block-size in case 'pos' is in the middle of the block.
                        if (static_cast<ssize_t>(block_size) > ptr - pos) {
                            (*block_sizes_copy)[iter] = ptr - pos;
                        }
                        break;
                    }
                }
                for (; iter < num_blks; iter++) {
                    size_t block_size = (*block_sizes_copy)[iter];
                    void* obj = reinterpret_cast<void*>(pos);
                    const void* end_obj = reinterpret_cast<void*>(pos + block_size);
                    if (end_obj >= End()) return;
                    // We don't know how many objects are allocated in the current block. When we hit a null class
                    // assume it's the end. TODO: Have a thread update the header when it flushes the block?
                    // No read barrier because obj may not be a valid object.
                    while (obj < end_obj && isValidObj(obj)) {
                        no_thread_safety_analysis_visit(obj);
                        obj = GetNextObject(obj);
                    }
                    pos += block_size;
                }
            }
        }

    };
    class Region{

    };
    class RegionSpace{
        char pad[0x280];
        const bool use_generational_cc_;
        uint32_t time_;                  // The time as the number of collections since the startup.
        size_t num_regions_;             // The number of regions in this space.
        uint64_t madvise_time_;          // The amount of time spent in madvise for purging pages.
        size_t num_non_free_regions_ ;
        size_t num_evac_regions_;
        size_t max_peak_num_non_free_regions_;
        std::unique_ptr<Region[]> regions_;
        std::multimap<size_t, Region*, std::greater<size_t>> partial_tlabs_;
        size_t non_free_region_index_limit_;
        Region* current_region_;         // The region currently used for allocation.
        Region* evac_region_;            // The region currently used for evacuation.
        Region full_region_;             // The fake/sentinel region that looks full.
        size_t cyclic_alloc_region_index_;
        accounting::ContinuousSpaceBitmap mark_bitmap_;//offset: 0x2f0
        /*
        template<bool kToSpaceOnly, typename Visitor>
        inline void WalkInternal(Visitor&& visitor) {
            for (size_t i = 0; i < num_regions_; ++i) {
                Region* r = &regions_[i];
                if (r->IsFree() || (kToSpaceOnly && !r->IsInToSpace())) {
                    continue;
                }
                if (r->IsLarge()) {
                    // We may visit a large object with live_bytes = 0 here. However, it is
                    // safe as it cannot contain dangling pointers because corresponding regions
                    // (and regions corresponding to dead referents) cannot be allocated for new
                    // allocations without first clearing regions' live_bytes and state.
                    void* obj = reinterpret_cast<void*>(r->Begin());//mirror::Object*
                    //visitor(obj);
                } else if (r->IsLargeTail()) {
                    // Do nothing.
                } else {
                    WalkNonLargeRegion(visitor, r);
                }
            }
        }*/
    };

    enum VerifyObjectMode {
        kVerifyObjectModeDisabled,  // Heap verification is disabled.
        kVerifyObjectModeFast,  // Check heap accesses quickly by using VerifyClassClass.
        kVerifyObjectModeAll  // Check heap accesses thoroughly.
    };
    struct padFor_blockGc{
        // Region space, used by the concurrent collector.
        void* region_space_;
        // Minimum free guarantees that you always have at least min_free_ free bytes after growing for
        // utilization, regardless of target utilization ratio.
        const size_t min_free_;
        // The ideal maximum free size, when we grow the heap for utilization.
        const size_t max_free_;
        // Target ideal heap utilization ratio.
        double target_utilization_;
        // How much more we grow the heap when we are a foreground app instead of background.
        double foreground_heap_growth_multiplier_;
        // The amount of native memory allocation since the last GC required to cause us to wait for a
        // collection as a result of native allocation. Very large values can cause the device to run
        // out of memory, due to lack of finalization to reclaim native memory.  Making it too small can
        // cause jank in apps like launcher that intentionally allocate large amounts of memory in rapid
        // succession. (b/122099093) 1/4 to 1/3 of physical memory seems to be a good number.
        const size_t stop_for_native_allocs_;
        // Total time which mutators are paused or waiting for GC to complete.
        uint64_t total_wait_time_;
        // The current state of heap verification, may be enabled or disabled.
        VerifyObjectMode verify_object_mode_;
        // Compacting GC disable count, prevents compacting GC from running iff > 0.
        size_t disable_moving_gc_count_;
        std::vector<void*> garbage_collectors_;
        void* semi_space_collector_;
        void* mark_compact_;
        void* active_concurrent_copying_collector_;
        void* young_concurrent_copying_collector_;
        void* concurrent_copying_collector_;
        const bool is_running_on_memory_tool_;
        const bool use_tlab_;
        // Pointer to the space which becomes the new main space when we do homogeneous space compaction.
        // Use unique_ptr since the space is only added during the homogeneous compaction phase.
        void* main_space_backup_;//std::unique_ptr<space::MallocSpace> main_space_backup_;
        // Minimal interval allowed between two homogeneous space compactions caused by OOM.
        uint64_t min_interval_homogeneous_space_compaction_by_oom_;
        // Times of the last homogeneous space compaction caused by OOM.
        uint64_t last_time_homogeneous_space_compaction_by_oom_;
        // Saved OOMs by homogeneous space compaction.
        size_t count_delayed_oom_;
        // Count for requested homogeneous space compaction.
        size_t count_requested_homogeneous_space_compaction_;
        // Count for ignored homogeneous space compaction.
        size_t count_ignored_homogeneous_space_compaction_;
        // Count for performed homogeneous space compaction.
        size_t count_performed_homogeneous_space_compaction_;
        // The number of garbage collections (either young or full, not trims or the like) we have
        // completed since heap creation. We include requests that turned out to be impossible
        // because they were disabled. We guard against wrapping, though that's unlikely.
        // Increment is guarded by gc_complete_lock_.
        uint32_t gcs_completed_;
        // The number of the last garbage collection that has been requested.  A value of gcs_completed
        // + 1 indicates that another collection is needed or in progress. A value of gcs_completed_ or
        // (logically) less means that no new GC has been requested.
        uint32_t max_gc_requested_;
        // Active tasks which we can modify (change target time, desired collector type, etc..).
        void* pending_collector_transition_ ;
        void* pending_heap_trim_;
        // Whether or not we use homogeneous space compaction to avoid OOM errors.
        bool use_homogeneous_space_compaction_for_oom_;
        // If true, enable generational collection when using the Concurrent Copying
        // (CC) collector, i.e. use sticky-bit CC for minor collections and (full) CC
        // for major collections. Set in Heap constructor.
        const bool use_generational_cc_;
        // True if the currently running collection has made some thread wait.
        bool running_collection_is_blocking_;
        // The number of blocking GC runs.
        uint64_t blocking_gc_count_;
    };
}


sol::table javaarray_to_luatable(sol::this_state ts, jobject arrObj);
std::string safe_to_string(const sol::object& obj);

std::string dump_table(const sol::table& tbl) {
    std::string out = "{ ";
    for (auto& pair : tbl) {
        std::string key = safe_to_string(pair.first);
        std::string val = safe_to_string(pair.second);
        out += key + " = " + val + ", ";
    }
    out += "}";
    return out;
}


std::string safe_to_string(const sol::object& obj) {
    switch (obj.get_type()) {
        case sol::type::string:
            return obj.as<std::string>();
        case sol::type::number:
            return std::to_string(obj.as<double>());
        case sol::type::boolean:
            return obj.as<bool>() ? "true" : "false";
        case sol::type::nil:
            return "nil";
        case sol::type::userdata:
            return "<userdata>";
        case sol::type::table:
            return dump_table(obj);
        case sol::type::function:
            return "<function>";
        default:
            return "<unknown>";
    }
}


void WRAP_C_LUA_FUNCTION::LUA_LOG(sol::this_state ts, sol::variadic_args args) {
    sol::state_view lua(ts);
    std::string final;

    for (auto&& arg : args) {
        std::string str;
        switch (arg.get_type()) {
            case sol::type::string:
                str = arg.as<std::string>();
                break;
            case sol::type::number:
                str = std::to_string(arg.as<double>());
                break;
            case sol::type::boolean:
                str = arg.as<bool>() ? "true" : "false";
                break;
            case sol::type::nil:
                str = "nil";
                break;
            case sol::type::table:
                str = dump_table(arg.as<sol::table>());
                break;
            case sol::type::userdata:
                str = "<userdata>";
                break;
            case sol::type::function:
                str = "<function>";
                break;
            default:
                str = "<unknown>";
                break;
        }
        final += str + " ";
    }
    auto mylog = "[Lua]" + final;
    Commands::tcp_log(mylog);
}

sol::table jobject_to_luatable(sol::this_state ts, jobject obj){
    JavaEnv MyEnv;
    JNIEnv* env = MyEnv.get();
    sol::table tbl = LUA::lua->create_table();
    // 获取类对象
    jclass objClass = env->GetObjectClass(obj);

    // 获取 java.lang.Class
    jmethodID mid_getClass = env->GetMethodID(objClass, "getClass", "()Ljava/lang/Class;");
    jobject classObj = env->CallObjectMethod(obj, mid_getClass);

    // 获取字段数组
    jclass classClass = env->FindClass("java/lang/Class");
    jmethodID mid_getDeclaredFields = env->GetMethodID(classClass, "getDeclaredFields", "()[Ljava/lang/reflect/Field;");

    jmethodID isArrayMethod = env->GetMethodID(classClass, "isArray", "()Z");


    auto fieldArray = (jobjectArray) env->CallObjectMethod(classObj, mid_getDeclaredFields);

    jsize fieldCount = env->GetArrayLength(fieldArray);

    jclass fieldClass = env->FindClass("java/lang/reflect/Field");
    jmethodID mid_getName = env->GetMethodID(fieldClass, "getName", "()Ljava/lang/String;");
    jmethodID mid_getType = env->GetMethodID(fieldClass, "getType", "()Ljava/lang/Class;");
    jmethodID mid_get = env->GetMethodID(fieldClass, "get", "(Ljava/lang/Object;)Ljava/lang/Object;");
    jmethodID mid_setAccessible = env->GetMethodID(fieldClass, "setAccessible", "(Z)V");

    jclass booleanClass = env->FindClass("java/lang/Boolean");
    jmethodID mid_boolean_value = env->GetMethodID(booleanClass, "booleanValue", "()Z");

    jclass integerClass = env->FindClass("java/lang/Integer");
    jmethodID mid_int_value = env->GetMethodID(integerClass, "intValue", "()I");

    jclass floatClass = env->FindClass("java/lang/Float");
    jmethodID mid_float_value = env->GetMethodID(floatClass, "floatValue", "()F");

    jclass doubleClass = env->FindClass("java/lang/Double");
    jmethodID mid_double_value = env->GetMethodID(doubleClass, "doubleValue", "()D");

    jclass longClass = env->FindClass("java/lang/Long");
    jmethodID mid_long_value = env->GetMethodID(longClass, "longValue", "()J");

    jclass shortClass = env->FindClass("java/lang/Short");
    jmethodID mid_short_value = env->GetMethodID(shortClass, "shortValue", "()S");

    jclass charClass = env->FindClass("java/lang/Character");
    jmethodID  mid_char_value = env->GetMethodID(charClass, "charValue", "()C");

    jclass byteClass = env->FindClass("java/lang/Byte");
    jmethodID mid_byte_value = env->GetMethodID(byteClass, "byteValue", "()B");

    for (jsize i = 0; i < fieldCount; ++i) {
        jobject field = env->GetObjectArrayElement(fieldArray, i);

        // 让私有字段也可以访问
        env->CallVoidMethod(field, mid_setAccessible, JNI_TRUE);

        // 获取字段名
        auto nameStr = (jstring) env->CallObjectMethod(field, mid_getName);
        const char *name = env->GetStringUTFChars(nameStr, nullptr);

        // 获取字段值
        jobject valueObj = env->CallObjectMethod(field, mid_get, obj);

        if (valueObj != nullptr) {
            jclass valueClass = env->GetObjectClass(valueObj);

            if (env->IsInstanceOf(valueObj, integerClass)) {
                jint val = env->CallIntMethod(valueObj, mid_int_value);
                tbl[name] = val;
            }  else if (env->IsInstanceOf(valueObj, longClass)) {
                jlong val = env->CallLongMethod(valueObj, mid_long_value);
                tbl[name] = (int64_t)val;  // sol2 支持 int64_t
            }
            else if (env->IsInstanceOf(valueObj, shortClass)){
                jshort val = env->CallShortMethod(valueObj, mid_short_value);
                tbl[name] = val;
            }
            else if (env->IsInstanceOf(valueObj, charClass)){
                char val = env->CallCharMethod(valueObj, mid_char_value);
                tbl[name] = val;
            }
            else if (env->IsInstanceOf(valueObj, byteClass)){
                jbyte val = env->CallByteMethod(valueObj, mid_byte_value);
                tbl[name] = val;
            }
            else if (env->IsInstanceOf(valueObj, floatClass)) {
                jfloat val = env->CallFloatMethod(valueObj, mid_float_value);
                tbl[name] = val;
            } else if (env->IsInstanceOf(valueObj, doubleClass)) {
                jdouble val = env->CallDoubleMethod(valueObj, mid_double_value);
                tbl[name] = val;
            } else if (env->IsInstanceOf(valueObj, booleanClass)) {
                jboolean val = env->CallBooleanMethod(valueObj, mid_boolean_value);
                tbl[name] = (bool) val;
            } else if (env->IsInstanceOf(valueObj, env->FindClass("java/lang/String"))) {
                const char *str = env->GetStringUTFChars((jstring) valueObj, nullptr);
                tbl[name] = std::string(str);
                env->ReleaseStringUTFChars((jstring) valueObj, str);
            } else if (env->CallBooleanMethod(valueClass, isArrayMethod)){
                //这里也只是一层，并不会递归的
                tbl[name] = (int64_t)valueObj;//javaarray_to_luatable(ts, valueObj);
            }else {
                // 其他对象
                //不用管，这里不进行递归处理
                tbl[name] = (int64_t)valueObj;
            }
        } else {
            tbl[name] = sol::nil;
        }

        env->ReleaseStringUTFChars(nameStr, name);
        env->DeleteLocalRef(field);
    }

    return tbl;
}
sol::table WRAP_C_LUA_FUNCTION::jobject_to_luatable_trampoline(sol::this_state ts, sol::variadic_args args) {
    uint64_t objPtr = args[0].as<uint64_t>();
    jobject obj = (jobject)objPtr;
    return jobject_to_luatable(ts, obj);
}

sol::table javaarray_to_luatable(sol::this_state ts, jobject arrObj) {
    JavaEnv MyEnv;
    JNIEnv* env = MyEnv.get();
    sol::state_view lua(ts);
    sol::table tbl = lua.create_table();

    if (arrObj == nullptr) {
        return tbl;  // 空表
    }

    // 获取数组长度
    jsize arrLen = env->GetArrayLength((jarray)arrObj);

    // 判断数组元素类型，获取元素的Class
    jclass arrClass = env->GetObjectClass(arrObj);
    jclass classClass = env->FindClass("java/lang/Class");
    jmethodID mid_getComponentType = env->GetMethodID(classClass, "getComponentType", "()Ljava/lang/Class;");
    jobject compType = env->CallObjectMethod(arrClass, mid_getComponentType);

    jclass stringClass = env->FindClass("java/lang/String");

    // componentType 的类名判断
    jmethodID mid_getName = env->GetMethodID(classClass, "getName", "()Ljava/lang/String;");
    jstring compNameStr = (jstring)env->CallObjectMethod(compType, mid_getName);
    const char* compName = env->GetStringUTFChars(compNameStr, nullptr);

    for (jsize i = 0; i < arrLen; ++i) {
        // 依赖类型调用不同的 GetXXXArrayRegion 或 GetObjectArrayElement
        if (strcmp(compName, "int") == 0) {
            jint* elems = env->GetIntArrayElements((jintArray)arrObj, nullptr);
            tbl[i + 1] = elems[i];
            env->ReleaseIntArrayElements((jintArray)arrObj, elems, JNI_ABORT);
        } else if (strcmp(compName, "boolean") == 0) {
            jboolean* elems = env->GetBooleanArrayElements((jbooleanArray)arrObj, nullptr);
            tbl[i + 1] = elems[i] != 0;
            env->ReleaseBooleanArrayElements((jbooleanArray)arrObj, elems, JNI_ABORT);
        } else if (strcmp(compName, "float") == 0) {
            jfloat* elems = env->GetFloatArrayElements((jfloatArray)arrObj, nullptr);
            tbl[i + 1] = elems[i];
            env->ReleaseFloatArrayElements((jfloatArray)arrObj, elems, JNI_ABORT);
        } else if (strcmp(compName, "double") == 0) {
            jdouble* elems = env->GetDoubleArrayElements((jdoubleArray)arrObj, nullptr);
            tbl[i + 1] = elems[i];
            env->ReleaseDoubleArrayElements((jdoubleArray)arrObj, elems, JNI_ABORT);
        } else if (strcmp(compName, "long") == 0) {
            jlong* elems = env->GetLongArrayElements((jlongArray)arrObj, nullptr);
            tbl[i + 1] = (int64_t)elems[i];  // 转成 lua number，注意精度问题
            env->ReleaseLongArrayElements((jlongArray)arrObj, elems, JNI_ABORT);
        }
        else if (strcmp(compName, "short") == 0) {
            jshort* elems = env->GetShortArrayElements((jshortArray)arrObj, nullptr);
            tbl[i + 1] = elems[i];
            env->ReleaseShortArrayElements((jshortArray)arrObj, elems, JNI_ABORT);
        }
        else if (strcmp(compName, "char") == 0) {
            jchar* elems = env->GetCharArrayElements((jcharArray)arrObj, nullptr);
            tbl[i + 1] = elems[i];
            env->ReleaseCharArrayElements((jcharArray)arrObj, elems, JNI_ABORT);
        }
        else if (strcmp(compName, "byte") == 0) {
            jbyte* elems = env->GetByteArrayElements((jbyteArray)arrObj, nullptr);
            tbl[i + 1] = elems[i];
            env->ReleaseByteArrayElements((jbyteArray)arrObj, elems, JNI_ABORT);
        }else {
            // 对象数组
            jobject elemObj = env->GetObjectArrayElement((jobjectArray)arrObj, i);
            if (elemObj == nullptr) {
                tbl[i + 1] = sol::nil;
            } else if (env->IsInstanceOf(elemObj, stringClass)) {
                const char* str = env->GetStringUTFChars((jstring)elemObj, nullptr);
                tbl[i + 1] = std::string(str);
                env->ReleaseStringUTFChars((jstring)elemObj, str);
            } else {
                // 不进行递归处理 仅保留地址 用户需要就让用户自己处理
                //用户处理完了，直接对这个地址应用就行了
                //递归需要考虑太多情况，耗费性能高容易出错
                tbl[i + 1] = (int64_t)elemObj;
            }
            //不要delete delete了就废了 后面用户没法操作了
            //env->DeleteLocalRef(elemObj);
        }
    }

    env->ReleaseStringUTFChars(compNameStr, compName);
    env->DeleteLocalRef(compNameStr);
    env->DeleteLocalRef(compType);
    env->DeleteLocalRef(arrClass);

    return tbl;
}

sol::table WRAP_C_LUA_FUNCTION::javaarray_to_luatable_trampoline(sol::this_state ts, sol::variadic_args args) {
    uint64_t objPtr = args[0].as<uint64_t>();
    jobject obj = (jobject)objPtr;
    return javaarray_to_luatable(ts, obj);
}

void apply_soltable_to_existing_jobject(sol::this_state ts, sol::table tbl, jobject obj){
    JavaEnv MyEnv;
    JNIEnv* env = MyEnv.get();
    // 获取类对象
    jclass objClass = env->GetObjectClass(obj);

    // 获取 java.lang.Class
    jmethodID mid_getClass = env->GetMethodID(objClass, "getClass", "()Ljava/lang/Class;");
    jobject classObj = env->CallObjectMethod(obj, mid_getClass);

    // 获取字段数组
    jclass classClass = env->FindClass("java/lang/Class");
    jmethodID mid_getDeclaredFields = env->GetMethodID(classClass, "getDeclaredFields", "()[Ljava/lang/reflect/Field;");

    jmethodID isArrayMethod = env->GetMethodID(classClass, "isArray", "()Z");


    auto fieldArray = (jobjectArray) env->CallObjectMethod(classObj, mid_getDeclaredFields);

    jsize fieldCount = env->GetArrayLength(fieldArray);

    jclass fieldClass = env->FindClass("java/lang/reflect/Field");
    jmethodID mid_getName = env->GetMethodID(fieldClass, "getName", "()Ljava/lang/String;");
    jmethodID mid_getType = env->GetMethodID(fieldClass, "getType", "()Ljava/lang/Class;");
    jmethodID mid_get = env->GetMethodID(fieldClass, "get", "(Ljava/lang/Object;)Ljava/lang/Object;");
    jmethodID mid_setAccessible = env->GetMethodID(fieldClass, "setAccessible", "(Z)V");

    jclass booleanClass = env->FindClass("java/lang/Boolean");
    jmethodID mid_boolean_value = env->GetMethodID(booleanClass, "booleanValue", "()Z");

    jclass integerClass = env->FindClass("java/lang/Integer");
    jmethodID mid_int_value = env->GetMethodID(integerClass, "intValue", "()I");

    jclass floatClass = env->FindClass("java/lang/Float");
    jmethodID mid_float_value = env->GetMethodID(floatClass, "floatValue", "()F");

    jclass doubleClass = env->FindClass("java/lang/Double");
    jmethodID mid_double_value = env->GetMethodID(doubleClass, "doubleValue", "()D");

    jclass longClass = env->FindClass("java/lang/Long");
    jmethodID mid_long_value = env->GetMethodID(longClass, "longValue", "()J");

    jclass shortClass = env->FindClass("java/lang/Short");
    jmethodID mid_short_value = env->GetMethodID(shortClass, "shortValue", "()S");

    jclass charClass = env->FindClass("java/lang/Character");
    jmethodID  mid_char_value = env->GetMethodID(charClass, "charValue", "()C");

    jclass byteClass = env->FindClass("java/lang/Byte");
    jmethodID mid_byte_value = env->GetMethodID(byteClass, "byteValue", "()B");



    for (jsize i = 0; i < fieldCount; ++i) {
        jobject field = env->GetObjectArrayElement(fieldArray, i);

        // 让私有字段也可以访问
        env->CallVoidMethod(field, mid_setAccessible, JNI_TRUE);

        // 获取字段名
        auto nameStr = (jstring) env->CallObjectMethod(field, mid_getName);
        const char *name = env->GetStringUTFChars(nameStr, nullptr);

        // 获取字段值
        jobject valueObj = env->CallObjectMethod(field, mid_get, obj);

        if (valueObj != nullptr) {
            jclass valueClass = env->GetObjectClass(valueObj);

            if (env->IsInstanceOf(valueObj, integerClass)) {
                if (tbl[name].is<int>()) {
                    jfieldID fid = env->GetFieldID(objClass, name, "I");
                    env->SetIntField(obj, fid,tbl[name].get<int>());
                }
            }  else if (env->IsInstanceOf(valueObj, longClass)) {
                if (tbl[name].is<int64_t>()) {
                    jfieldID fid = env->GetFieldID(objClass, name, "J");
                    env->SetLongField(obj, fid,tbl[name].get<int64_t>());
                }
            }
            else if (env->IsInstanceOf(valueObj, shortClass)){
                if (tbl[name].is<short>()) {
                    jfieldID fid = env->GetFieldID(objClass, name, "S");
                    env->SetShortField(obj, fid,tbl[name].get<short>());
                }
            }
            else if (env->IsInstanceOf(valueObj, charClass)){
                if (tbl[name].is<char>()) {
                    jfieldID fid = env->GetFieldID(objClass, name, "C");
                    env->SetCharField(obj, fid,tbl[name].get<char>());
                }
            }
            else if (env->IsInstanceOf(valueObj, byteClass)){
                if (tbl[name].is<int8_t>()) {
                    jfieldID fid = env->GetFieldID(objClass, name, "B");
                    env->SetByteField(obj, fid,tbl[name].get<int8_t>());
                }
            }
            else if (env->IsInstanceOf(valueObj, floatClass)) {
                if (tbl[name].is<float>()) {
                    jfieldID fid = env->GetFieldID(objClass, name, "F");
                    env->SetFloatField(obj, fid,tbl[name].get<float>());
                }
            } else if (env->IsInstanceOf(valueObj, doubleClass)) {
                if (tbl[name].is<double>()) {
                    jfieldID fid = env->GetFieldID(objClass, name, "D");
                    env->SetDoubleField(obj, fid,tbl[name].get<double>());
                }
            } else if (env->IsInstanceOf(valueObj, booleanClass)) {
                if (tbl[name].is<bool>()) {
                    jfieldID fid = env->GetFieldID(objClass, name, "Z");
                    env->SetBooleanField(obj, fid,tbl[name].get<bool>());
                }
            } else if (env->IsInstanceOf(valueObj, env->FindClass("java/lang/String"))) {
                if (tbl[name].is<std::string>()) {
                    jfieldID fid = env->GetFieldID(objClass, name, "Ljava/lang/String;");
                    std::string cppStr = tbl[name].get<std::string>();
                    jstring jstr = env->NewStringUTF(cppStr.c_str());
                    env->SetObjectField(obj, fid, jstr);
                    env->DeleteLocalRef(jstr);  // 推荐清理局部引用
                }
            }
            else if (env->CallBooleanMethod(valueClass, isArrayMethod)){
                //类套数组，不递归处理
                //tbl[name] = javaarray_to_luatable(ts, valueObj);
            }else {
                // 其他对象，不递归处理
                //tbl[name] = jobject_to_luatable(ts, valueObj);
            }
        }
        else {
            tbl[name] = sol::nil;
        }

        env->ReleaseStringUTFChars(nameStr, name);
        env->DeleteLocalRef(field);
    }

    return;
}

void WRAP_C_LUA_FUNCTION::apply_soltable_to_existing_jobject_trampoline(sol::this_state ts, sol::variadic_args args) {
    auto table = args[0].as<sol::table>();
    uint64_t obj = args[1].as<uint64_t>();
    apply_soltable_to_existing_jobject(ts, table, (jobject) obj);
}

void apply_soltable_to_existing_javaarray(sol::this_state ts,sol::table tbl, jobject arrObj) {
    JavaEnv MyEnv;
    JNIEnv *env = MyEnv.get();
    sol::state_view lua(ts);
    if (arrObj == nullptr) {
        return;  // 空表
    }
    jsize arrLen = env->GetArrayLength((jarray) arrObj);

    // 判断数组元素类型，获取元素的Class
    jclass arrClass = env->GetObjectClass(arrObj);
    jclass classClass = env->FindClass("java/lang/Class");
    jmethodID mid_getComponentType = env->GetMethodID(classClass, "getComponentType",
                                                      "()Ljava/lang/Class;");
    jobject compType = env->CallObjectMethod(arrClass, mid_getComponentType);

    jclass stringClass = env->FindClass("java/lang/String");

    // componentType 的类名判断
    jmethodID mid_getName = env->GetMethodID(classClass, "getName", "()Ljava/lang/String;");
    jstring compNameStr = (jstring) env->CallObjectMethod(compType, mid_getName);
    const char *compName = env->GetStringUTFChars(compNameStr, nullptr);

    // 依赖类型调用不同的 GetXXXArrayRegion 或 GetObjectArrayElement
    if (strcmp(compName, "int") == 0) {
        auto buffer = new int[tbl.size()];
        for (int index = 0; index < tbl.size(); index++){
            if (tbl[index + 1].is<int>())
                buffer[index] = tbl[index + 1].get<int>();
        }
        env->SetIntArrayRegion(((jintArray) arrObj),0, tbl.size(),buffer);
        delete[] buffer;
    } else if (strcmp(compName, "boolean") == 0) {
        auto* buffer = new jboolean[tbl.size()];
        for (int index = 0; index < tbl.size(); index++){
            if (tbl[index + 1].is<jboolean>())
                buffer[index] = tbl[index + 1].get<jboolean>();
        }
        env->SetBooleanArrayRegion(((jbooleanArray) arrObj),0, tbl.size(),buffer);
        delete[] buffer;

    } else if (strcmp(compName, "float") == 0) {
        auto* buffer = new float[tbl.size()];
        for (int index = 0; index < tbl.size(); index++){
            if (tbl[index + 1].is<float>())
                buffer[index] = tbl[index + 1].get<float>();
        }
        env->SetFloatArrayRegion(((jfloatArray) arrObj),0, tbl.size(),buffer);
        delete[] buffer;

    } else if (strcmp(compName, "double") == 0) {
        auto* buffer = new double[tbl.size()];
        for (int index = 0; index < tbl.size(); index++){
            if (tbl[index + 1].is<double>())
                buffer[index] = tbl[index + 1].get<double>();
        }
        env->SetDoubleArrayRegion(((jdoubleArray) arrObj),0, tbl.size(),buffer);
        delete[] buffer;
    } else if (strcmp(compName, "long") == 0) {
        auto* buffer = new int64_t[tbl.size()];
        for (int index = 0; index < tbl.size(); index++){
            if (tbl[index + 1].is<int64_t>())
                buffer[index] = tbl[index + 1].get<int64_t>();
        }
        env->SetLongArrayRegion(((jlongArray) arrObj),0, tbl.size(),buffer);
        delete[] buffer;
    } else if (strcmp(compName, "short") == 0) {
        auto buffer = new short[tbl.size()];
        for (int index = 0; index < tbl.size(); index++){
            if (tbl[index + 1].is<short>())
                buffer[index] = tbl[index + 1].get<short>();
        }
        env->SetShortArrayRegion(((jshortArray) arrObj),0, tbl.size(),buffer);
        delete[] buffer;
    } else if (strcmp(compName, "char") == 0) {
        auto buffer = new jchar[tbl.size()];
        for (int index = 0; index < tbl.size(); index++){
            if (tbl[index + 1].is<jchar>())
                buffer[index] = tbl[index + 1].get<jchar>();
        }
        env->SetCharArrayRegion(((jcharArray) arrObj),0, tbl.size(),buffer);
        delete[] buffer;
    } else if (strcmp(compName, "byte") == 0) {
        auto* buffer = new jbyte [tbl.size()];
        for (int index = 0; index < tbl.size(); index++){
            if (tbl[index + 1].is<jbyte>())
                buffer[index] = tbl[index + 1].get<jbyte>();
        }
        env->SetByteArrayRegion(((jbyteArray) arrObj),0, tbl.size(),buffer);
        delete[] buffer;
    } else {
        // 对象数组
        jobject elemObj = env->GetObjectArrayElement((jobjectArray)arrObj, 0);
        if (elemObj == nullptr) {
            //不知道，理论不会发生
        } else if (env->IsInstanceOf(elemObj, stringClass)) {
            //是String[]数组
            //String不可变，不能修改，只能替换
            for (int index = 0; index < tbl.size(); index++){
                if (tbl[index + 1].is<std::string>()){
                    jstring newJstr = env->NewStringUTF(tbl[index + 1].get<std::string>().c_str());
                    env->SetObjectArrayElement((jobjectArray)arrObj, index, newJstr);
                    env->DeleteLocalRef(newJstr);
                }
            }
        } else {
            // 调用结构体应用函数 注意，这里也是只应用了一层，并不会递归
            for (int index = 0; index < tbl.size(); index++){
                if (tbl[index + 1].is<sol::table>()){
                    jobject objElem =  env->GetObjectArrayElement((jobjectArray)arrObj, index);
                    apply_soltable_to_existing_jobject(ts,tbl[index + 1].get<sol::table>(),objElem);
                    env->SetObjectArrayElement((jobjectArray)arrObj, index, objElem);
                }
            }
        }
    }


    env->ReleaseStringUTFChars(compNameStr, compName);
    env->DeleteLocalRef(compNameStr);
    env->DeleteLocalRef(compType);
    env->DeleteLocalRef(arrClass);

    return;
}


void WRAP_C_LUA_FUNCTION::apply_soltable_to_existing_javaarray_trampoline(sol::this_state ts, sol::variadic_args args) {
    auto table = args[0].as<sol::table>();
    uint64_t obj = args[1].as<uint64_t>();
    apply_soltable_to_existing_javaarray(ts, table, (jobject) obj);
}

sol::table javalist_to_luatable(sol::this_state ts, jobject listObj) {
    JavaEnv MyEnv;
    JNIEnv* env = MyEnv.get();
    sol::state_view lua(ts);
    sol::table tbl = lua.create_table();

    if (listObj == nullptr) {
        return tbl;  // 空表
    }

    // 获取 List 接口和其方法
    jclass listClass = env->GetObjectClass(listObj);
    jmethodID mid_size = env->GetMethodID(listClass, "size", "()I");
    jmethodID mid_get = env->GetMethodID(listClass, "get", "(I)Ljava/lang/Object;");

    jint size = env->CallIntMethod(listObj, mid_size);
    jclass stringClass = env->FindClass("java/lang/String");

    jclass booleanClass = env->FindClass("java/lang/Boolean");
    jmethodID mid_boolean_value = env->GetMethodID(booleanClass, "booleanValue", "()Z");

    jclass integerClass = env->FindClass("java/lang/Integer");
    jmethodID mid_int_value = env->GetMethodID(integerClass, "intValue", "()I");

    jclass floatClass = env->FindClass("java/lang/Float");
    jmethodID mid_float_value = env->GetMethodID(floatClass, "floatValue", "()F");

    jclass doubleClass = env->FindClass("java/lang/Double");
    jmethodID mid_double_value = env->GetMethodID(doubleClass, "doubleValue", "()D");

    jclass longClass = env->FindClass("java/lang/Long");
    jmethodID mid_long_value = env->GetMethodID(longClass, "longValue", "()J");

    jclass shortClass = env->FindClass("java/lang/Short");
    jmethodID mid_short_value = env->GetMethodID(shortClass, "shortValue", "()S");

    jclass charClass = env->FindClass("java/lang/Character");
    jmethodID  mid_char_value = env->GetMethodID(charClass, "charValue", "()C");

    jclass byteClass = env->FindClass("java/lang/Byte");
    jmethodID mid_byte_value = env->GetMethodID(byteClass, "byteValue", "()B");

    jclass classClass = env->FindClass("java/lang/Class");
    jmethodID isArrayMethod = env->GetMethodID(classClass, "isArray", "()Z");

    for (jint i = 0; i < size; ++i) {
        jobject valueObj = env->CallObjectMethod(listObj, mid_get, i);

        if (valueObj != nullptr) {
            jclass valueClass = env->GetObjectClass(valueObj);

            if (env->IsInstanceOf(valueObj, integerClass)) {
                jint val = env->CallIntMethod(valueObj, mid_int_value);
                tbl[i+1] = val;
            }  else if (env->IsInstanceOf(valueObj, longClass)) {
                jlong val = env->CallLongMethod(valueObj, mid_long_value);
                tbl[i+1] = (int64_t)val;  // sol2 支持 int64_t
            }
            else if (env->IsInstanceOf(valueObj, shortClass)){
                jshort val = env->CallShortMethod(valueObj, mid_short_value);
                tbl[i+1] = val;
            }
            else if (env->IsInstanceOf(valueObj, charClass)){
                char val = env->CallCharMethod(valueObj, mid_char_value);
                tbl[i+1] = val;
            }
            else if (env->IsInstanceOf(valueObj, byteClass)){
                jbyte val = env->CallByteMethod(valueObj, mid_byte_value);
                tbl[i+1] = val;
            }
            else if (env->IsInstanceOf(valueObj, floatClass)) {
                jfloat val = env->CallFloatMethod(valueObj, mid_float_value);
                tbl[i+1] = val;
            } else if (env->IsInstanceOf(valueObj, doubleClass)) {
                jdouble val = env->CallDoubleMethod(valueObj, mid_double_value);
                tbl[i+1] = val;
            } else if (env->IsInstanceOf(valueObj, booleanClass)) {
                jboolean val = env->CallBooleanMethod(valueObj, mid_boolean_value);
                tbl[i+1] = (bool) val;
            } else if (env->IsInstanceOf(valueObj, env->FindClass("java/lang/String"))) {
                const char *str = env->GetStringUTFChars((jstring) valueObj, nullptr);
                tbl[i+1] = std::string(str);
                env->ReleaseStringUTFChars((jstring) valueObj, str);
            } else if (env->CallBooleanMethod(valueClass, isArrayMethod)){
                //这里也只是一层，并不会递归的
                tbl[i+1] = (int64_t)valueObj;//javaarray_to_luatable(ts, valueObj);
            }else {
                // 其他对象
                //不用管，这里不进行递归处理
                tbl[i+1] = (int64_t)valueObj;
            }
        } else {
            tbl[i+1] = sol::nil;
        }
    }

    env->DeleteLocalRef(listClass);
    return tbl;
}

sol::table WRAP_C_LUA_FUNCTION::javalist_to_luatable_trampoline(sol::this_state ts, sol::variadic_args args) {
    uint64_t objPtr = args[0].as<uint64_t>();
    jobject obj = (jobject)objPtr;
    return javalist_to_luatable(ts, obj);
}


void sync_soltable_to_javalist(sol::this_state ts, sol::table tbl, jobject listObj) {
    JavaEnv MyEnv;
    JNIEnv* env = MyEnv.get();
    sol::state_view lua(ts);

    if (listObj == nullptr) return;

    jclass listClass = env->GetObjectClass(listObj);
    jmethodID mid_size = env->GetMethodID(listClass, "size", "()I");
    jmethodID mid_get = env->GetMethodID(listClass, "get", "(I)Ljava/lang/Object;");
    jmethodID mid_set = env->GetMethodID(listClass, "set", "(ILjava/lang/Object;)Ljava/lang/Object;");
    jmethodID mid_add = env->GetMethodID(listClass, "add", "(Ljava/lang/Object;)Z");
    jmethodID mid_remove = env->GetMethodID(listClass, "remove", "(I)Ljava/lang/Object;");

    // 获取 Java 基本类型封装类和构造函数
#define PREPARE(cls, name, sig) \
        jclass cls##Class = env->FindClass("java/lang/" #cls); \
        jmethodID cls##_init = env->GetMethodID(cls##Class, "<init>", sig)

    PREPARE(Integer, int, "(I)V");
    PREPARE(Long, long, "(J)V");
    PREPARE(Float, float, "(F)V");
    PREPARE(Double, double, "(D)V");
    PREPARE(Boolean, boolean, "(Z)V");
    PREPARE(Short, short, "(S)V");
    PREPARE(Character, char, "(C)V");
    PREPARE(Byte, byte, "(B)V");

    jclass stringClass = env->FindClass("java/lang/String");

    jint java_size = env->CallIntMethod(listObj, mid_size);

    // 获取 Lua 表长度（最大索引）
    int lua_len = 0;
    for (auto& kv : tbl) {
        if (kv.first.is<int>()) {
            int key = kv.first.as<int>();
            if (key > lua_len) lua_len = key;
        }
    }

    auto build_java_object = [&](jobject refType, sol::object val) -> jobject {
        if (refType == nullptr || val.is<sol::nil_t>()) return nullptr;
        jclass cls = env->GetObjectClass(refType);
        if (env->IsInstanceOf(refType, IntegerClass)) {
            return env->NewObject(IntegerClass, Integer_init, (jint)val.as<int>());
        } else if (env->IsInstanceOf(refType, LongClass)) {
            return env->NewObject(LongClass, Long_init, (jlong)val.as<int64_t>());
        } else if (env->IsInstanceOf(refType, FloatClass)) {
            return env->NewObject(FloatClass, Float_init, (jfloat)val.as<float>());
        } else if (env->IsInstanceOf(refType, DoubleClass)) {
            return env->NewObject(DoubleClass, Double_init, (jdouble)val.as<double>());
        } else if (env->IsInstanceOf(refType, BooleanClass)) {
            return env->NewObject(BooleanClass, Boolean_init, (jboolean)val.as<bool>());
        } else if (env->IsInstanceOf(refType, ShortClass)) {
            return env->NewObject(ShortClass, Short_init, (jshort)val.as<int>());
        } else if (env->IsInstanceOf(refType, CharacterClass)) {
            return env->NewObject(CharacterClass, Character_init, (jchar)val.as<int>());
        } else if (env->IsInstanceOf(refType, ByteClass)) {
            return env->NewObject(ByteClass, Byte_init, (jbyte)val.as<int>());
        } else if (env->IsInstanceOf(refType, stringClass)) {
            std::string str = val.as<std::string>();
            return env->NewStringUTF(str.c_str());
        } else {
            return nullptr;  // 忽略复杂类型
        }
    };

    // 替换或追加
    for (int i = 0; i < lua_len; ++i) {
        sol::object luaVal = tbl[i + 1];
        if (luaVal.is<sol::nil_t>()) continue;

        if (i < java_size) {
            jobject oldObj = env->CallObjectMethod(listObj, mid_get, i);
            jobject newObj = build_java_object(oldObj, luaVal);
            if (newObj != nullptr)
                env->CallObjectMethod(listObj, mid_set, i, newObj);
            env->DeleteLocalRef(newObj);
            env->DeleteLocalRef(oldObj);
        } else {
            // 获取最后一个已存在的元素类型作为参考
            jobject lastRef = java_size > 0 ? env->CallObjectMethod(listObj, mid_get, java_size - 1) : nullptr;
            jobject newObj = build_java_object(lastRef, luaVal);
            if (newObj != nullptr)
                env->CallBooleanMethod(listObj, mid_add, newObj);
            env->DeleteLocalRef(newObj);
            if (lastRef) env->DeleteLocalRef(lastRef);
        }
    }

    // 删除多余
    for (jint i = java_size - 1; i >= lua_len; --i) {
        env->CallObjectMethod(listObj, mid_remove, i);
    }

    env->DeleteLocalRef(listClass);
}

void WRAP_C_LUA_FUNCTION::apply_soltable_to_existing_javalist_trampoline(sol::this_state ts, sol::variadic_args args) {
    auto table = args[0].as<sol::table>();
    uint64_t obj = args[1].as<uint64_t>();
    sync_soltable_to_javalist(ts, table, (jobject) obj);
}

std::string WRAP_C_LUA_FUNCTION::getJavaStringContent(sol::object solobj){
    std::string result = "";
    JavaEnv myenv;
    auto env = myenv.get();
    if (solobj.is<int64_t>()){
        jobject jobj = (jobject)solobj.as<int64_t>();
        if (env->IsInstanceOf(jobj, env->FindClass("java/lang/String"))) {
            const char *str = env->GetStringUTFChars((jstring) jobj, nullptr);
            result = std::string(str);
            env->ReleaseStringUTFChars((jstring) jobj, str);
        }
    }
    return result;
}

//String不可变，要修改只能用Create
int64_t WRAP_C_LUA_FUNCTION::createJavaString(sol::object solobj, sol::object solstr) {
    JavaEnv myenv;
    auto env = myenv.get();

    if (!solstr.is<std::string>()) {
        return solobj.as<int64_t>();
    }

    std::string content = solstr.as<std::string>();
    jstring jstr = env->NewStringUTF(content.c_str());
    //api33这里需要额外的decode，还没写。
    return (int64_t)jstr;  // 以整数形式返回给 Lua
}


static jvmtiIterationControl JNICALL objectInstanceCallback(jlong class_tag, jlong size, jlong* tag_ptr, void* user_data) {
    *tag_ptr = 1;
    return JVMTI_ITERATION_CONTINUE;
}

sol::table find_class_instance_byjvmti(sol::this_state ts, sol::variadic_args solargs){
    sol::state_view lua(ts);
    std::string classname = solargs[0].as<std::string>();
    JavaEnv myenv;
    auto env = myenv.get();
    static jvmtiEnv* jvmti = nullptr;
    if (!jvmti) {
        jint res = myenv.getJVM()->GetEnv((void **) &jvmti, JVMTI_VERSION_1_0);
        if (res != JNI_OK || jvmti == nullptr) {
            LOGE("GetEnv for JVMTI failed: %d", res);
            return sol::nil;
        }

        jvmtiCapabilities current;
        memset(&current, 0, sizeof(current));
        jvmti->GetCapabilities(&current);

        if (!current.can_tag_objects) {
            jvmtiCapabilities requested = {0};
            requested.can_tag_objects = 1;
            jvmti->AddCapabilities(&requested);
        }
    }

    auto clazz = (jclass) Class_Method_Finder::FindClassViaLoadClass(env,classname.c_str());

    jvmti->IterateOverInstancesOfClass(clazz, JVMTI_HEAP_OBJECT_EITHER,
                                       objectInstanceCallback, NULL);

    jlong tag = 1;
    jint count;
    jobject* instances;
    jvmti->GetObjectsWithTags(1, &tag, &count, &instances, NULL);

    sol::table result = lua.create_table(count, 0);  // 顺序数组模式

    if (instances != nullptr) {
        for (jsize i = 0; i < count; ++i) {
            result[i + 1] = (int64_t)instances[i];
        }
        jvmti->Deallocate(reinterpret_cast<unsigned char *>(instances));
    }
    jvmti->IterateOverHeap(JVMTI_HEAP_OBJECT_EITHER,
                           [](jlong class_tag, jlong size, jlong* tag_ptr, void* arg) -> jvmtiIterationControl {
                               if (tag_ptr && *tag_ptr == 1) {
                                   *tag_ptr = 0;
                               }
                               return JVMTI_ITERATION_CONTINUE;
                           },
                           nullptr);
    return result;
}

sol::table  WRAP_C_LUA_FUNCTION::find_class_instance(sol::this_state ts, sol::variadic_args solargs){
    sol::state_view lua(ts);
    std::string classname = solargs[0].as<std::string>();
    sol::table sol_result = lua.create_table();

    JavaEnv myenv;
    auto env = myenv.get();

    uint64_t heap = *(uint64_t*)(ArtInternals::RuntimeInstance + ArtInternals::RunTimeSpec.heap);
    static uint64_t offset_max_allocation_size = 0;
    if (!offset_max_allocation_size){
        for (int i = 0 ; i < 0x500 ; i+= 8){
            if (*(uint64_t*)(heap + i) == 0x200000){
                offset_max_allocation_size = i;
                break;
            }
        }
    }
    uint64_t maxSize = *(uint64_t*)(heap + offset_max_allocation_size);
    uint64_t live_bitmap_= *(uint64_t*)(heap + offset_max_allocation_size - 24);
    uint64_t mark_bitmap_= *(uint64_t*)(heap + offset_max_allocation_size - 16);
    uint64_t allocation_stack_addr = *(uint64_t*)(heap + offset_max_allocation_size + 8);
    uint64_t live_stack_addr = *(uint64_t*)(heap + offset_max_allocation_size + 16);
    uint64_t bump_pointer_space_addr = *(uint64_t*)(heap + offset_max_allocation_size + 56);

    auto pad = (accounting::padFor_blockGc*)(heap + offset_max_allocation_size + 72);
    uint64_t region_space_addr = (uint64_t)pad->region_space_;

    // 强制执行GC 这样我们的bump pointer space就能立即看到我们需要的实例了
    uint64_t dumb = 0;
    auto before = pad->gcs_completed_;
    ArtInternals::RequestConcurrentGCAndSaveObjectFn(reinterpret_cast<void *>(heap), ArtInternals::GetCurrentThread(), true, before, &dumb);
    while (pad->gcs_completed_ == before) {
        usleep(100);
    }

    using ContinuousSpaceBitmap = accounting::SpaceBitmap<kObjectAlignment>;
    using LargeObjectBitmap = accounting::SpaceBitmap<kMinPageSize>;
    class HeapBitmap {
    public:
        const void* heap_;
        // Bitmaps covering continuous spaces.
        std::vector<ContinuousSpaceBitmap*> continuous_space_bitmaps_;//ContinuousSpaceBitmap*
        //,TrackingAllocator<ContinuousSpaceBitmap*, kAllocatorTagHeapBitmap>
        // Sets covering discontinuous spaces.
        std::vector<LargeObjectBitmap*> large_object_bitmaps_;//LargeObjectBitmap*
        //,TrackingAllocator<LargeObjectBitmap*, kAllocatorTagHeapBitmapLOS>

    };
    HeapBitmap* livebitmap = (HeapBitmap*) live_bitmap_;
    HeapBitmap* markbitmap = (HeapBitmap*) mark_bitmap_;
    accounting::AtomicStack* allocation_stack = (accounting::AtomicStack*)allocation_stack_addr;
    accounting::AtomicStack* live_stack = (accounting::AtomicStack*)live_stack_addr;
    accounting::BumpPointerSpace* bump_pointer_space = (accounting::BumpPointerSpace*) bump_pointer_space_addr;

    auto regionSpaces = (accounting::RegionSpace*)region_space_addr;
    // 非空可以walk一下，但大部分情况都是空的，所以没写 基本永远都是nullptr

    std::vector<void*> result = {};

    jclass classClass = env->FindClass("java/lang/Class");
    jmethodID mid_getName = env->GetMethodID(classClass, "getName", "()Ljava/lang/String;");

    /*
    for (int i = allocation_stack->front_index_; i <= allocation_stack->back_index_; i++){
        auto tmp = *(uint32_t*)((uint64_t)allocation_stack->begin_ + 4*i);//硬编码size为4
        if (tmp) {
            auto jobj = ArtInternals::newlocalrefFn(env, reinterpret_cast<void *>(tmp));

            jclass objCls = env->GetObjectClass((jobject) jobj);

            jstring nameStr = (jstring) env->CallObjectMethod(objCls, mid_getName);
            const char *cname = env->GetStringUTFChars(nameStr, nullptr);
            std::string className(cname);
            env->ReleaseStringUTFChars(nameStr, cname);
            env->DeleteLocalRef(nameStr);
            env->DeleteLocalRef(objCls);
            if (className == classname) {
                sol_result.add(jobj);
            }
            else
                ArtInternals::deletelocalrefFn(env,(void*)jobj);
        }
    }
    for (int i = live_stack->front_index_; i <= live_stack->back_index_; i++){
        auto tmp = *(uint32_t*)((uint64_t)live_stack->begin_ + 4*i);
        if (tmp) {
            auto jobj = ArtInternals::newlocalrefFn(env, reinterpret_cast<void *>(tmp));

            jclass objCls = env->GetObjectClass((jobject) jobj);

            jstring nameStr = (jstring) env->CallObjectMethod(objCls, mid_getName);
            const char *cname = env->GetStringUTFChars(nameStr, nullptr);
            std::string className(cname);
            env->ReleaseStringUTFChars(nameStr, cname);
            env->DeleteLocalRef(nameStr);
            env->DeleteLocalRef(objCls);
            if (className == classname) {
                sol_result.add(jobj);
            }
            else
                ArtInternals::deletelocalrefFn(env,(void*)jobj);
        }
    }

    for(const auto& spaceBitmap: livebitmap->continuous_space_bitmaps_){
        if (spaceBitmap->heap_begin_)
            spaceBitmap->VisitMarkedRange(spaceBitmap->heap_begin_,spaceBitmap->heap_limit_,result, false);
    }
    for(const auto& spaceBitmap: livebitmap->large_object_bitmaps_){
        if (spaceBitmap->heap_begin_)
            spaceBitmap->VisitMarkedRange(spaceBitmap->heap_begin_,spaceBitmap->heap_limit_,result, false);
    }

    for(const auto& spaceBitmap: markbitmap->continuous_space_bitmaps_){
        if (spaceBitmap->heap_begin_)
            spaceBitmap->VisitMarkedRange(spaceBitmap->heap_begin_,spaceBitmap->heap_limit_,result, false);
    }
    for(const auto& spaceBitmap: markbitmap->large_object_bitmaps_){
        if (spaceBitmap->heap_begin_)
            spaceBitmap->VisitMarkedRange(spaceBitmap->heap_begin_,spaceBitmap->heap_limit_,result, false);
    }

     */



    //bump_pointer_space->Walk(result);
    //用户类实例基本都在bump_pointer_space里面
    if ((uint64_t)bump_pointer_space->mark_bitmap_.heap_begin_ != 0)
        bump_pointer_space->mark_bitmap_.VisitMarkedRange(bump_pointer_space->mark_bitmap_.heap_begin_,bump_pointer_space->mark_bitmap_.heap_limit_,result, false);
    if ((uint64_t)bump_pointer_space->live_bitmap_.heap_begin_ != 0)
        bump_pointer_space->live_bitmap_.VisitMarkedRange(bump_pointer_space->live_bitmap_.heap_begin_,bump_pointer_space->live_bitmap_.heap_limit_,result, false);
    if ((uint64_t)bump_pointer_space->temp_bitmap_.heap_begin_ != 0)
        bump_pointer_space->temp_bitmap_.VisitMarkedRange(bump_pointer_space->temp_bitmap_.heap_begin_,bump_pointer_space->temp_bitmap_.heap_limit_,result, false);

    for (const auto obj: result){
        auto jobj = ArtInternals::newlocalrefFn(env,obj);

        jclass objCls = env->GetObjectClass((jobject)jobj);
        if (!env->IsInstanceOf(objCls, classClass)) {
            LOGE("objCls is not an instance of java.lang.Class");
            continue;
        }

        jstring nameStr = (jstring)env->CallObjectMethod(objCls, mid_getName);
        const char* cname = env->GetStringUTFChars(nameStr, nullptr);
        std::string className(cname);
        env->ReleaseStringUTFChars(nameStr, cname);
        if (className == classname) {
            sol_result.add(jobj);
        }
        else
            ArtInternals::deletelocalrefFn(env,(void*)jobj);
        env->DeleteLocalRef(nameStr);
        env->DeleteLocalRef(objCls);
    }
    return sol_result;
}


sol::object WRAP_C_LUA_FUNCTION::call_java_function(sol::this_state ts, sol::variadic_args solargs){

    sol::state_view lua(ts);

    auto myenv = JavaEnv();
    auto env = myenv.get();

    auto function_info_table = solargs[0].as<sol::table>();

    std::string classname = function_info_table[1].get<std::string>();
    std::string hookFunctionName = function_info_table[2].get<std::string>();
    std::string shorty = function_info_table[3].get<std::string>();
    bool is_static = function_info_table[4].get<bool>();

    auto argstable = solargs[1].as<sol::table>();



    //如果要执行，先构造args数组，准备传递给原函数，再开启调用流程。
    //分配足够的内存，每个参数都给
    auto args = new uint32_t[(argstable.size() + 2) * 8];
    memset(args, 0, sizeof(uint32_t) * (argstable.size() + 2) * 8);
    uint32_t argsize = 0;

    auto clazz = (jclass) Class_Method_Finder::FindClassViaLoadClass(env,classname.c_str());
    if (!is_static) {
        auto thiz = (jobject)function_info_table[5].get<int64_t>();
        if (!thiz) return nullptr;
        if (env->IsSameObject(thiz, nullptr)) {
            // 无效，说明 thiz/clazz 已经悬空（被回收）
            return nullptr;
        }
        args[0] = *(uint32_t *) ((uint64_t)thiz & (~(0x1)));  // StackReference<T>* 地址，32位
        argsize += 4;
    }

    //这里开始parse来自lua的参数表，构造参数数组
    int checkShorty_index = 1;//顺便从1开始检查shorty，确保对应
    for (auto &p: argstable) {
        if (shorty[checkShorty_index] == 'F') {//float
            if (p.second.is<float>()) {
                float value = p.second.as<float>();
                memcpy((void *) ((uint64_t) args + argsize), &value, sizeof(float));
            }
            argsize += 4;
        } else if (shorty[checkShorty_index] == 'Z') {//boolean
            if (p.second.is<bool>()) {
                bool value = p.second.as<bool>();
                memcpy((void *) ((uint64_t) args + argsize), &value, sizeof(bool));
            }
            argsize += 4;
        } else if (shorty[checkShorty_index] == 'B') {//byte
            if (p.second.is<jbyte>()) {
                jbyte value = p.second.as<jbyte>();//其实就是signed char
                memcpy((void *) ((uint64_t) args + argsize), &value, sizeof(jbyte));
            }
            argsize += 4;
        } else if (shorty[checkShorty_index] == 'C') {//char
            if(p.second.is<jchar>()) {
                jchar value = p.second.as<jchar>();
                memcpy((void *) ((uint64_t) args + argsize), &value, sizeof(jchar));
            }
            if(p.second.is<char>()){
                char value = p.second.as<char>();
                memcpy((void *) ((uint64_t) args + argsize), &value, sizeof(char));
            }
            argsize += 4;
        } else if (shorty[checkShorty_index] == 'S') {//short
            if (p.second.is<short>()) {
                short value = p.second.as<short>();
                memcpy((void *) ((uint64_t) args + argsize), &value, sizeof(short));
            }
            argsize += 4;
        } else if (shorty[checkShorty_index] == 'I') {//int
            if (p.second.is<int>()) {
                int value = p.second.as<int>();
                memcpy((void *) ((uint64_t) args + argsize), &value, sizeof(int));
            }
            argsize += 4;
        } else if (shorty[checkShorty_index] == 'L') {//对象，全部都是uint32_t
            if (p.second.is<uint64_t>() ) {
                uint64_t value = p.second.as<uint64_t>();
                value &= (~(1));//通常需要对齐不然会崩溃
                uint32_t comporessedPtr = *(uint32_t *) value;
                memcpy((void *) ((uint64_t) args + argsize), &comporessedPtr, sizeof(uint32_t));
            }
            argsize += 4;
        } else if (shorty[checkShorty_index] == 'D') {//double
            if (p.second.is<double>()) {
                double value = p.second.as<double>();
                memcpy((void *) ((uint64_t) args + argsize), &value, sizeof(double));
            }
            argsize += 8;
        } else if (shorty[checkShorty_index] == 'J') {//long
            if (p.second.is<int64_t>()) {
                int64_t value = p.second.as<int64_t>();
                memcpy((void *) ((uint64_t) args + argsize), &value, sizeof(int64_t));
            }
            argsize += 8;
        }
        checkShorty_index++;
    }


    auto [methodID, _] = Class_Method_Finder::findJMethodIDByName(env,clazz,
                                                                  hookFunctionName.c_str(),
                                                                  shorty.c_str(),
                                                                  is_static

    );
    void* artMethod = nullptr;
    artMethod = ArtInternals::DecodeFunc(ArtInternals::jniIDManager, methodID);

    jvalue result;
    void *thread = ArtInternals::GetCurrentThread();
    ArtInternals::Invoke(artMethod, thread, (uint32_t *) args, argsize, &result,shorty.c_str());

    if (shorty[0] == 'F') {
        return sol::make_object(lua, (jfloat)result.f);
    } else if (shorty[0] == 'D') {
        return sol::make_object(lua, (jdouble)result.d);
    }
    else if (shorty[0] == 'Z'){
        return sol::make_object(lua, (jboolean)result.z);
    }
    else if (shorty[0] == 'B'){
        return sol::make_object(lua, (jbyte)result.b);//aka jbyte
    }
    else if (shorty[0] == 'C'){
        return sol::make_object(lua, (jchar)result.c);//aka unsigned shor, jchar
    }
    else if (shorty[0] == 'S'){
        return sol::make_object(lua, (jshort )result.s);
    }
    else if (shorty[0] == 'I'){
        return sol::make_object(lua, (jint)result.i);
    }
    else if (shorty[0] == 'J'){
        return sol::make_object(lua, (jlong)result.j);
    }
    else if (shorty[0] == 'L'){
        return sol::make_object(lua, (int64_t)ArtInternals::newlocalrefFn(env, result.l));
    }
    //else if (shorty[0] == 'V'){
    //    return sol::make_object(lua, (int)0);
    // }
    delete[] args;
    return sol::make_object(lua, (int)0);
}