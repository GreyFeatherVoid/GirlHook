//
// Created by Lynnette on 2025/6/18.
//
#include "FindClass.h"


namespace ArtInternals {
    DecodeMethodIdFn DecodeFunc = nullptr;
    uintptr_t RuntimeInstance = 0;
    void* jniIDManager = nullptr;
    ArtMethodInvoke Invoke = nullptr;
    CurrentFromGDB GetCurrentThread = nullptr;
    ScopedGCSection SGCFn = nullptr;
    destroyScopedGCSection DestroyGCFn = nullptr;
    ScopedSuspendAll ScopedSuspendAllFn = nullptr;
    destroyScopedSuspendAll destroyScopedSuspendAllFn = nullptr;
    newGlobalref newGlobalrefFn = nullptr;
    deleteGlobalref deleteGlobalrefFn = nullptr;
    VisitClassLoaders VisitClassLoadersFn = nullptr;
    newlocalref newlocalrefFn = nullptr;
    deletelocalref deletelocalrefFn = nullptr;
    VisitClasses VisitClassesFn = nullptr;
    PrettyDescriptor PrettyDescriptorFn = nullptr;

    ArtMethodSpec ArtMethodLayout = {};
    ArtRuntimeSpecOffsets RunTimeSpec = {};
    ClassLinkerSpecOffsets ClassLinkerSpec = {};

    bool Init() {

        JavaEnv myenv;

        if (!DecodeFunc) {
            DecodeFunc = (DecodeMethodIdFn) tool::get_address_from_module(
                    tool::find_path_from_maps("libart.so"),
                    "_ZN3art3jni12JniIdManager14DecodeMethodIdEP10_jmethodID"
            );
        }
        LOGI("DecodeMethodIdFn: %p", DecodeFunc);

        RuntimeInstance = reinterpret_cast<uintptr_t>(tool::get_address_from_module(
                tool::find_path_from_maps("libart.so"),
                "_ZN3art7Runtime9instance_E", false));
        RuntimeInstance = *(uintptr_t *) (RuntimeInstance);//从bss读取出真地址
        //RuntimeInstance = *(uintptr_t*)((uintptr_t)DecodeFunc + RUNINSTANCE_DIFF_DECODE);//测试用硬编码
        LOGI("RuntimeInstance: %p", (void *) RuntimeInstance);

        jniIDManager = *reinterpret_cast<void **>(RuntimeInstance + JNI_MANAGER_OFFSET);
        LOGI("jniIDManager: %p", jniIDManager);

        if (!Invoke) {
            Invoke = (ArtMethodInvoke) tool::get_address_from_module(
                    tool::find_path_from_maps("libart.so"),
                    "_ZN3art9ArtMethod6InvokeEPNS_6ThreadEPjjPNS_6JValueEPKc"
            );
        }
        LOGI("INVOKE FUNC: %p", Invoke);

        if (!GetCurrentThread) {
            GetCurrentThread = (CurrentFromGDB) tool::get_address_from_module(
                    tool::find_path_from_maps("libart.so"),
                    "_ZN3art6Thread14CurrentFromGdbEv"
            );
        }
        LOGI("CurrentFromGDB FUNC: %p", GetCurrentThread);
        if (!SGCFn) {
            SGCFn = (ScopedGCSection) tool::get_address_from_module(
                    tool::find_path_from_maps("libart.so"),
                    "_ZN3art2gc23ScopedGCCriticalSectionC2EPNS_6ThreadENS0_7GcCauseENS0_13CollectorTypeE"
            );
        }
        if (!DestroyGCFn) {
            DestroyGCFn = (destroyScopedGCSection) tool::get_address_from_module(
                    tool::find_path_from_maps("libart.so"),
                    "_ZN3art2gc23ScopedGCCriticalSectionD2Ev"
            );
        }

        if (!ScopedSuspendAllFn) {
            ScopedSuspendAllFn = (ScopedSuspendAll) tool::get_address_from_module(
                    tool::find_path_from_maps("libart.so"),
                    "_ZN3art16ScopedSuspendAllC2EPKcb"
            );
        }
        if (!destroyScopedSuspendAllFn) {
            destroyScopedSuspendAllFn = (destroyScopedSuspendAll) tool::get_address_from_module(
                    tool::find_path_from_maps("libart.so"),
                    "_ZN3art16ScopedSuspendAllD2Ev"
            );
        }
        if (!newGlobalrefFn) {
            newGlobalrefFn = (newGlobalref) tool::get_address_from_module(
                    tool::find_path_from_maps("libart.so"),
                    "_ZN3art9JavaVMExt12AddGlobalRefEPNS_6ThreadENS_6ObjPtrINS_6mirror6ObjectEEE");
        }
        if (!deleteGlobalrefFn) {
            deleteGlobalrefFn = (deleteGlobalref) tool::get_address_from_module(
                    tool::find_path_from_maps("libart.so"),
                    "_ZN3art9JavaVMExt15DeleteGlobalRefEPNS_6ThreadEP8_jobject");
        }
        if(!VisitClassLoadersFn){
            VisitClassLoadersFn = (VisitClassLoaders)tool::get_address_from_module(
                    tool::find_path_from_maps("libart.so"),
                    "_ZNK3art11ClassLinker17VisitClassLoadersEPNS_18ClassLoaderVisitorE");
        }
        if(!newlocalrefFn){
            newlocalrefFn = (newlocalref)tool::get_address_from_module(
                    tool::find_path_from_maps("libart.so"),
                    "_ZN3art9JNIEnvExt11NewLocalRefEPNS_6mirror6ObjectE");
        }
        if (!deletelocalrefFn){
            deletelocalrefFn = (deletelocalref)tool::get_address_from_module(
                    tool::find_path_from_maps("libart.so"),
                    "_ZN3art9JNIEnvExt14DeleteLocalRefEP8_jobject");
        }
        if (!VisitClassesFn){
            VisitClassesFn = (VisitClasses)tool::get_address_from_module(
                    tool::find_path_from_maps("libart.so"),
                    "_ZN3art11ClassLinker12VisitClassesEPNS_12ClassVisitorE");
        }
        if (!PrettyDescriptorFn){
            PrettyDescriptorFn = (PrettyDescriptor)tool::get_address_from_module(
                    tool::find_path_from_maps("libart.so"),
                    "_ZN3art6mirror5Class16PrettyDescriptorEv");
        }
        if(DecodeFunc && RuntimeInstance && jniIDManager && Invoke && GetCurrentThread &&
               SGCFn && DestroyGCFn && ScopedSuspendAllFn && destroyScopedSuspendAllFn &&
               newGlobalrefFn && deleteGlobalrefFn && VisitClassLoadersFn && newlocalrefFn
               && VisitClassesFn && PrettyDescriptorFn)
        {
            bool ok = ClassStruct_Detector::detect_artmethod_layout(myenv.get(), &ArtMethodLayout);
            if (!ok){
                LOGE("Failed to Detect ArtMethod Layout.");
                return false;
            }

            ok = ClassStruct_Detector::getArtRuntimeSpec((void*)ArtInternals::RuntimeInstance, myenv.getJVM(), &RunTimeSpec);
            if (ok) {
                printf("Found offsets:\n");
                printf("heap: 0x%lx\n", RunTimeSpec.heap);
                printf("threadList: 0x%lx\n", RunTimeSpec.threadList);
                printf("internTable: 0x%lx\n", RunTimeSpec.internTable);
                printf("classLinker: 0x%lx\n", RunTimeSpec.classLinker);
                printf("jniIdManager: 0x%lx\n", RunTimeSpec.jniIdManager);
            } else {
                printf("Failed to locate offsets\n");
                return false;
            }

            ok = ClassStruct_Detector::tryGetArtClassLinkerSpec((void*)ArtInternals::RuntimeInstance,&RunTimeSpec,& ClassLinkerSpec);
            if (!ok){
                LOGE("Failed to Detect ClassLinker Layout.");
                return false;
            }
            return true;
        }
        return false;
    }
}

void* realVisit(void* thiz, void* classloader){
    LOGI("Called realVisit ClassLoader:%p", classloader);
    auto currentSize = VectorStore<ClassLoaderPtr>::Instance().Size();
    VectorStore<ClassLoaderPtr>::Instance().Add((ClassLoaderPtr)classloader,currentSize);
    return classloader;
}

bool MyVisitClassImpl(void* thiz, void* kclass) {
    if (kclass == nullptr) return true;
    std::string dest = ArtInternals::PrettyDescriptorFn(kclass);
    size_t pos = dest.find('$');
    if (pos != std::string::npos) {
        dest = dest.substr(0, pos);  // 截取 $ 之前的部分
    }
    pos = dest.find('[');
    if (pos != std::string::npos) {
        dest = dest.substr(0, pos);  // 截取 $ 之前的部分
    }
    for (char& ch : dest) {
        if (ch == '.') {
            ch = '/';
        }
    }
    UnorderedStore<CLASSNAMETYPE>::Instance().Add(dest);
    return true;
}

namespace Class_Method_Finder {

    void Store_all_classLoaders(){
        // 构造 vtable
        void* vtable[3] = {};
        vtable[2] = (void*)+[](void* thiz, void* loader) -> bool {
            return realVisit(thiz, loader);
        };
        // 构造 fake visitor 实例
        void* visitorBuf[1] = {};
        visitorBuf[0] = vtable;

        // 调用 VisitClassLoaders
        VectorStore<ClassLoaderPtr>::Instance().Clear();
        void* classLinker = *(void**)(ArtInternals::RuntimeInstance + ArtInternals::RunTimeSpec.classLinker);
        ArtInternals::VisitClassLoadersFn(classLinker, visitorBuf);
    }

    void iterate_all_classes(){
        // 构造 vtable，只有一个函数，在 operator() 的虚函数槽位
        UnorderedStore<CLASSNAMETYPE>::Instance().Clear();
        void* vtable[3] = {};
        vtable[2] = (void*)+[](void* thiz, void* klass, void* arg) -> bool {
            return MyVisitClassImpl(thiz, klass);
        };
        // 构造 fake visitor 实例，第一项就是 vtable 指针
        void* visitorBuf[1] = {};
        visitorBuf[0] = vtable;
        // 调用 VisitClassLoaders
        void* classLinker = *(void**)(ArtInternals::RuntimeInstance + ArtInternals::RunTimeSpec.classLinker);
        ArtInternals::VisitClassesFn(classLinker, visitorBuf);
    }

    void iterate_class_info(JNIEnv *env){
        Store_all_classLoaders();
        iterate_all_classes();
    }

    jclass FindClassViaLoadClass(JNIEnv *env, const char *class_name_dot) {
        Store_all_classLoaders();//更新一下所有的loader
        for (const auto & loader : VectorStore<ClassLoaderPtr>::Instance().GetAll()){
            auto classLoader = (jobject)ArtInternals::newlocalrefFn(env, loader);
            LOGI("classloader %p", classLoader);

            jclass loaderClass = env->GetObjectClass(classLoader);
            jmethodID toString = env->GetMethodID(loaderClass, "toString", "()Ljava/lang/String;");
            jstring str = (jstring) env->CallObjectMethod(classLoader, toString);
            const char *desc = env->GetStringUTFChars(str, nullptr);

            LOGI("ClassLoader = %s", desc);
            // 调用 classLoader.loadClass(String)
            jclass classLoaderClass = env->FindClass("java/lang/ClassLoader");
            jmethodID loadClass = env->GetMethodID(classLoaderClass, "loadClass",
                                                   "(Ljava/lang/String;)Ljava/lang/Class;");
            if (!loadClass) {
                LOGI("GetMethodID(loadClass) failed");
                ArtInternals::deletelocalrefFn(env, classLoader);
                continue;
            }

            jstring classNameStr = env->NewStringUTF(class_name_dot);
            jclass targetClass = (jclass) env->CallObjectMethod(classLoader, loadClass, classNameStr);
            if (env->ExceptionCheck()) {
                env->ExceptionDescribe();
                env->ExceptionClear();
            }
            env->DeleteLocalRef(classNameStr);

            if (!targetClass) {
                LOGI("loadClass failed for %s", class_name_dot);
                ArtInternals::deletelocalrefFn(env, classLoader);
                continue;
            }

            LOGI("Successfully loaded class: %s", class_name_dot);
            ArtInternals::deletelocalrefFn(env, classLoader);
            return targetClass;

        }
        return nullptr;
    }
    // 返回方法的完整签名 和 shorty 字符串
    std::pair<std::string, std::string>
    getJNIMethodSignatureAndShorty(JNIEnv *env, jobject method) {
        jclass methodClass = env->FindClass("java/lang/reflect/Method");

        jmethodID mid_getParameterTypes = env->GetMethodID(methodClass, "getParameterTypes",
                                                           "()[Ljava/lang/Class;");
        jobjectArray paramTypes = (jobjectArray) env->CallObjectMethod(method,
                                                                       mid_getParameterTypes);

        jmethodID mid_getReturnType = env->GetMethodID(methodClass, "getReturnType",
                                                       "()Ljava/lang/Class;");
        jobject returnType = env->CallObjectMethod(method, mid_getReturnType);

        jsize paramCount = env->GetArrayLength(paramTypes);

        std::string sig = "(";
        std::string shorty;

        for (jsize i = 0; i < paramCount; i++) {
            jobject paramCls = env->GetObjectArrayElement(paramTypes, i);
            auto [paramSig, paramShorty] = getSignatureAndShortyForClass(env, (jclass) paramCls);
            sig += paramSig;
            shorty += paramShorty;
            env->DeleteLocalRef(paramCls);
        }
        sig += ")";

        auto [retSig, retShorty] = getSignatureAndShortyForClass(env, (jclass) returnType);
        sig += retSig;
        shorty = retShorty + shorty;  // shorty第一个是返回类型，然后是参数类型

        env->DeleteLocalRef(returnType);
        env->DeleteLocalRef(paramTypes);
        env->DeleteLocalRef(methodClass);

        return {sig, shorty};
    }

/**
 * 找 jmethodID
 * @param env JNIEnv指针
 * @param clazz 目标类
 * @param methodName 方法名
 * @param isStatic 是否查找静态方法
 * @return 找到返回 jmethodID，找不到返回 nullptr
 */
    std::pair<jmethodID, std::string>
    findJMethodIDByName(JNIEnv *env, jclass clazz, const char *methodName,const char* target_shorty, bool isStatic) {
        jclass classClass = env->FindClass("java/lang/Class");
        jmethodID mid_getDeclaredMethods = env->GetMethodID(classClass, "getDeclaredMethods",
                                                            "()[Ljava/lang/reflect/Method;");
        if (!mid_getDeclaredMethods) {
            LOGE("getDeclaredMethods not found");
            return {nullptr, ""};
        }

        jobjectArray methods = (jobjectArray) env->CallObjectMethod(clazz, mid_getDeclaredMethods);
        if (env->ExceptionCheck()) {
            env->ExceptionDescribe();
            env->ExceptionClear();
            return {nullptr, ""};
        }

        jsize methodCount = env->GetArrayLength(methods);
        jclass methodClass = env->FindClass("java/lang/reflect/Method");
        jmethodID mid_getName = env->GetMethodID(methodClass, "getName", "()Ljava/lang/String;");
        jmethodID mid_getModifiers = env->GetMethodID(methodClass, "getModifiers", "()I");
        if (!mid_getName || !mid_getModifiers) {
            LOGE("getName or getModifiers not found");
            return {nullptr, ""};
        }

        for (jsize i = 0; i < methodCount; i++) {
            jobject method = env->GetObjectArrayElement(methods, i);

            jstring nameStr = (jstring) env->CallObjectMethod(method, mid_getName);
            const char *nameCStr = env->GetStringUTFChars(nameStr, nullptr);

            bool nameMatch = strcmp(nameCStr, methodName) == 0;

            // 判断静态或非静态
            jint modifiers = env->CallIntMethod(method, mid_getModifiers);
            bool methodIsStatic = (modifiers & 0x0008) != 0; // Modifier.STATIC=0x0008

            env->ReleaseStringUTFChars(nameStr, nameCStr);
            env->DeleteLocalRef(nameStr);

            if (nameMatch && methodIsStatic == isStatic) {
                // 找到匹配的方法，拼签名
                auto [sig, shorty] = getJNIMethodSignatureAndShorty(env, method);
                if (shorty == target_shorty) {
                    jmethodID mid = nullptr;
                    if (isStatic) {
                        mid = env->GetStaticMethodID(clazz, methodName, sig.c_str());
                    } else {
                        mid = env->GetMethodID(clazz, methodName, sig.c_str());
                    }

                    env->DeleteLocalRef(method);
                    env->DeleteLocalRef(methodClass);
                    env->DeleteLocalRef(methods);
                    env->DeleteLocalRef(classClass);

                    if (!mid) {
                        LOGE("GetMethodID failed for %s with signature %s", methodName,
                             sig.c_str());
                    }
                    return {mid, shorty};
                }
            }
            env->DeleteLocalRef(method);
        }

        env->DeleteLocalRef(methodClass);
        env->DeleteLocalRef(methods);
        env->DeleteLocalRef(classClass);

        return {nullptr, ""};
    }

// 解析单个Class，返回签名 和 shorty 字符
    std::pair<std::string, char> getSignatureAndShortyForClass(JNIEnv *env, jclass cls) {
        jclass classClass = env->FindClass("java/lang/Class");
        jmethodID mid_getName = env->GetMethodID(classClass, "getName", "()Ljava/lang/String;");
        jstring nameStr = (jstring) env->CallObjectMethod(cls, mid_getName);
        const char *nameCStr = env->GetStringUTFChars(nameStr, nullptr);

        std::string sig;
        char shorty;

        if (strcmp(nameCStr, "void") == 0) {
            sig = "V";
            shorty = 'V';
        } else if (strcmp(nameCStr, "boolean") == 0) {
            sig = "Z";
            shorty = 'Z';
        } else if (strcmp(nameCStr, "byte") == 0) {
            sig = "B";
            shorty = 'B';
        } else if (strcmp(nameCStr, "char") == 0) {
            sig = "C";
            shorty = 'C';
        } else if (strcmp(nameCStr, "short") == 0) {
            sig = "S";
            shorty = 'S';
        } else if (strcmp(nameCStr, "int") == 0) {
            sig = "I";
            shorty = 'I';
        } else if (strcmp(nameCStr, "long") == 0) {
            sig = "J";
            shorty = 'J';
        } else if (strcmp(nameCStr, "float") == 0) {
            sig = "F";
            shorty = 'F';
        } else if (strcmp(nameCStr, "double") == 0) {
            sig = "D";
            shorty = 'D';
        } else if (nameCStr[0] == '[') {
            // 数组统一当对象
            sig = std::string(nameCStr);
            for (auto &c: sig) {
                if (c == '.') c = '/';
            }
            shorty = 'L';
        } else {
            std::string className(nameCStr);
            for (auto &c: className) {
                if (c == '.') c = '/';
            }
            sig = "L" + className + ";";
            shorty = 'L';
        }

        env->ReleaseStringUTFChars(nameStr, nameCStr);
        env->DeleteLocalRef(nameStr);
        env->DeleteLocalRef(classClass);

        return {sig, shorty};
    }

    void iterate_all_method_from_jclass(JNIEnv *env, jclass clazz, std::vector<std::string>& methodsname) {
        methodsname.clear();
        if (clazz == nullptr) {
            LOGE("clazz is null, skip iterate.");
            return;
        }

        jclass classClass = env->FindClass("java/lang/Class");
        if (env->ExceptionCheck()) {
            env->ExceptionDescribe();
            env->ExceptionClear();
            LOGE("Failed to find java/lang/Class");
            return;
        }

        jmethodID mid_getDeclaredMethods = env->GetMethodID(classClass, "getDeclaredMethods", "()[Ljava/lang/reflect/Method;");
        if (!mid_getDeclaredMethods || env->ExceptionCheck()) {
            env->ExceptionDescribe();
            env->ExceptionClear();
            LOGE("Method getDeclaredMethods not found");
            return;
        }

        jobjectArray methods = (jobjectArray)env->CallObjectMethod(clazz, mid_getDeclaredMethods);
        if (env->ExceptionCheck() || methods == nullptr) {
            env->ExceptionDescribe();
            env->ExceptionClear();
            LOGE("CallObjectMethod(getDeclaredMethods) failed");
            return;
        }

        jsize methodCount = env->GetArrayLength(methods);

        jclass methodClass = env->FindClass("java/lang/reflect/Method");
        if (env->ExceptionCheck()) {
            env->ExceptionDescribe();
            env->ExceptionClear();
            LOGE("Failed to find Method class");
            return;
        }

        jmethodID mid_getName = env->GetMethodID(methodClass, "getName", "()Ljava/lang/String;");
        jmethodID mid_getModifiers = env->GetMethodID(methodClass, "getModifiers", "()I");
        if (!mid_getName || !mid_getModifiers || env->ExceptionCheck()) {
            env->ExceptionDescribe();
            env->ExceptionClear();
            LOGE("Method getName or getModifiers not found");
            return;
        }

        for (jsize i = 0; i < methodCount; i++) {
            jobject method = env->GetObjectArrayElement(methods, i);
            if (method == nullptr || env->ExceptionCheck()) {
                env->ExceptionDescribe();
                env->ExceptionClear();
                continue;
            }

            jstring nameStr = (jstring)env->CallObjectMethod(method, mid_getName);
            if (env->ExceptionCheck() || nameStr == nullptr) {
                env->ExceptionDescribe();
                env->ExceptionClear();
                continue;
            }

            const char* nameCStr = env->GetStringUTFChars(nameStr, nullptr);
            if (nameCStr != nullptr) {
                jint modifiers = env->CallIntMethod(method, mid_getModifiers);
                bool methodIsStatic = (modifiers & 0x0008) != 0; // Modifier.STATIC=0x0008

                auto sigNshorty = getJNIMethodSignatureAndShorty(env, method);
                std::string concat_name = std::string(nameCStr) + "//" + sigNshorty.first + "//" + sigNshorty.second;
                if (methodIsStatic)
                    concat_name = "[S]" + concat_name;
                else
                    concat_name = "[D]" + concat_name;
                methodsname.push_back(concat_name);
                env->ReleaseStringUTFChars(nameStr, nameCStr);
            }

            env->DeleteLocalRef(method);
            env->DeleteLocalRef(nameStr);
        }

        // Clean up local refs
        env->DeleteLocalRef(methodClass);
        env->DeleteLocalRef(methods);
        env->DeleteLocalRef(classClass);
    }

}

namespace ClassStruct_Detector {
    bool getArtRuntimeSpec(
            void *runtime,
            void *javaVM,
            ArtRuntimeSpecOffsets *outSpec) {
        size_t pointerSize = sizeof(void *);
        intptr_t startOffset = (pointerSize == 4) ? 200 : 384;
        intptr_t endOffset = startOffset + (100 * pointerSize);

        if (runtime == NULL || javaVM == NULL || outSpec == NULL) {
            return false;
        }

        for (intptr_t offset = startOffset; offset < endOffset; offset += pointerSize) {
            void *value = *(void **) ((uintptr_t) runtime + offset);
            if (value == javaVM) {
                // 找到vm成员偏移，推算其它成员偏移
                intptr_t classLinkerOffset = 0;
                intptr_t jniIdManagerOffset = 0;

                classLinkerOffset = offset - 4 * pointerSize;
                jniIdManagerOffset = offset - 1 * pointerSize;

                intptr_t internTableOffset = classLinkerOffset - pointerSize;
                intptr_t threadListOffset = internTableOffset - pointerSize;
                intptr_t heapOffset = 0;


                heapOffset = threadListOffset - 8 * pointerSize;
                outSpec->heap = heapOffset;
                outSpec->threadList = threadListOffset;
                outSpec->internTable = internTableOffset;
                outSpec->classLinker = classLinkerOffset;
                outSpec->jniIdManager = jniIdManagerOffset;
                ClassLinkerSpecOffsets tmp;
                if (tryGetArtClassLinkerSpec(runtime, outSpec, &tmp) && tmp.quickGenericJniTrampoline != 0)
                    return true;
            }
        }

        return false;  // 没找到
    }

    bool tryGetArtClassLinkerSpec(void *runtime, ArtRuntimeSpecOffsets *runtimeSpec,
                                  ClassLinkerSpecOffsets *output) {

        static int POINTER_SIZE = sizeof(void *);
        uintptr_t classLinkerOffset = runtimeSpec->classLinker;
        uintptr_t internTableOffset = runtimeSpec->internTable;

        void *classLinker = *(void **) ((uintptr_t) runtime + classLinkerOffset);
        void *internTable = *(void **) ((uintptr_t) runtime + internTableOffset);

        intptr_t startOffset = (POINTER_SIZE == 4) ? 100 : 200;
        intptr_t endOffset = startOffset + (100 * POINTER_SIZE);

        for (intptr_t offset = startOffset; offset < endOffset; offset += POINTER_SIZE) {
            void *value = *(void **) ((uintptr_t) classLinker + offset);
            if (value == internTable) {
                int delta = 0;
                delta = 6;

                intptr_t quickGenericJniTrampolineOffset = offset + (delta * POINTER_SIZE);
                intptr_t quickResolutionTrampolineOffset = 0;

                quickResolutionTrampolineOffset =
                        quickGenericJniTrampolineOffset - (2 * POINTER_SIZE);

                output->quickResolutionTrampoline = quickResolutionTrampolineOffset;
                output->quickImtConflictTrampoline = quickGenericJniTrampolineOffset - POINTER_SIZE;
                output->quickGenericJniTrampoline = quickGenericJniTrampolineOffset;
                output->quickToInterpreterBridgeTrampoline =
                        quickGenericJniTrampolineOffset + POINTER_SIZE;

                return true;
            }
        }

        return false;
    }

    bool detect_artmethod_layout(JNIEnv *env, ArtMethodSpec *output) {
        size_t pointer_size = sizeof(void *);
        jclass cls = env->FindClass("android/os/Process");
        jmethodID mid = env->GetStaticMethodID(cls, "getElapsedCpuTime", "()J");
        env->DeleteLocalRef(cls);

        void *art_method = ArtInternals::DecodeFunc(ArtInternals::jniIDManager, mid);

        uintptr_t base = reinterpret_cast<uintptr_t>(art_method);
        uintptr_t entry_jni_offset = 0;
        uintptr_t access_flags_offset = 0;
        size_t found = 0;

        const uint32_t expected_flags =
                kAccPublic | kAccStatic | kAccFinal | kAccNative; // public static native final
        const uint32_t flags_mask = 0x0000FFFF;

        for (size_t offset = 0; offset < 64; offset += 4) {
            uintptr_t addr = base + offset;

            // 1. check if it's a pointer into libandroid_runtime.so
            void *maybe_ptr = *reinterpret_cast<void **>(addr);
            if (tool::is_in_module(maybe_ptr, "libandroid_runtime.so")) {
                entry_jni_offset = offset;
                found++;
                LOGI("Finding: entry_jni_offset = 0x%lx", offset);
            }

            // 2. check if it looks like access_flags
            uint32_t maybe_flags = *reinterpret_cast<uint32_t *>(addr);
            if ((maybe_flags & flags_mask) == expected_flags) {
                access_flags_offset = offset;
                found++;
                LOGI("Finding: access_flags_offset = 0x%lx (flags = 0x%x)", offset, maybe_flags);
            }

            if (found == 2) break;
        }

        if (found != 2) {
            LOGE("Failed to detect ArtMethod field layout");
            return false;
        }

        // 3. quick_code entry offset is next pointer
        uintptr_t entry_quick_offset = entry_jni_offset + pointer_size;

        output->offset_entry_jni = entry_jni_offset;
        output->offset_access_flags = access_flags_offset;
        output->offset_entry_quick = entry_quick_offset;
        output->art_method_size = entry_quick_offset + pointer_size;
        output->interpreterCode = output->offset_entry_jni - pointer_size;//这个字段不一定所有系统都有

        LOGI("Result offset_entry_jni 0x%zx", output->offset_entry_jni);
        LOGI("Result offset_access_flags 0x%zx", output->offset_access_flags);
        LOGI("Result offset_entry_quick 0x%zx", output->offset_entry_quick);
        LOGI("Result interpreterCode  0x%zx", output->interpreterCode);
        LOGI("Result art_method_size 0x%zx", output->art_method_size);

        LOGI("Estimated ArtMethod size: %zu", output->art_method_size);
        return true;
    }
}