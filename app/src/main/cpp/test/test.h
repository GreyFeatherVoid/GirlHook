#ifndef GIRLHOOK_TEST_H
#define GIRLHOOK_TEST_H

#include "../JVM/JVM.h"
#include "../Utility/GirlLog.h"
#include <jni.h>
#include <thread>
#include <cstdio>

void testJNIEnv(JNIEnv* env);
void testJavaVMThread(JavaVM* vm);
void test_get_env();

#endif //GIRLHOOK_TEST_H
