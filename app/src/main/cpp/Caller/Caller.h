//
// Created by Lynnette on 2025/6/18.
//

#ifndef GIRLHOOK_CALLER_H
#define GIRLHOOK_CALLER_H

#include "../Utility/GirlLog.h"
#include <jni.h>
#include <android/log.h>
#include <unistd.h>
#include <cstring>
#include <dlfcn.h>
#include "../JVM/JVM.h"
#include "../Utility/FindClass.h"

// ---------- JNI 签名映射 ---------- //
template<typename T> struct JNISignature;

#define DEFINE_JNI_SIG(cpp_type, sig) \
    template<> struct JNISignature<cpp_type> { static constexpr const char* value = sig; }

DEFINE_JNI_SIG(void, "V");
DEFINE_JNI_SIG(jboolean, "Z");
DEFINE_JNI_SIG(jbyte, "B");
DEFINE_JNI_SIG(jchar, "C");
DEFINE_JNI_SIG(jshort, "S");
DEFINE_JNI_SIG(jint, "I");
DEFINE_JNI_SIG(jlong, "J");
DEFINE_JNI_SIG(jfloat, "F");
DEFINE_JNI_SIG(jdouble, "D");
DEFINE_JNI_SIG(jstring, "Ljava/lang/String;");
DEFINE_JNI_SIG(jobject, "Ljava/lang/Object;");
DEFINE_JNI_SIG(jclass, "Ljava/lang/Class;");
DEFINE_JNI_SIG(jbyteArray, "[B");

// ---------- 参数签名拼接 ---------- //
template<typename... Args> struct JNISignatureBuilder;

template<> struct JNISignatureBuilder<> {
    static std::string build() { return ""; }
};

template<typename First, typename... Rest>
struct JNISignatureBuilder<First, Rest...> {
    static std::string build() {
        return std::string(JNISignature<First>::value) + JNISignatureBuilder<Rest...>::build();
    }
};

template<typename Ret, typename... Args>
std::string GetJNISignature() {
    return "(" + JNISignatureBuilder<Args...>::build() + ")" + JNISignature<Ret>::value;
}

template<typename Ret, typename... Args>
Ret CallStaticJavaMethod(JNIEnv* env,
                         const char* className,
                         const char* methodName,
                         char* method_sig,
                         Args... args) {
    std::string sig;
    if (method_sig == nullptr)
        sig = GetJNISignature<Ret, Args...>();
    else
        sig = method_sig;
    LOGI("SIGNATURE: %s", sig.c_str());
    jclass clazz = Class_Method_Finder::FindClassViaLoadClass(env, className);
    if (env->ExceptionCheck()) {
        env->ExceptionDescribe();
        env->ExceptionClear();
    }
    if (!clazz) {
        LOGE("FindClass failed: %s", className);
        return Ret();
    }
    LOGE("FindClass success: %s", className);
    jmethodID methodID = env->GetStaticMethodID(clazz, methodName, sig.c_str());
    if (env->ExceptionCheck()) {
        env->ExceptionDescribe();
        env->ExceptionClear();
    }
    if (!methodID) {
        LOGE("GetStaticMethodID failed: %s %s", methodName, sig.c_str());
        return Ret();
    }
    LOGE("Find method success: %s", methodName);

    if constexpr (std::is_same_v<Ret, void>) {
        env->CallStaticVoidMethod(clazz, methodID, args...);
    } else if constexpr (std::is_same_v<Ret, jboolean>) {
        return env->CallStaticBooleanMethod(clazz, methodID, args...);
    } else if constexpr (std::is_same_v<Ret, jint>) {
        return env->CallStaticIntMethod(clazz, methodID, args...);
    } else if constexpr (std::is_same_v<Ret, jlong>) {
        return env->CallStaticLongMethod(clazz, methodID, args...);
    } else if constexpr (std::is_same_v<Ret, jfloat>) {
        return env->CallStaticFloatMethod(clazz, methodID, args...);
    } else if constexpr (std::is_same_v<Ret, jdouble>) {
        return env->CallStaticDoubleMethod(clazz, methodID, args...);
    } else if constexpr (std::is_same_v<Ret, jobject>) {
        return env->CallStaticObjectMethod(clazz, methodID, args...);
    } else {
        static_assert(sizeof(Ret) == 0, "Unsupported return type");
    }
    return Ret(); // fallback
}


#endif //GIRLHOOK_CALLER_H
