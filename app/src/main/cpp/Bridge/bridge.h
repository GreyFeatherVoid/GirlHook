//
// Created by Lynnette on 2025/6/23.
//

#ifndef GIRLHOOK_BRIDGE_H
#define GIRLHOOK_BRIDGE_H

#include <sol/sol.hpp>
#include <string>
#include <iostream>
#include "../Utility/GirlLog.h"
#include "../JVM/JVM.h"
#include "WrappedC_LuaFunction.h"


namespace LUA{
    extern sol::state* lua;
    void init_lua_bridge();
}
void bridgeTest();

#endif //GIRLHOOK_BRIDGE_H
