//
// Created by Lynnette on 2025/6/23.
//
#include "bridge.h"

sol::state* LUA::lua = nullptr;

void LUA::init_lua_bridge(){
    //使用lua基本库
    lua = new sol::state;
    lua->open_libraries(sol::lib::base);
    //绑定一些自定义函数
    LUA::lua->set_function("print", &WRAP_C_LUA_FUNCTION::LUA_LOG);
    LUA::lua->set_function("jobject_to_luatable", &WRAP_C_LUA_FUNCTION::jobject_to_luatable_trampoline);
    LUA::lua->set_function("javaarray_to_luatable", &WRAP_C_LUA_FUNCTION::javaarray_to_luatable_trampoline);
    LUA::lua->set("javalist_to_luatable",&WRAP_C_LUA_FUNCTION::javalist_to_luatable_trampoline);
    LUA::lua->set_function("apply_soltable_to_existing_jobject", &WRAP_C_LUA_FUNCTION::apply_soltable_to_existing_jobject_trampoline);
    LUA::lua->set_function("apply_soltable_to_existing_javaarray", &WRAP_C_LUA_FUNCTION::apply_soltable_to_existing_javaarray_trampoline);
    LUA::lua->set("apply_soltable_to_existing_javalist",&WRAP_C_LUA_FUNCTION::apply_soltable_to_existing_javalist_trampoline);

    LUA::lua->set("getJavaStringContent",&WRAP_C_LUA_FUNCTION::getJavaStringContent);
    LUA::lua->set("createJavaString",&WRAP_C_LUA_FUNCTION::createJavaString);

    LUA::lua->script(R"(
    function hookTester(args)
        local testStruct = jobject_to_luatable(args[1])
        local darray = javaarray_to_luatable(args[2])
        local sarray = javaarray_to_luatable(args[3])
        local struct_array = javaarray_to_luatable(args[4])
        testStruct["tlong"] = 1
        print(testStruct)
        print(darray)
        print(sarray)
        struct_array[1]["tint"] = 8

        darray[1] = 0.001
        darray[2] = 0.002

        sarray[1] = "我试着修改"
        apply_soltable_to_existing_jobject(testStruct, args[1])
        apply_soltable_to_existing_javaarray(sarray, args[3])
        apply_soltable_to_existing_javaarray(darray, args[2])
        apply_soltable_to_existing_javaarray(struct_array, args[4])


        return true, args, 0
    end
    )");
    //args[15]是jchar，也支持char，也就是utf8编码

    LUA::lua->script(R"(
    function hookret(oret)
        print(oret)
        return oret
    end
    )");

}

void bridgeTest() {

    const std::string script = R"(
        function add(a, b)
            return a + b
        end

        local result = add(10, 32)
        print("Result of add(10, 32) = " .. result)
        return result
    )";

    // 执行脚本
    sol::protected_function_result result = LUA::lua->safe_script(script, sol::script_pass_on_error);

    if (!result.valid()) {
        sol::error err = result;
        LOGI("[C++] Lua 执行错误: %s", err.what());
    } else {
        int return_value = result; // 获取脚本返回值
        LOGI("[C++] Lua 返回值: %d", return_value);
    }
}