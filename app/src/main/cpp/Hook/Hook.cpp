//
// Created by Lynnette on 2025/6/18.
//
#include "Hook.h"

#define TRAMPOLINE_SIZE 0x10

uint32_t getModifiedFlag(uint32_t orgFlag){
    uint32_t removeFlags =  (kAccCriticalNative | kAccFastNative | kAccNterpEntryPointFastPathFlag);
    uint32_t addFlags = (kAccNative | kAccCompileDontBother);
    removeFlags = (~removeFlags);
    return ((orgFlag & removeFlags) | addFlags);
}

uint32_t GetforceUseInterpretorFlag(uint32_t orgFlag){
    uint32_t removeFlags =  (kAccSingleImplementation | kAccNterpEntryPointFastPathFlag);//没有kAccFastInterpreterToInterpreterInvoke |
    if ((orgFlag & kAccNative) == 0) {
        removeFlags |= (kAccSkipAccessChecks);
    }
    removeFlags = (~removeFlags);
    return ((orgFlag & removeFlags) | kAccCompileDontBother);
}

//之所以留下第一个参数，是因为在调用原函数的时候也可以用这里
void recover_artmethod(void* ArtmethodToRecover,hooked_function hookInfo, bool tempRecover = false){
    if (tempRecover) {
        *reinterpret_cast<uint32_t *>((char *) ArtmethodToRecover +
                                      hookInfo.Layout.offset_access_flags) = GetforceUseInterpretorFlag(hookInfo.orgFlag);
    }
    else {
        *reinterpret_cast<uint32_t *>((char *) ArtmethodToRecover +
                                      hookInfo.Layout.offset_access_flags) = (hookInfo.orgFlag);
    }
    *reinterpret_cast<uint64_t*>((char*)ArtmethodToRecover + hookInfo.Layout.offset_entry_quick) = (uint64_t)hookInfo.orgEntryPointPtr;
    *reinterpret_cast<uint64_t*>((char*)ArtmethodToRecover + hookInfo.Layout.offset_entry_jni) = (uint64_t)hookInfo.orgJNIEntry;
}

void unhook(uint32_t hookIndex){
    //最好先暂停再unhook
    //必须是引用
    std::mutex& mtx = HookIdLockManager::Instance().GetMutex(hookIndex);
    std::lock_guard<std::mutex> lock(mtx);
    auto& info = VectorStore<hooked_function>::Instance().Get(hookIndex);
    if (info.Valid){
        info.Valid = false;
        char SSA[128] = {};
        ArtInternals::ScopedSuspendAllFn(SSA,"UNHOOK",false);
        recover_artmethod(info.ArtMethod, info);
        if (info.ourTrampoline)
            tool::free_exec_mem(info.ourTrampoline,TRAMPOLINE_SIZE);
        info.ourTrampoline = nullptr;
        ArtInternals::destroyScopedSuspendAllFn(SSA);
    }

}

uint32_t getAvailableIndex() {
    auto size = VectorStore<hooked_function>::Instance().Size();
    for (int i = 0; i < size; i++) {
        auto instance = VectorStore<hooked_function>::Instance().CopyByIndex(i);
        if (!instance.Valid) {
            return i;
        }
    }
    // 如果没有空槽，则返回 size 表示新插入位置
    return size;
}

// trampoline: 每个方法一份，写入 hook_id 和 handler_addr
uint8_t* GenerateTrampoline(uint64_t hook_id, void* handler_addr) {
    uint8_t* code = (uint8_t *)tool::allocate_exec_mem(TRAMPOLINE_SIZE);
    uint32_t* inst = (uint32_t*)code;
    int i = 0;
    inst[i++] = 0xA9BF07E0;//stp x0, x1, [sp, #-16]!
    // 1. mov x0, #hook_id
    // Split成 movz/movk if needed（这里只演示低16位 足够用了）
    inst[i++] = 0xD2800000 | ((hook_id & 0xFFFF) << 5); // movz x0, #imm16
    // 2. ldr x1, [pc, #offset] → 假地址，用于占位
    inst[i++] = 0x58000041; // ldr x1, #8 （相对于下一条指令）
    // 3. br x1
    inst[i++] = 0xD61F0020;
    // 4. literal data: handler_addr（8字节）
    void** addr_ptr = (void**)&inst[i++];
    *addr_ptr = handler_addr;
    return code;
}

__attribute__((naked)) void hook_trampoline_ex();
int hook_java_method(
        const char* org_fullname,
        const char* className,
                      const char* methodName,
                      const char* target_shorty,
                      bool isStatic,
                      const char* enterfuncname,
                      const char* enterfunc,
                      const char* leavefuncname,
                      const char* leavefunc) {
    JavaEnv Jenv;
    JNIEnv* env = Jenv.get();
    JavaVM * vm = Jenv.getJVM();

    int installed_index = 0;
    for (const auto & installed_hook : VectorStore<hooked_function>::Instance().GetAll()){
        //判断是否为已经安装的hook
        if (installed_hook.org_fullname == org_fullname && installed_hook.Valid){
            //该hook已经安装过，只更新脚本
            VectorStore<hooked_function>::Instance().Get(installed_index).enterfunc = enterfunc;
            VectorStore<hooked_function>::Instance().Get(installed_index).leavefunc = leavefunc;
            LUA::lua->script(enterfunc);
            LUA::lua->script(leavefunc);
            return 2;
        }
        installed_index++;
    }

    jclass clazz = Class_Method_Finder::FindClassViaLoadClass(env, className);
    if (env->ExceptionCheck()) {
        env->ExceptionDescribe();
        env->ExceptionClear();
    }
    if (!clazz) {
        LOGE("FindClass failed: %s", className);
        return false;
    }
    LOGE("FindClass succeeded %s", className);
    auto [methodID, shorty] =
            Class_Method_Finder::findJMethodIDByName(env,clazz,methodName,target_shorty,isStatic);
    if (!methodID) {
        LOGE("GetMethodID failed: %s", methodName);
        return false;
    }
    LOGE("GetMethodID succeeded: %s %p, shorty:%s", methodName, methodID, shorty.c_str());
    auto artMethod = ArtInternals::DecodeFunc(ArtInternals::jniIDManager, methodID);
    LOGI("artMethod : %p", artMethod);

    try {
        LUA::lua->safe_script(enterfunc);
    }
    catch (const sol::error& e) {
        std::string installError = std::string("Lua script Enter error: ") + e.what();
        Commands::tcp_log(installError);
        return 0;
    }
    try {
        LUA::lua->safe_script(leavefunc);
    }
    catch (const sol::error& e) {
        std::string installError = std::string("Lua script Leave error: ") + e.what();
        Commands::tcp_log(installError);
        return 0;
    }

    // 获取原始 entry point
    void* quickCode = reinterpret_cast<void*>((char*)artMethod + ArtInternals::ArtMethodLayout.offset_entry_quick);
    uint32_t orgFlag = *reinterpret_cast<uint32_t*>((char*)artMethod + ArtInternals::ArtMethodLayout.offset_access_flags);
    void* jni = reinterpret_cast<void*>((char*)artMethod + ArtInternals::ArtMethodLayout.offset_entry_jni);
    void* interpreterCode = reinterpret_cast<void*>((char*)artMethod + ArtInternals::ArtMethodLayout.interpreterCode);

    uint32_t hookIndex = getAvailableIndex();
    void* our_trampoline = GenerateTrampoline(hookIndex, (void*)&hook_trampoline_ex);
    hooked_function newFunc = {
            org_fullname,
            true,
            isStatic,
            artMethod,
            our_trampoline,
            *(uint64_t*)quickCode,
            *(uint64_t*)jni,
            orgFlag,
            ArtInternals::ArtMethodLayout,
            className,
            methodName,
            shorty,
            {},
            methodID,
            enterfuncname,
            enterfunc,
            leavefuncname,
            leavefunc
    };
    memcpy((void*)newFunc.backup, (void*)artMethod,ArtInternals::ArtMethodLayout.art_method_size);
    VectorStore<hooked_function>::Instance().Add(newFunc, hookIndex);

    char SSA[128] = {};
    ArtInternals::ScopedSuspendAllFn(SSA,"Install Hook",false);

    *reinterpret_cast<uint32_t*>((char*)artMethod + ArtInternals::ArtMethodLayout.offset_access_flags) = getModifiedFlag(orgFlag);
    void* classLinker = *(void**)((uintptr_t)ArtInternals::RuntimeInstance + ArtInternals::RunTimeSpec.classLinker);
    *(void**)jni = our_trampoline;// (void*)& hooks_handler;//unknown opcode
    *(void**)quickCode = *(void**)((uintptr_t)classLinker + ArtInternals::ClassLinkerSpec.quickGenericJniTrampoline);//quicktoInterpretorbridge
    //*(void**)interpreterCode = *(void**)((uintptr_t)classLinker + ClassLinkerSpec.quickToInterpreterBridgeTrampoline);

    ArtInternals::destroyScopedSuspendAllFn(SSA);

    LOGI("HOOKED!!!!");
    return true;
}

extern "C" uint64_t JNICALL hooks_handler(JNIEnv* env, jobject thiz,uint64_t x2,uint64_t x3,uint64_t x4,uint64_t x5,uint64_t x6,uint64_t x7) {
    uint64_t tmpHookid;
    asm volatile("mov %0, x15" : "=r"(tmpHookid));
    uint32_t hookID = (uint32_t) tmpHookid;

    //x0–x7 和 v0–v7 各有 8 个寄存器可以传参

    double v0, v1, v2, v3, v4, v5, v6, v7;
    asm volatile(
            "mov %0, v0.d[0]\n\t"
            "mov %1, v1.d[0]\n\t"
            "mov %2, v2.d[0]\n\t"
            "mov %3, v3.d[0]\n\t"
            "mov %4, v4.d[0]\n\t"
            "mov %5, v5.d[0]\n\t"
            "mov %6, v6.d[0]\n\t"
            "mov %7, v7.d[0]\n\t"
            : "=r"(v0), "=r"(v1), "=r"(v2), "=r"(v3),
    "=r"(v4), "=r"(v5), "=r"(v6), "=r"(v7)
            :
            :);

    uint64_t origin_sp;
    asm volatile("mov %0, x29" : "=r"(origin_sp));
    void *args_in = (void *) (origin_sp + 0x20);

    //LOGI("env: %p, Thiz: %p hookID %d  %d", env, thiz, hookID);

    void *thread = ArtInternals::GetCurrentThread();
    if (thread == nullptr) {
        //LOGE("Thread::CurrentFromGdb returned NULL");
        return 0;
    }
    //LOGI("thread:%p", thread);


    char gc[256];
    ArtInternals::SGCFn(gc, thread, GcCause::kGcCauseDebugger,
                        CollectorType::kCollectorTypeDebugger);


    auto hookInfo = VectorStore<hooked_function>::Instance().CopyByIndex(hookID);
    std::mutex &mtx = HookIdLockManager::Instance().GetMutex(hookID);
    std::lock_guard<std::mutex> lock(mtx);

    //在这里，已经拿到了hook信息和参数了，可以调用lua函数了。
    //调用lua函数，我们要先把参数打包成一个table，然后把这个table直接传递过去，用户的lua脚本怎么用让用户决定。
    //等到从lua返回，再解包返回的table，构造args，传入函数。
    //构造table只需要根据shorty进行就行了
    std::vector<sol::object> lua_args_vector = {};
    int x_reg_count = 2;//x0和x1本来就被占用了，所以从x2开始
    int v_reg_count = 0;//这里是记录V和X寄存器的个数，如果超过7，要从栈上取
    int stack_reg_count = 0;
    sol::table lua_arg_table = LUA::lua->create_table();

    float float_value = 0;
    double double_value = 0;
    uint64_t x_value = 0;
    for (size_t i = 1; i < hookInfo.shorty.size(); ++i) {
        //开始遍历shorty，注意跳过了返回值，index从1开始。这也适配了lua的数组，从1开始
        char type = hookInfo.shorty[i];
        sol::object param_object = {};
        switch (type) {
            case 'F'://float和double优先走V寄存器，其他的不管是指针还是值都走X寄存器
                if (v_reg_count == 0)
                    float_value = *(float *) &v0;
                else if (v_reg_count == 1)
                    float_value = *(float *) &v1;
                else if (v_reg_count == 2)
                    float_value = *(float *) &v2;
                else if (v_reg_count == 3)
                    float_value = *(float *) &v3;
                else if (v_reg_count == 4)
                    float_value = *(float *) &v4;
                else if (v_reg_count == 5)
                    float_value = *(float *) &v5;
                else if (v_reg_count == 6)
                    float_value = *(float *) &v6;
                else if (v_reg_count == 7)
                    float_value = *(float *) &v7;
                else {//从栈取
                    float_value = *(float *) ((uint64_t) args_in + stack_reg_count * 8);
                }

                if (v_reg_count > 7) {
                    stack_reg_count += 1;
                }
                v_reg_count++;
                param_object = sol::make_object(*LUA::lua, (float) float_value);

                break;

            case 'D':
                if (v_reg_count == 0)
                    double_value = *(double *) &v0;
                else if (v_reg_count == 1)
                    double_value = *(double *) &v1;
                else if (v_reg_count == 2)
                    double_value = *(double *) &v2;
                else if (v_reg_count == 3)
                    double_value = *(double *) &v3;
                else if (v_reg_count == 4)
                    double_value = *(double *) &v4;
                else if (v_reg_count == 5)
                    double_value = *(double *) &v5;
                else if (v_reg_count == 6)
                    double_value = *(double *) &v6;
                else if (v_reg_count == 7)
                    double_value = *(double *) &v7;
                else {//从栈取
                    double_value = *(double *) ((uint64_t) args_in + stack_reg_count * 8);
                }

                if (v_reg_count > 7) {
                    stack_reg_count += 1;
                }
                v_reg_count++;
                param_object = sol::make_object(*LUA::lua, (double) double_value);
                break;
            default:
                if (x_reg_count == 2)
                    x_value = *(uint64_t *) &x2;
                else if (x_reg_count == 3)
                    x_value = *(uint64_t *) &x3;
                else if (x_reg_count == 4)
                    x_value = *(uint64_t *) &x4;
                else if (x_reg_count == 5)
                    x_value = *(uint64_t *) &x5;
                else if (x_reg_count == 6)
                    x_value = *(uint64_t *) &x6;
                else if (x_reg_count == 7)
                    x_value = *(uint64_t *) &x7;
                else {//从栈取
                    x_value = *(uint64_t *) ((uint64_t) args_in + stack_reg_count * 8);
                }
                param_object = sol::make_object(*LUA::lua, (int64_t) x_value);
                if (x_reg_count > 7) {
                    stack_reg_count += 1;
                }
                x_reg_count++;
        }
        lua_arg_table[i] = param_object;
    }

    sol::protected_function_result lua_func_result = (*LUA::lua)[hookInfo.enterfuncname.c_str()](
            lua_arg_table);

    bool callOriginalMessage;
    sol::table modifiedArgs;
    sol::object directReturnValue;

    if (lua_func_result.valid()){
        // 手动解构 第一个是是否调用原函数，第二个是改后的参数表，第三个是直接返回值。第三个是不调用原函数的时候才用。
        callOriginalMessage = lua_func_result.get<bool>(0);
        modifiedArgs = lua_func_result.get<sol::table>(1);
        directReturnValue = lua_func_result.get<sol::object>(2);
    }
    else {
        callOriginalMessage = true;
        sol::error err = lua_func_result;
        auto err_debug = err.what();
        modifiedArgs = lua_arg_table;
        //LUA error了直接原封不动调用原函数
        std::string luaerr = std::string("[Lua] Error:") +  err_debug;
        Commands::tcp_log(luaerr);
    }
    //再往后是调用原函数的逻辑，应该根据lua中是否要调用原函数再决定是否执行。
    //这里是如果不执行，那么直接返回值即可
    if (!callOriginalMessage) {//直接返回值
        ArtInternals::DestroyGCFn(gc);
        if (directReturnValue.is<int>()) {
            return directReturnValue.as<int>();
        } else if (directReturnValue.is<int64_t>()) {
            return directReturnValue.as<int64_t>();
        } else if (directReturnValue.is<bool>()) {
            return directReturnValue.as<bool>() ? 1 : 0;
        } else if (directReturnValue.is<double>()) {
            if (hookInfo.shorty[0] == 'F') {
                float ret = directReturnValue.as<float>();
                asm volatile("fmov s0, %s0" : : "w"(ret));
                return 0;
            } else if (hookInfo.shorty[0] == 'D') {
                double ret = directReturnValue.as<double>();
                asm volatile("fmov d0, %d0" : : "w"(ret));
                return 0;
            }
        }
        //这里应该提示异常
        return 0;
    }

    //如果要执行，先构造args数组，准备传递给原函数，再开启调用流程。
    //分配足够的内存，每个参数都给
    auto args = new uint32_t[(modifiedArgs.size() + 2) * 8];
    memset(args, 0, sizeof(uint32_t) * (modifiedArgs.size() + 2) * 8);
    uint32_t argsize = 0;

    if (env->IsSameObject(thiz, nullptr)) {
        // 无效，说明 thiz/clazz 已经悬空（被回收）
        ArtInternals::DestroyGCFn(gc);
        return 0;
    }

    jclass clazz = nullptr;
    if (!hookInfo.isStatic) {
        clazz = env->GetObjectClass(thiz);
        //动态方法，参数必须带thiz
        args[0] = *(uint32_t *) thiz;  // StackReference<T>* 地址，32位
        argsize += 4;
    } else {
        clazz = (jclass) thiz;
        //静态方法，直接传参数
    }


    //这里开始parse来自lua的参数表，构造参数数组
    int checkShorty_index = 1;//顺便从1开始检查shorty，确保对应
    for (auto &p: modifiedArgs) {
        if (hookInfo.shorty[checkShorty_index] == 'F') {//float
            if (p.second.is<float>()) {
                float value = p.second.as<float>();
                memcpy((void *) ((uint64_t) args + argsize), &value, sizeof(float));
            }
            argsize += 4;
        } else if (hookInfo.shorty[checkShorty_index] == 'Z') {//boolean
            if (p.second.is<bool>()) {
                bool value = p.second.as<bool>();
                memcpy((void *) ((uint64_t) args + argsize), &value, sizeof(bool));
            }
            argsize += 4;
        } else if (hookInfo.shorty[checkShorty_index] == 'B') {//byte
            if (p.second.is<jbyte>()) {
                jbyte value = p.second.as<jbyte>();//其实就是signed char
                memcpy((void *) ((uint64_t) args + argsize), &value, sizeof(jbyte));
            }
            argsize += 4;
        } else if (hookInfo.shorty[checkShorty_index] == 'C') {//char
            if(p.second.is<jchar>()) {
                jchar value = p.second.as<jchar>();
                memcpy((void *) ((uint64_t) args + argsize), &value, sizeof(jchar));
            }
            if(p.second.is<char>()){
                char value = p.second.as<char>();
                memcpy((void *) ((uint64_t) args + argsize), &value, sizeof(char));
            }
            argsize += 4;
        } else if (hookInfo.shorty[checkShorty_index] == 'S') {//short
            if (p.second.is<short>()) {
                short value = p.second.as<short>();
                memcpy((void *) ((uint64_t) args + argsize), &value, sizeof(short));
            }
            argsize += 4;
        } else if (hookInfo.shorty[checkShorty_index] == 'I') {//int
            if (p.second.is<int>()) {
                int value = p.second.as<int>();
                memcpy((void *) ((uint64_t) args + argsize), &value, sizeof(int));
            }
            argsize += 4;
        } else if (hookInfo.shorty[checkShorty_index] == 'L') {//对象，全部都是uint32_t
            if (p.second.is<uint64_t>() ) {
                uint64_t value = p.second.as<uint64_t>();
                uint32_t comporessedPtr = *(uint32_t *) value;
                memcpy((void *) ((uint64_t) args + argsize), &comporessedPtr, sizeof(uint32_t));
            }
            argsize += 4;
        } else if (hookInfo.shorty[checkShorty_index] == 'D') {//double
            if (p.second.is<double>()) {
                double value = p.second.as<double>();
                memcpy((void *) ((uint64_t) args + argsize), &value, sizeof(double));
            }
            argsize += 8;
        } else if (hookInfo.shorty[checkShorty_index] == 'J') {//long
            if (p.second.is<int64_t>()) {
                int64_t value = p.second.as<int64_t>();
                memcpy((void *) ((uint64_t) args + argsize), &value, sizeof(int64_t));
            }
            argsize += 8;
        }
        checkShorty_index++;
    }


    auto [methodID, shorty] = Class_Method_Finder::findJMethodIDByName(env,clazz,
                                                                       hookInfo.name.c_str(),
                                                                       hookInfo.shorty.c_str(),
                                                                       hookInfo.isStatic

                                                                       );
    void* artMethod = nullptr;
    if (!methodID) {
        LOGE("GetMethodID failed in hook");
        artMethod=hookInfo.ArtMethod;
    }
    else
        artMethod = ArtInternals::DecodeFunc(ArtInternals::jniIDManager, methodID);

    //LOGI("GetMethodID succeeded: %s %p, shorty:%s", hookInfo.name.c_str(), methodID,
    //     shorty.c_str());



    //auto artMethod = hookInfo.ArtMethod;

    //LOGI("artMethod : %p", artMethod);


    /*我还不确定复制一份再恢复，然后用假的invoke 和直接恢复原本的有什么区别 二者看起来都是可以的，但高频率调用下很快就会崩溃
    //在原方法上进行unhook操作，然后立刻invoke，invoke完成之后，先hook再进行一个usleep，稳定性有所提升
    //后来发现复制一份出来稳定性也还可以
    //最后尝试让GC不回收这个线程的垃圾，稳定性进一步提升。
    //GC不回收 + 拷贝副本恢复执行，目前两个hook总计调用70000次 750秒，多次出现日志
    Waiting for a blocking GC NativeAlloc
    2025-06-21 19:27:36.182 11812-11883 nnette.girlhook         com.lynnette.girlhook                I  WaitForGcToComplete blocked NativeAlloc on Background for 48.651ms
     */
    //姑且认为崩溃行为大概率和内存free有关。如果用profiler查看内存，attach上的瞬间也会崩溃。
    auto tocallOrigin = new uint8_t[hookInfo.Layout.art_method_size];
    memcpy(tocallOrigin, artMethod, hookInfo.Layout.art_method_size);

    /*
    uint32_t newflag = *reinterpret_cast<uint32_t*>((char*)artMethod + hookInfo.Layout.offset_access_flags);
    uint64_t newEntryPointQuick = *reinterpret_cast<uint64_t*>((char*)artMethod + hookInfo.Layout.offset_entry_quick);
    uint64_t newEntryJNI = *reinterpret_cast<uint64_t*>((char*)artMethod + hookInfo.Layout.offset_entry_jni);
    */

    recover_artmethod(tocallOrigin, hookInfo, true);
    /*Logger::hex_dump_log(tocallOrigin, hookInfo.Layout.art_method_size);
    Logger::hex_dump_log(artMethod, hookInfo.Layout.art_method_size);*/


    jvalue result;

    //usleep(10 * 1000); // 10ms
    //LOGI("ARGS:%p, arg0this %x argSize:%d result:%p shorty:%s", args, args[0], argsize, result, shorty.c_str());

    ArtInternals::Invoke(tocallOrigin, thread, (uint32_t *) args, argsize, &result,hookInfo.shorty.c_str());

    /*
    *reinterpret_cast<uint32_t*>((char*)artMethod + hookInfo.Layout.offset_access_flags) = newflag;
    *reinterpret_cast<uint64_t*>((char*)artMethod + hookInfo.Layout.offset_entry_quick) = newEntryPointQuick;
    *reinterpret_cast<uint64_t*>((char*)artMethod + hookInfo.Layout.offset_entry_jni) = newEntryJNI;
    usleep(500);
    Logger::hex_dump_log(tocallOrigin, hookInfo.Layout.art_method_size);
    Logger::hex_dump_log(hookInfo.ArtMethod, hookInfo.Layout.art_method_size);
     */


    delete[] tocallOrigin;
    delete[] args;
    uint64_t ret;//统一return值

    ArtInternals::DestroyGCFn(gc);

    //这里我们已经拿到了返回值，应该接着进入lua处理，lua可以修改返回值。我们要传递给lua的只有返回值这一个内容
    if (hookInfo.shorty[0] == 'F') {
        float fret = result.f;
        sol::protected_function_result lua_ret_func_result_ = (*LUA::lua)[hookInfo.leavefuncname.c_str()](fret);
        if (lua_ret_func_result_.valid())
            fret = lua_ret_func_result_.get<float>(0);
        asm volatile("fmov s0, %s0" : : "w"(fret));

        sol::error err = lua_ret_func_result_;
        std::string retFuncError = std::string("[Lua] Error") + err.what();
        Commands::tcp_log(retFuncError);

        return 0;
    } else if (hookInfo.shorty[0] == 'D') {
        double dret = result.d;
        sol::protected_function_result lua_ret_func_result_=  (*LUA::lua)[hookInfo.leavefuncname.c_str()](dret);
        if (lua_ret_func_result_.valid()) {
            dret = lua_ret_func_result_.get<double>(0);
        }
        asm volatile("fmov d0, %d0" : : "w"(dret));

        sol::error err = lua_ret_func_result_;
        std::string retFuncError = std::string("[Lua] Error") + err.what();
        Commands::tcp_log(retFuncError);

        return 0;
    }
    else if (hookInfo.shorty[0] == 'Z'){
        ret = result.z;
    }
    else if (hookInfo.shorty[0] == 'B'){
        ret = result.b;
    }
    else if (hookInfo.shorty[0] == 'C'){
        ret = result.c;
    }
    else if (hookInfo.shorty[0] == 'S'){
        ret = result.s;
    }
    else if (hookInfo.shorty[0] == 'I'){
        ret = result.i;
    }
    else if (hookInfo.shorty[0] == 'J'){
        ret = result.j;
    }
    else if (hookInfo.shorty[0] == 'L'){
        ret = (uint64_t)ArtInternals::newlocalrefFn(env, result.l);
    }
    else if (hookInfo.shorty[0] == 'V'){
        return 0;
    }
    sol::protected_function_result lua_ret_func_result = (*LUA::lua)[hookInfo.leavefuncname.c_str()](ret);
    //从lua返回，拿到用户修改后的返回值，并返回
    if (lua_ret_func_result.valid()) {
        return lua_ret_func_result.get<uint64_t>(0);
    }
    //这里是返回函数处理错误了，log一下，把原来的返回
    sol::error err = lua_ret_func_result;
    std::string retFuncError = std::string("[Lua] Error") + err.what();
    Commands::tcp_log(retFuncError);
    return (uint64_t)ret;
}
/*
    不再使用的globalreference，会导致GC tried to mark invalid reference崩溃。
    JavaVM* vm = nullptr;
    env->GetJavaVM(&vm);
    auto ref_1 = ArtInternals::newGlobalrefFn(vm,thread,  (void*)thiz);
    ArtInternals::deleteGlobalrefFn(vm, thread,ref_1);

 */
__attribute__((naked)) void hook_trampoline_ex() {
    asm volatile(
            "mov x15, x0\n"                // 保存 hook_id 到 x9
            "ldp x0, x1, [sp], #16\n"     // x0是env x1是class或this
            "b hooks_handler\n"
            );
}




void unhook_all(){
    JavaEnv Env;//触发线程attach
    for (int i = 0; i < VectorStore<hooked_function>::Instance().Size(); i++){
        unhook(i);
    }
}