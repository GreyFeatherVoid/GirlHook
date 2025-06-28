#include "test.h"

void testJNIEnv(JNIEnv* env) {
    if (!env) {
        LOGE("JNIEnv is null");
        return;
    }
    jclass logClass = env->FindClass("android/util/Log");
    if (!logClass) {
        LOGE("Failed to find Log class");
        return;
    }

    jmethodID logIMethod = env->GetStaticMethodID(logClass, "i", "(Ljava/lang/String;Ljava/lang/String;)I");
    if (!logIMethod) {
        LOGE("Failed to find Log.i method");
        env->DeleteLocalRef(logClass);
        return;
    }

    jstring tag = env->NewStringUTF("GirlHook");
    jstring msg = env->NewStringUTF("JNIEnv and JVM test successful!");

    env->CallStaticIntMethod(logClass, logIMethod, tag, msg);

    env->DeleteLocalRef(tag);
    env->DeleteLocalRef(msg);
    env->DeleteLocalRef(logClass);

    LOGI("testJNIEnv finished");
}


void testJavaVMThread(JavaVM* vm) {
    if (!vm) {
        LOGE("JavaVM is null");
        return;
    }
    std::thread([vm]{
        JNIEnv* env = nullptr;
        if (vm->AttachCurrentThread(&env, nullptr) != JNI_OK || !env) {
            LOGE("AttachCurrentThread failed");
            return;
        }

        testJNIEnv(env);

        vm->DetachCurrentThread();
    }).detach();
}

void test_get_env() {
    JavaEnv env;
    if (env.isNull()) {
        LOGE("Failed to get env");
        return;
    }

    jclass cls = env->FindClass("java/lang/String");
    if (cls) {
        LOGI("FindClass success");
    }
    testJavaVMThread(env.getJVM());
}