//
// Created by Lynnette on 2025/6/26.
//

#ifndef GIRLHOOK_COMMANDS_H
#define GIRLHOOK_COMMANDS_H
#include "../json/json.h"
#include "../Hook/Hook.h"
#include "../Utility/GirlLog.h"
#include "../GlobalStore/GlobalStore.h"
#include "../Communicate/Communicate.h"
namespace Commands{
    void parse_command(const std::string& inData);
    void tcp_log(const std::string& logdata);
}
#endif //GIRLHOOK_COMMANDS_H
