// native_hook.cpp
#include <jni.h>
#include <thread>
#include <cstdio>
#include "Utility/GirlLog.h"
#include "JVM/JVM.h"
#include "Hook/Hook.h"
#include "Caller/Caller.h"
#include "Bridge/bridge.h"
#include "Communicate/Communicate.h"
#include "json/json.h"
#include "Commands/Commands.h"

#define BUFFER_SIZE 8192


//hook_java_method("com/lynnette/girlhook/MainActivity",
//                 "loopFunction",false);

//hook_java_method("com/lynnette/girlhook/MainActivity",
//             "loopFunction_static",true);
//sleep(4);
//unhook_all();
void main_thread(){
    sleep(5);
    LUA::init_lua_bridge();
    bool ok = ArtInternals::Init();
    if (!ok){
        LOGE("Unsupported System! Please set your offsets in Findclass.cpp");
        return;
    }
    JavaEnv env;
    Class_Method_Finder::iterate_class_info(env.get());
    sleep(5);
    while(1){
        char* buffer = nullptr;
        if (Communicate::getInstance().read(&buffer)) {
            Commands::parse_command(std::string(buffer));
        }
        //Communicate::getInstance().add("Hello");
        if (Communicate::getInstance().write()){
            usleep(1000*100);
        }
        if (buffer != nullptr)
            delete[] buffer;
    }

}

__attribute__((constructor()))
void initialize_globals() {
    LOGI("initialize_globals_test");
    std::thread(main_thread).detach();
}
__attribute__((destructor()))
void destroy_globals() {
    unhook_all();
    LOGD("destroy_globals_test");
}
extern "C"
JNIEXPORT void JNICALL
Java_com_lynnette_girlhook_MainActivity_printEnvAndThiz(JNIEnv* env, jobject thiz) {
    LOGI("Native printEnvAndThiz called");
    LOGI("ENV: %p", env);
    LOGI("THIZ: %p", thiz);
}

