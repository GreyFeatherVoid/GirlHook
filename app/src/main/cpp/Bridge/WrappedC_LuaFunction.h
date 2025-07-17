//
// Created by Lynnette on 2025/6/24.
//

#ifndef GIRLHOOK_WRAPPEDC_LUAFUNCTION_H
#define GIRLHOOK_WRAPPEDC_LUAFUNCTION_H
#include <jni.h>
#include <sol/sol.hpp>
#include "../JVM/JVM.h"
#include "bridge.h"
#include "../Commands/Commands.h"
#include <set>
#include "../include/jvmti.h"


namespace WRAP_C_LUA_FUNCTION {
    void LUA_LOG(sol::this_state ts, sol::variadic_args args);

    sol::table jobject_to_luatable_trampoline(sol::this_state ts, sol::variadic_args args);

    sol::table javaarray_to_luatable_trampoline(sol::this_state ts, sol::variadic_args args);

    sol::table javalist_to_luatable_trampoline(sol::this_state ts, sol::variadic_args args);

    void apply_soltable_to_existing_jobject_trampoline(sol::this_state ts, sol::variadic_args args);

    void
    apply_soltable_to_existing_javaarray_trampoline(sol::this_state ts, sol::variadic_args args);

    void apply_soltable_to_existing_javalist_trampoline(sol::this_state
    ts,
    sol::variadic_args args
    );

    std::string getJavaStringContent(sol::object solobj);
    //String不可变，要修改只能用Create
    int64_t createJavaString(sol::object solobj, sol::object solstr);

    sol::table  find_class_instance(sol::this_state ts, sol::variadic_args solargs);
    sol::object call_java_function(sol::this_state ts, sol::variadic_args solargs);
}

#endif //GIRLHOOK_WRAPPEDC_LUAFUNCTION_H
