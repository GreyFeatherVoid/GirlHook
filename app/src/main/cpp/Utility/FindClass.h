//
// Created by Lynnette on 2025/6/18.
//

#ifndef GIRLHOOK_FINDCLASS_H
#define GIRLHOOK_FINDCLASS_H
#include "../Utility/GirlLog.h"
#include <jni.h>
#include <android/log.h>
#include <unistd.h>
#include <cstring>
#include <dlfcn.h>
#include "../JVM/JVM.h"
#include "../GlobalStore/GlobalStore.h"

typedef void* ClassLoaderPtr;
typedef std::string CLASSNAMETYPE;
typedef struct {
    intptr_t heap;
    intptr_t threadList;
    intptr_t internTable;
    intptr_t classLinker;
    intptr_t jniIdManager;  // 仅部分版本
} ArtRuntimeSpecOffsets;

typedef struct {
    intptr_t quickResolutionTrampoline;
    intptr_t quickImtConflictTrampoline;
    intptr_t quickGenericJniTrampoline;
    intptr_t quickToInterpreterBridgeTrampoline;
} ClassLinkerSpecOffsets;

struct ArtMethodSpec {
    size_t offset_access_flags;
    size_t offset_entry_jni;
    size_t offset_entry_quick;
    size_t art_method_size;
    size_t interpreterCode;
};

static constexpr uint32_t kAccPublic =       0x0001;  // class, field, method, ic
static constexpr uint32_t kAccPrivate =      0x0002;  // field, method, ic
static constexpr uint32_t kAccProtected =    0x0004;  // field, method, ic
static constexpr uint32_t kAccStatic =       0x0008;  // field, method, ic
static constexpr uint32_t kAccFinal =        0x0010;  // class, field, method, ic
static constexpr uint32_t kAccSynchronized = 0x0020;  // method (only allowed on natives)
static constexpr uint32_t kAccSuper =        0x0020;  // class (not used in dex)
static constexpr uint32_t kAccVolatile =     0x0040;  // field
static constexpr uint32_t kAccBridge =       0x0040;  // method (1.5)
static constexpr uint32_t kAccTransient =    0x0080;  // field
static constexpr uint32_t kAccVarargs =      0x0080;  // method (1.5)
static constexpr uint32_t kAccNative =       0x0100;  // method
static constexpr uint32_t kAccInterface =    0x0200;  // class, ic
static constexpr uint32_t kAccAbstract =     0x0400;  // class, method, ic
static constexpr uint32_t kAccStrict =       0x0800;  // method
static constexpr uint32_t kAccSynthetic =    0x1000;  // class, field, method, ic
static constexpr uint32_t kAccAnnotation =   0x2000;  // class, ic (1.5)
static constexpr uint32_t kAccEnum =         0x4000;  // class, field, ic (1.5)
static constexpr uint32_t kAccFastNative =            0x00080000;  // method (dex only)

static constexpr uint32_t kAccCriticalNative =        0x00100000;
static constexpr uint32_t kAccNterpEntryPointFastPathFlag = 0x00100000;
// Set by the verifier for a method we do not want the compiler to compile.
static constexpr uint32_t kAccCompileDontBother =     0x02000000;  // method (runtime)
// Set by the class linker for a method that has only one implementation for a
// virtual call.
static constexpr uint32_t kAccSingleImplementation =  0x08000000;  // method (runtime)
// Used by a method to denote that its execution does not need to go through slow path interpreter.
static constexpr uint32_t kAccSkipAccessChecks =      0x00080000;  // method (runtime, not native)

enum GcCause {
    // Invalid GC cause used as a placeholder.
    kGcCauseNone,
    // GC triggered by a failed allocation. Thread doing allocation is blocked waiting for GC before
    // retrying allocation.
    kGcCauseForAlloc,
    // A background GC trying to ensure there is free memory ahead of allocations.
    kGcCauseBackground,
    // An explicit System.gc() call.
    kGcCauseExplicit,
    // GC triggered for a native allocation when NativeAllocationGcWatermark is exceeded.
    // (This may be a blocking GC depending on whether we run a non-concurrent collector).
    kGcCauseForNativeAlloc,
    // GC triggered for a collector transition.
    kGcCauseCollectorTransition,
    // Not a real GC cause, used when we disable moving GC (currently for GetPrimitiveArrayCritical).
    kGcCauseDisableMovingGc,
    // Not a real GC cause, used when we trim the heap.
    kGcCauseTrim,
    // Not a real GC cause, used to implement exclusion between GC and instrumentation.
    kGcCauseInstrumentation,
    // Not a real GC cause, used to add or remove app image spaces.
    kGcCauseAddRemoveAppImageSpace,
    // Not a real GC cause, used to implement exclusion between GC and debugger.
    kGcCauseDebugger,
    // GC triggered for background transition when both foreground and background collector are CMS.
    kGcCauseHomogeneousSpaceCompact,
    // Class linker cause, used to guard filling art methods with special values.
    kGcCauseClassLinker,
    // Not a real GC cause, used to implement exclusion between code cache metadata and GC.
    kGcCauseJitCodeCache,
    // Not a real GC cause, used to add or remove system-weak holders.
    kGcCauseAddRemoveSystemWeakHolder,
    // Not a real GC cause, used to prevent hprof running in the middle of GC.
    kGcCauseHprof,
    // Not a real GC cause, used to prevent GetObjectsAllocated running in the middle of GC.
    kGcCauseGetObjectsAllocated,
    // GC cause for the profile saver.
    kGcCauseProfileSaver,
    // GC cause for deleting dex cache arrays at startup.
    kGcCauseDeletingDexCacheArrays,
};
// Which types of collections are able to be performed.
enum CollectorType {
    // No collector selected.
    kCollectorTypeNone,
    // Non concurrent mark-sweep.
    kCollectorTypeMS,
    // Concurrent mark-sweep.
    kCollectorTypeCMS,
    // Concurrent mark-compact.
    kCollectorTypeCMC,
    // The background compaction of the Concurrent mark-compact GC.
    kCollectorTypeCMCBackground,
    // Semi-space / mark-sweep hybrid, enables compaction.
    kCollectorTypeSS,
    // Heap trimming collector, doesn't do any actual collecting.
    kCollectorTypeHeapTrim,
    // A (mostly) concurrent copying collector.
    kCollectorTypeCC,
    // The background compaction of the concurrent copying collector.
    kCollectorTypeCCBackground,
    // Instrumentation critical section fake collector.
    kCollectorTypeInstrumentation,
    // Fake collector for adding or removing application image spaces.
    kCollectorTypeAddRemoveAppImageSpace,
    // Fake collector used to implement exclusion between GC and debugger.
    kCollectorTypeDebugger,
    // A homogeneous space compaction collector used in background transition
    // when both foreground and background collector are CMS.
    kCollectorTypeHomogeneousSpaceCompact,
    // Class linker fake collector.
    kCollectorTypeClassLinker,
    // JIT Code cache fake collector.
    kCollectorTypeJitCodeCache,
    // Hprof fake collector.
    kCollectorTypeHprof,
    // Fake collector for installing/removing a system-weak holder.
    kCollectorTypeAddRemoveSystemWeakHolder,
    // Fake collector type for GetObjectsAllocated
    kCollectorTypeGetObjectsAllocated,
    // Fake collector type for ScopedGCCriticalSection
    kCollectorTypeCriticalSection,
};

namespace Class_Method_Finder{
    void iterate_class_info(JNIEnv *env);
    jclass FindClassViaLoadClass(JNIEnv *env, const char *class_name_dot);
    //用method jobject 获取函数名和shorty
    std::pair<std::string, std::string> getJNIMethodSignatureAndShorty(JNIEnv* env, jobject method);
    //根据名称找到一个方法 注意，目前并不关心参数情况，找不齐
    std::pair<jmethodID, std::string> findJMethodIDByName(JNIEnv* env, jclass clazz,
                                                          const char* methodName,
                                                          const char* target_shorty,
                                                          bool isStatic);
    // 解析单个Class，返回签名 和 shorty 字符
    std::pair<std::string, char> getSignatureAndShortyForClass(JNIEnv* env, jclass cls);
    //从jclazz获取所有方法名
    void iterate_all_method_from_jclass(JNIEnv *env, jclass clazz, std::vector<std::string>& methodsname);
}


namespace ClassStruct_Detector{
    //获取ArtRuntime Instance内部要用到的偏移
    bool getArtRuntimeSpec(
            void* runtime,
            void* javaVM,
            ArtRuntimeSpecOffsets* outSpec);
    //获取classlinker内部偏移
    bool tryGetArtClassLinkerSpec(void* runtime,
                                  ArtRuntimeSpecOffsets* runtimeSpec,
                                  ClassLinkerSpecOffsets* output);
    //获取artmethod内部偏移
    bool detect_artmethod_layout(JNIEnv* env, ArtMethodSpec* output);
}
namespace ArtInternals {
    using DecodeMethodIdFn = void* (*)(void* jniIdManager, jmethodID methodID);
    extern DecodeMethodIdFn DecodeFunc;

    extern uintptr_t RuntimeInstance;
    extern void* jniIDManager;

    using ArtMethodInvoke = void (*)(void*, void*, uint32_t*, uint32_t, jvalue*, const char*);
    extern ArtMethodInvoke Invoke;

    using CurrentFromGDB = void* (*)();
    extern CurrentFromGDB GetCurrentThread;

    using ScopedGCSection = int64_t(*)(void* self, void* thread,GcCause cause, CollectorType);
    extern ScopedGCSection SGCFn;

    using destroyScopedGCSection = void (*)(void* self);
    extern destroyScopedGCSection DestroyGCFn;

    using ScopedSuspendAll = int64_t(*)(void* self, const char* cause, bool long_suspend);
    extern ScopedSuspendAll ScopedSuspendAllFn;//注意要用到的时候一定要先JavaEnv创建一个实例，拿一次env和jvm，这里面我会让线程attach上，不然会崩溃。

    using destroyScopedSuspendAll = void(*)(void *self);
    extern destroyScopedSuspendAll destroyScopedSuspendAllFn;

    using newGlobalref = void*(*)(void*env,void* thread,void* objptr);
    extern newGlobalref newGlobalrefFn;

    using deleteGlobalref = void*(*)(void*env,void* thread,void* jobj);
    extern deleteGlobalref deleteGlobalrefFn;

    using VisitClassLoaders = int64_t(*)(void* thiz, void* visitor);
    extern VisitClassLoaders VisitClassLoadersFn;

    using newlocalref = int64_t(*)(void *envext, void* mirrorobj);
    extern  newlocalref newlocalrefFn;

    using deletelocalref =int64_t(*)(void* envext, void* jobject);
    extern deletelocalref deletelocalrefFn;

    using VisitClasses = void(*)(void* classLinker , void* ClassVisitor);
    extern VisitClasses VisitClassesFn;

    using PrettyDescriptor = std::string(*)(void* thiz);//art::mirror::Class *
    extern PrettyDescriptor PrettyDescriptorFn;
    extern ArtMethodSpec ArtMethodLayout;
    extern ArtRuntimeSpecOffsets RunTimeSpec;
    extern ClassLinkerSpecOffsets ClassLinkerSpec;


//LOGI("NEWLOCALREF:%p", realThis);
//deleterefFunc(env, realThis);

    bool Init();
}

#endif //GIRLHOOK_FINDCLASS_H
