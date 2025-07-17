//
// Created by Lynnette on 2025/6/26.
//
#define RESULT "result"
#define COMMAND "command"
#define CLASSLIST "classes"
#define REFRESH_ALL_CLASS "GET_ALL_CLASS"

//由类名获取方法
#define REFRESH_ALL_METHODS "GET_ALL_METHODS"
#define CLASSNAME "class_name"
#define METHODS "methods"

//安装hook
#define INSTALL_HOOK "INSTALL_HOOK"
#define INSTALLED_HOOK_INFO "installed_hook_info"

//获取所有hook
#define GET_ALL_HOOKS "GET_ALL_HOOKS"
#define HOOKS "hooks"

//LOG
#define TCPLOG "TCP_Log"
#define LOGCONTENT "logs"

//卸载Hook
#define UNINSTALL_HOOK "UNINSTALL_HOOK"
#define UNINSTALL_FULLNAME "UNINSTALL_FULLNAME"

//脱壳
#define DUMP_DEX "DUMP_DEX"

//直接执行脚本
#define EXCUTE_SCRIPT  "EXCUTE_SCRIPT"
#define SCRIPT "script"

#include "Commands.h"
using json = nlohmann::json;

void Commands::parse_command(const std::string& inData){
    //进来的一定是json格式
    LOGI("[命令] 收到数据: %s" ,inData.c_str());
    json j = json::parse(inData);
    std::string command = j.value(COMMAND, "unknown");

    json result_json;
    result_json[RESULT] = 0;
    JavaEnv myenv;
    if (command == REFRESH_ALL_CLASS){
        result_json[RESULT] = 1;
        Class_Method_Finder::iterate_class_info(myenv.get());
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& c :  UnorderedStore<CLASSNAMETYPE>::Instance().GetAll()){
            arr.push_back(c.c_str());
        }
        result_json[CLASSLIST] = arr;
        result_json[COMMAND] = REFRESH_ALL_CLASS;
    }
    if (command == REFRESH_ALL_METHODS){
        std::string target_classname = j.value(CLASSNAME, "unknown");
        JavaEnv myEnv;
        jclass myjclass = Class_Method_Finder::FindClassViaLoadClass(myEnv.get(),target_classname.c_str());
        result_json[CLASSNAME] = target_classname;
        if (myjclass){
            result_json[RESULT] = 1;
            std::vector<std::string> methods;
            Class_Method_Finder::iterate_all_method_from_jclass(myEnv.get(),myjclass, methods);
            nlohmann::json arr = nlohmann::json::array();
            for (const auto & method : methods){
                arr.push_back(method);
            }
            result_json[METHODS] = arr;
        }
        else {
            result_json[RESULT] = 0;
        }
        result_json[COMMAND] = REFRESH_ALL_METHODS;
    }
    if (command == INSTALL_HOOK){
        //在这里进行hook的安装数据格式如下
        /*
        data = {
                'className': classname
                'hookFunction': name,
                "org_fullname" : 类名+/+方法名
                'shorty': shorty,
                'is_static': is_static,
                'onEnter_FuncName': funcname_enter,
                'onLeave_FuncName': funcname_leave,
                'script': luafunctions,
        }
         */
        std::string classname = j.value("className","unknown");
        std::string hookFunction = j.value("hookFunction", "unknown");
        std::string shorty = j.value("shorty","unknown");
        bool is_static = j.value("is_static",false);
        std::string onEnterFuncName = j.value("onEnter_FuncName","unknown");
        std::string onLeaveFuncName = j.value("onLeave_FuncName","unknown");
        std::string script = j.value("script","unknown");
        std::string org_fullname = j.value("org_fullname", "unknown");
        LOGI("[通信] Hook内容 %s %s %s %s %d %s %s %s",org_fullname.c_str(), classname.c_str(), hookFunction.c_str(),
             shorty.c_str(),is_static,onEnterFuncName.c_str(),
             onLeaveFuncName.c_str(),script.c_str());
        result_json[COMMAND] = j.value(COMMAND, "unknown");
        j.erase(COMMAND);
        result_json[INSTALLED_HOOK_INFO] = j;
        result_json[RESULT] = hook_java_method(
                        org_fullname.c_str(),
                        classname.c_str(),
                         hookFunction.c_str(),
                         shorty.c_str(),
                         is_static,
                         onEnterFuncName.c_str(),
                         onLeaveFuncName.c_str(),
                        script.c_str());
        //将hook信息原封不动的传输回去，而Result表示是否成功，来与客户端的数据进行同步
    }
    if (command == GET_ALL_HOOKS){
        //客户端请求查看所有已经安装的钩子
        nlohmann::json arr = nlohmann::json::array();
        /*
         * data = {
                'className': classname
                'hookFunction': name,
                "org_fullname" : 类名+/+方法名
                'shorty': shorty,
                'is_static': is_static,
                'onEnter_FuncName': funcname_enter,
                'onLeave_FuncName': funcname_leave,
                'script': luafunction,
        }
         * */
        for (const auto& hook : VectorStore<hooked_function>::Instance().GetAll()){
            if (!hook.Valid) continue;
            json onehook;
            onehook["className"] = hook.classname;
            onehook["hookFunction"] = hook.name;
            onehook["org_fullname"] = hook.org_fullname;
            onehook["shorty"] = hook.shorty;
            onehook["is_static"] = hook.isStatic;
            onehook["onEnter_FuncName"] = hook.enterfuncname;
            onehook["onLeave_FuncName"] = hook.leavefuncname;
            onehook["script"] = hook.script;
            arr.push_back(onehook);
        }
        result_json[RESULT] = 1;
        result_json[HOOKS] = arr;
        result_json[COMMAND] = GET_ALL_HOOKS;
    }
    if (command == UNINSTALL_HOOK){
        std::string fullname = j.value(UNINSTALL_FULLNAME, "unknown");
        int index = 0;
        for (const auto& hook : VectorStore<hooked_function>::Instance().GetAll()){
            if (hook.org_fullname == fullname){
                unhook(index);
                result_json[RESULT] = 1;
                result_json[UNINSTALL_FULLNAME] = fullname;
                result_json[COMMAND] = UNINSTALL_HOOK;
                break;
            }
            index++;
        }
    }
    if (command == DUMP_DEX){
        Class_Method_Finder::dumpDexes();
        result_json[RESULT] = 1;
        result_json[COMMAND] = DUMP_DEX;
        tcp_log("Dump完成: /data/data/<包名>/girldump");
    }
    if (command == EXCUTE_SCRIPT){
        result_json[RESULT] = 1;
        result_json[COMMAND] = EXCUTE_SCRIPT;
        sol::protected_function_result result = LUA::lua->safe_script(j.value(SCRIPT, "unknown"), sol::script_pass_on_error);
        if (!result.valid()) {
            sol::error err = result;
            tcp_log(std::string("脚本执行错误") + err.what());
        }
    }
    Communicate::getInstance().add(result_json.dump());
}

void Commands::tcp_log(const std::string& logdata){
    json result_json;
    result_json[RESULT] = 1;
    result_json[COMMAND] = TCPLOG;
    result_json[LOGCONTENT] = logdata;
    Communicate::getInstance().add(result_json.dump());
}