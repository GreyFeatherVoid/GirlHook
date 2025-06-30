//
// Created by Lynnette on 2025/6/24.
//

/*
 * 规则应该是这样：
 * 对于一切的自定义类，直接传入jobject_to_luatable。可以把里面
 * 所有基础类型都转换出来，也可以修改，并写回去。
 * 但对于一切稍微复杂的类型，比如List，Map，以及嵌套的数组和自定义类，这时候
 * 不进行解析，不然嵌套太多，而是直接显示jobject指针。
 * jobject指针，可以根据类型，让用户自己再用各种处理函数
 *
 * 到最后应用修改的时候，传递进入的jobejct，应该只能保存本层基础类型的修改
 * 保留不了嵌套类、嵌套数组的修改。这些内容应该全部传入嵌套的jobject单独进行应用
 */

#include "WrappedC_LuaFunction.h"
sol::table javaarray_to_luatable(sol::this_state ts, jobject arrObj);
std::string safe_to_string(const sol::object& obj);

std::string dump_table(const sol::table& tbl) {
    std::string out = "{ ";
    for (auto& pair : tbl) {
        std::string key = safe_to_string(pair.first);
        std::string val = safe_to_string(pair.second);
        out += key + " = " + val + ", ";
    }
    out += "}";
    return out;
}


std::string safe_to_string(const sol::object& obj) {
    switch (obj.get_type()) {
        case sol::type::string:
            return obj.as<std::string>();
        case sol::type::number:
            return std::to_string(obj.as<double>());
        case sol::type::boolean:
            return obj.as<bool>() ? "true" : "false";
        case sol::type::nil:
            return "nil";
        case sol::type::userdata:
            return "<userdata>";
        case sol::type::table:
            return dump_table(obj);
        case sol::type::function:
            return "<function>";
        default:
            return "<unknown>";
    }
}


void WRAP_C_LUA_FUNCTION::LUA_LOG(sol::this_state ts, sol::variadic_args args) {
    sol::state_view lua(ts);
    std::string final;

    for (auto&& arg : args) {
        std::string str;
        switch (arg.get_type()) {
            case sol::type::string:
                str = arg.as<std::string>();
                break;
            case sol::type::number:
                str = std::to_string(arg.as<double>());
                break;
            case sol::type::boolean:
                str = arg.as<bool>() ? "true" : "false";
                break;
            case sol::type::nil:
                str = "nil";
                break;
            case sol::type::table:
                str = dump_table(arg.as<sol::table>());
                break;
            case sol::type::userdata:
                str = "<userdata>";
                break;
            case sol::type::function:
                str = "<function>";
                break;
            default:
                str = "<unknown>";
                break;
        }
        final += str + " ";
    }
    auto mylog = "[Lua]" + final;
    Commands::tcp_log(mylog);
}

sol::table jobject_to_luatable(sol::this_state ts, jobject obj){
    JavaEnv MyEnv;
    JNIEnv* env = MyEnv.get();
    sol::table tbl = LUA::lua->create_table();
    // 获取类对象
    jclass objClass = env->GetObjectClass(obj);

    // 获取 java.lang.Class
    jmethodID mid_getClass = env->GetMethodID(objClass, "getClass", "()Ljava/lang/Class;");
    jobject classObj = env->CallObjectMethod(obj, mid_getClass);

    // 获取字段数组
    jclass classClass = env->FindClass("java/lang/Class");
    jmethodID mid_getDeclaredFields = env->GetMethodID(classClass, "getDeclaredFields", "()[Ljava/lang/reflect/Field;");

    jmethodID isArrayMethod = env->GetMethodID(classClass, "isArray", "()Z");


    auto fieldArray = (jobjectArray) env->CallObjectMethod(classObj, mid_getDeclaredFields);

    jsize fieldCount = env->GetArrayLength(fieldArray);

    jclass fieldClass = env->FindClass("java/lang/reflect/Field");
    jmethodID mid_getName = env->GetMethodID(fieldClass, "getName", "()Ljava/lang/String;");
    jmethodID mid_getType = env->GetMethodID(fieldClass, "getType", "()Ljava/lang/Class;");
    jmethodID mid_get = env->GetMethodID(fieldClass, "get", "(Ljava/lang/Object;)Ljava/lang/Object;");
    jmethodID mid_setAccessible = env->GetMethodID(fieldClass, "setAccessible", "(Z)V");

    jclass booleanClass = env->FindClass("java/lang/Boolean");
    jmethodID mid_boolean_value = env->GetMethodID(booleanClass, "booleanValue", "()Z");

    jclass integerClass = env->FindClass("java/lang/Integer");
    jmethodID mid_int_value = env->GetMethodID(integerClass, "intValue", "()I");

    jclass floatClass = env->FindClass("java/lang/Float");
    jmethodID mid_float_value = env->GetMethodID(floatClass, "floatValue", "()F");

    jclass doubleClass = env->FindClass("java/lang/Double");
    jmethodID mid_double_value = env->GetMethodID(doubleClass, "doubleValue", "()D");

    jclass longClass = env->FindClass("java/lang/Long");
    jmethodID mid_long_value = env->GetMethodID(longClass, "longValue", "()J");

    jclass shortClass = env->FindClass("java/lang/Short");
    jmethodID mid_short_value = env->GetMethodID(shortClass, "shortValue", "()S");

    jclass charClass = env->FindClass("java/lang/Character");
    jmethodID  mid_char_value = env->GetMethodID(charClass, "charValue", "()C");

    jclass byteClass = env->FindClass("java/lang/Byte");
    jmethodID mid_byte_value = env->GetMethodID(byteClass, "byteValue", "()B");

    for (jsize i = 0; i < fieldCount; ++i) {
        jobject field = env->GetObjectArrayElement(fieldArray, i);

        // 让私有字段也可以访问
        env->CallVoidMethod(field, mid_setAccessible, JNI_TRUE);

        // 获取字段名
        auto nameStr = (jstring) env->CallObjectMethod(field, mid_getName);
        const char *name = env->GetStringUTFChars(nameStr, nullptr);

        // 获取字段值
        jobject valueObj = env->CallObjectMethod(field, mid_get, obj);

        if (valueObj != nullptr) {
            jclass valueClass = env->GetObjectClass(valueObj);

            if (env->IsInstanceOf(valueObj, integerClass)) {
                jint val = env->CallIntMethod(valueObj, mid_int_value);
                tbl[name] = val;
            }  else if (env->IsInstanceOf(valueObj, longClass)) {
                jlong val = env->CallLongMethod(valueObj, mid_long_value);
                tbl[name] = (int64_t)val;  // sol2 支持 int64_t
            }
            else if (env->IsInstanceOf(valueObj, shortClass)){
                jshort val = env->CallShortMethod(valueObj, mid_short_value);
                tbl[name] = val;
            }
            else if (env->IsInstanceOf(valueObj, charClass)){
                char val = env->CallCharMethod(valueObj, mid_char_value);
                tbl[name] = val;
            }
            else if (env->IsInstanceOf(valueObj, byteClass)){
                jbyte val = env->CallByteMethod(valueObj, mid_byte_value);
                tbl[name] = val;
            }
            else if (env->IsInstanceOf(valueObj, floatClass)) {
                jfloat val = env->CallFloatMethod(valueObj, mid_float_value);
                tbl[name] = val;
            } else if (env->IsInstanceOf(valueObj, doubleClass)) {
                jdouble val = env->CallDoubleMethod(valueObj, mid_double_value);
                tbl[name] = val;
            } else if (env->IsInstanceOf(valueObj, booleanClass)) {
                jboolean val = env->CallBooleanMethod(valueObj, mid_boolean_value);
                tbl[name] = (bool) val;
            } else if (env->IsInstanceOf(valueObj, env->FindClass("java/lang/String"))) {
                const char *str = env->GetStringUTFChars((jstring) valueObj, nullptr);
                tbl[name] = std::string(str);
                env->ReleaseStringUTFChars((jstring) valueObj, str);
            } else if (env->CallBooleanMethod(valueClass, isArrayMethod)){
                //这里也只是一层，并不会递归的
                tbl[name] = (int64_t)valueObj;//javaarray_to_luatable(ts, valueObj);
            }else {
                // 其他对象
                //不用管，这里不进行递归处理
                tbl[name] = (int64_t)valueObj;
            }
        } else {
            tbl[name] = sol::nil;
        }

        env->ReleaseStringUTFChars(nameStr, name);
        env->DeleteLocalRef(field);
    }

    return tbl;
}
sol::table WRAP_C_LUA_FUNCTION::jobject_to_luatable_trampoline(sol::this_state ts, sol::variadic_args args) {
    uint64_t objPtr = args[0].as<uint64_t>();
    jobject obj = (jobject)objPtr;
    return jobject_to_luatable(ts, obj);
}

sol::table javaarray_to_luatable(sol::this_state ts, jobject arrObj) {
    JavaEnv MyEnv;
    JNIEnv* env = MyEnv.get();
    sol::state_view lua(ts);
    sol::table tbl = lua.create_table();

    if (arrObj == nullptr) {
        return tbl;  // 空表
    }

    // 获取数组长度
    jsize arrLen = env->GetArrayLength((jarray)arrObj);

    // 判断数组元素类型，获取元素的Class
    jclass arrClass = env->GetObjectClass(arrObj);
    jclass classClass = env->FindClass("java/lang/Class");
    jmethodID mid_getComponentType = env->GetMethodID(classClass, "getComponentType", "()Ljava/lang/Class;");
    jobject compType = env->CallObjectMethod(arrClass, mid_getComponentType);

    jclass stringClass = env->FindClass("java/lang/String");

    // componentType 的类名判断
    jmethodID mid_getName = env->GetMethodID(classClass, "getName", "()Ljava/lang/String;");
    jstring compNameStr = (jstring)env->CallObjectMethod(compType, mid_getName);
    const char* compName = env->GetStringUTFChars(compNameStr, nullptr);

    for (jsize i = 0; i < arrLen; ++i) {
        // 依赖类型调用不同的 GetXXXArrayRegion 或 GetObjectArrayElement
        if (strcmp(compName, "int") == 0) {
            jint* elems = env->GetIntArrayElements((jintArray)arrObj, nullptr);
            tbl[i + 1] = elems[i];
            env->ReleaseIntArrayElements((jintArray)arrObj, elems, JNI_ABORT);
        } else if (strcmp(compName, "boolean") == 0) {
            jboolean* elems = env->GetBooleanArrayElements((jbooleanArray)arrObj, nullptr);
            tbl[i + 1] = elems[i] != 0;
            env->ReleaseBooleanArrayElements((jbooleanArray)arrObj, elems, JNI_ABORT);
        } else if (strcmp(compName, "float") == 0) {
            jfloat* elems = env->GetFloatArrayElements((jfloatArray)arrObj, nullptr);
            tbl[i + 1] = elems[i];
            env->ReleaseFloatArrayElements((jfloatArray)arrObj, elems, JNI_ABORT);
        } else if (strcmp(compName, "double") == 0) {
            jdouble* elems = env->GetDoubleArrayElements((jdoubleArray)arrObj, nullptr);
            tbl[i + 1] = elems[i];
            env->ReleaseDoubleArrayElements((jdoubleArray)arrObj, elems, JNI_ABORT);
        } else if (strcmp(compName, "long") == 0) {
            jlong* elems = env->GetLongArrayElements((jlongArray)arrObj, nullptr);
            tbl[i + 1] = (int64_t)elems[i];  // 转成 lua number，注意精度问题
            env->ReleaseLongArrayElements((jlongArray)arrObj, elems, JNI_ABORT);
        }
        else if (strcmp(compName, "short") == 0) {
            jshort* elems = env->GetShortArrayElements((jshortArray)arrObj, nullptr);
            tbl[i + 1] = elems[i];
            env->ReleaseShortArrayElements((jshortArray)arrObj, elems, JNI_ABORT);
        }
        else if (strcmp(compName, "char") == 0) {
            jchar* elems = env->GetCharArrayElements((jcharArray)arrObj, nullptr);
            tbl[i + 1] = elems[i];
            env->ReleaseCharArrayElements((jcharArray)arrObj, elems, JNI_ABORT);
        }
        else if (strcmp(compName, "byte") == 0) {
            jbyte* elems = env->GetByteArrayElements((jbyteArray)arrObj, nullptr);
            tbl[i + 1] = elems[i];
            env->ReleaseByteArrayElements((jbyteArray)arrObj, elems, JNI_ABORT);
        }else {
            // 对象数组
            jobject elemObj = env->GetObjectArrayElement((jobjectArray)arrObj, i);
            if (elemObj == nullptr) {
                tbl[i + 1] = sol::nil;
            } else if (env->IsInstanceOf(elemObj, stringClass)) {
                const char* str = env->GetStringUTFChars((jstring)elemObj, nullptr);
                tbl[i + 1] = std::string(str);
                env->ReleaseStringUTFChars((jstring)elemObj, str);
            } else {
                // 不进行递归处理 仅保留地址 用户需要就让用户自己处理
                //用户处理完了，直接对这个地址应用就行了
                //递归需要考虑太多情况，耗费性能高容易出错
                tbl[i + 1] = (int64_t)elemObj;
            }
            //不要delete delete了就废了 后面用户没法操作了
            //env->DeleteLocalRef(elemObj);
        }
    }

    env->ReleaseStringUTFChars(compNameStr, compName);
    env->DeleteLocalRef(compNameStr);
    env->DeleteLocalRef(compType);
    env->DeleteLocalRef(arrClass);

    return tbl;
}

sol::table WRAP_C_LUA_FUNCTION::javaarray_to_luatable_trampoline(sol::this_state ts, sol::variadic_args args) {
    uint64_t objPtr = args[0].as<uint64_t>();
    jobject obj = (jobject)objPtr;
    return javaarray_to_luatable(ts, obj);
}

void apply_soltable_to_existing_jobject(sol::this_state ts, sol::table tbl, jobject obj){
    JavaEnv MyEnv;
    JNIEnv* env = MyEnv.get();
    // 获取类对象
    jclass objClass = env->GetObjectClass(obj);

    // 获取 java.lang.Class
    jmethodID mid_getClass = env->GetMethodID(objClass, "getClass", "()Ljava/lang/Class;");
    jobject classObj = env->CallObjectMethod(obj, mid_getClass);

    // 获取字段数组
    jclass classClass = env->FindClass("java/lang/Class");
    jmethodID mid_getDeclaredFields = env->GetMethodID(classClass, "getDeclaredFields", "()[Ljava/lang/reflect/Field;");

    jmethodID isArrayMethod = env->GetMethodID(classClass, "isArray", "()Z");


    auto fieldArray = (jobjectArray) env->CallObjectMethod(classObj, mid_getDeclaredFields);

    jsize fieldCount = env->GetArrayLength(fieldArray);

    jclass fieldClass = env->FindClass("java/lang/reflect/Field");
    jmethodID mid_getName = env->GetMethodID(fieldClass, "getName", "()Ljava/lang/String;");
    jmethodID mid_getType = env->GetMethodID(fieldClass, "getType", "()Ljava/lang/Class;");
    jmethodID mid_get = env->GetMethodID(fieldClass, "get", "(Ljava/lang/Object;)Ljava/lang/Object;");
    jmethodID mid_setAccessible = env->GetMethodID(fieldClass, "setAccessible", "(Z)V");

    jclass booleanClass = env->FindClass("java/lang/Boolean");
    jmethodID mid_boolean_value = env->GetMethodID(booleanClass, "booleanValue", "()Z");

    jclass integerClass = env->FindClass("java/lang/Integer");
    jmethodID mid_int_value = env->GetMethodID(integerClass, "intValue", "()I");

    jclass floatClass = env->FindClass("java/lang/Float");
    jmethodID mid_float_value = env->GetMethodID(floatClass, "floatValue", "()F");

    jclass doubleClass = env->FindClass("java/lang/Double");
    jmethodID mid_double_value = env->GetMethodID(doubleClass, "doubleValue", "()D");

    jclass longClass = env->FindClass("java/lang/Long");
    jmethodID mid_long_value = env->GetMethodID(longClass, "longValue", "()J");

    jclass shortClass = env->FindClass("java/lang/Short");
    jmethodID mid_short_value = env->GetMethodID(shortClass, "shortValue", "()S");

    jclass charClass = env->FindClass("java/lang/Character");
    jmethodID  mid_char_value = env->GetMethodID(charClass, "charValue", "()C");

    jclass byteClass = env->FindClass("java/lang/Byte");
    jmethodID mid_byte_value = env->GetMethodID(byteClass, "byteValue", "()B");



    for (jsize i = 0; i < fieldCount; ++i) {
        jobject field = env->GetObjectArrayElement(fieldArray, i);

        // 让私有字段也可以访问
        env->CallVoidMethod(field, mid_setAccessible, JNI_TRUE);

        // 获取字段名
        auto nameStr = (jstring) env->CallObjectMethod(field, mid_getName);
        const char *name = env->GetStringUTFChars(nameStr, nullptr);

        // 获取字段值
        jobject valueObj = env->CallObjectMethod(field, mid_get, obj);

        if (valueObj != nullptr) {
            jclass valueClass = env->GetObjectClass(valueObj);

            if (env->IsInstanceOf(valueObj, integerClass)) {
                if (tbl[name].is<int>()) {
                    jfieldID fid = env->GetFieldID(objClass, name, "I");
                    env->SetIntField(obj, fid,tbl[name].get<int>());
                }
            }  else if (env->IsInstanceOf(valueObj, longClass)) {
                if (tbl[name].is<int64_t>()) {
                    jfieldID fid = env->GetFieldID(objClass, name, "J");
                    env->SetLongField(obj, fid,tbl[name].get<int64_t>());
                }
            }
            else if (env->IsInstanceOf(valueObj, shortClass)){
                if (tbl[name].is<short>()) {
                    jfieldID fid = env->GetFieldID(objClass, name, "S");
                    env->SetShortField(obj, fid,tbl[name].get<short>());
                }
            }
            else if (env->IsInstanceOf(valueObj, charClass)){
                if (tbl[name].is<char>()) {
                    jfieldID fid = env->GetFieldID(objClass, name, "C");
                    env->SetCharField(obj, fid,tbl[name].get<char>());
                }
            }
            else if (env->IsInstanceOf(valueObj, byteClass)){
                if (tbl[name].is<int8_t>()) {
                    jfieldID fid = env->GetFieldID(objClass, name, "B");
                    env->SetByteField(obj, fid,tbl[name].get<int8_t>());
                }
            }
            else if (env->IsInstanceOf(valueObj, floatClass)) {
                if (tbl[name].is<float>()) {
                    jfieldID fid = env->GetFieldID(objClass, name, "F");
                    env->SetFloatField(obj, fid,tbl[name].get<float>());
                }
            } else if (env->IsInstanceOf(valueObj, doubleClass)) {
                if (tbl[name].is<double>()) {
                    jfieldID fid = env->GetFieldID(objClass, name, "D");
                    env->SetDoubleField(obj, fid,tbl[name].get<double>());
                }
            } else if (env->IsInstanceOf(valueObj, booleanClass)) {
                if (tbl[name].is<bool>()) {
                    jfieldID fid = env->GetFieldID(objClass, name, "Z");
                    env->SetBooleanField(obj, fid,tbl[name].get<bool>());
                }
            } else if (env->IsInstanceOf(valueObj, env->FindClass("java/lang/String"))) {
                if (tbl[name].is<std::string>()) {
                    jfieldID fid = env->GetFieldID(objClass, name, "Ljava/lang/String;");
                    std::string cppStr = tbl[name].get<std::string>();
                    jstring jstr = env->NewStringUTF(cppStr.c_str());
                    env->SetObjectField(obj, fid, jstr);
                    env->DeleteLocalRef(jstr);  // 推荐清理局部引用
                }
            }
            else if (env->CallBooleanMethod(valueClass, isArrayMethod)){
                //类套数组，不递归处理
                //tbl[name] = javaarray_to_luatable(ts, valueObj);
            }else {
                // 其他对象，不递归处理
                //tbl[name] = jobject_to_luatable(ts, valueObj);
            }
        }
        else {
            tbl[name] = sol::nil;
        }

        env->ReleaseStringUTFChars(nameStr, name);
        env->DeleteLocalRef(field);
    }

    return;
}

void WRAP_C_LUA_FUNCTION::apply_soltable_to_existing_jobject_trampoline(sol::this_state ts, sol::variadic_args args) {
    auto table = args[0].as<sol::table>();
    uint64_t obj = args[1].as<uint64_t>();
    apply_soltable_to_existing_jobject(ts, table, (jobject) obj);
}

void apply_soltable_to_existing_javaarray(sol::this_state ts,sol::table tbl, jobject arrObj) {
    JavaEnv MyEnv;
    JNIEnv *env = MyEnv.get();
    sol::state_view lua(ts);
    if (arrObj == nullptr) {
        return;  // 空表
    }
    jsize arrLen = env->GetArrayLength((jarray) arrObj);

    // 判断数组元素类型，获取元素的Class
    jclass arrClass = env->GetObjectClass(arrObj);
    jclass classClass = env->FindClass("java/lang/Class");
    jmethodID mid_getComponentType = env->GetMethodID(classClass, "getComponentType",
                                                      "()Ljava/lang/Class;");
    jobject compType = env->CallObjectMethod(arrClass, mid_getComponentType);

    jclass stringClass = env->FindClass("java/lang/String");

    // componentType 的类名判断
    jmethodID mid_getName = env->GetMethodID(classClass, "getName", "()Ljava/lang/String;");
    jstring compNameStr = (jstring) env->CallObjectMethod(compType, mid_getName);
    const char *compName = env->GetStringUTFChars(compNameStr, nullptr);

    // 依赖类型调用不同的 GetXXXArrayRegion 或 GetObjectArrayElement
    if (strcmp(compName, "int") == 0) {
        auto buffer = new int[tbl.size()];
        for (int index = 0; index < tbl.size(); index++){
            if (tbl[index + 1].is<int>())
                buffer[index] = tbl[index + 1].get<int>();
        }
        env->SetIntArrayRegion(((jintArray) arrObj),0, tbl.size(),buffer);
        delete[] buffer;
    } else if (strcmp(compName, "boolean") == 0) {
        auto* buffer = new jboolean[tbl.size()];
        for (int index = 0; index < tbl.size(); index++){
            if (tbl[index + 1].is<jboolean>())
                buffer[index] = tbl[index + 1].get<jboolean>();
        }
        env->SetBooleanArrayRegion(((jbooleanArray) arrObj),0, tbl.size(),buffer);
        delete[] buffer;

    } else if (strcmp(compName, "float") == 0) {
        auto* buffer = new float[tbl.size()];
        for (int index = 0; index < tbl.size(); index++){
            if (tbl[index + 1].is<float>())
                buffer[index] = tbl[index + 1].get<float>();
        }
        env->SetFloatArrayRegion(((jfloatArray) arrObj),0, tbl.size(),buffer);
        delete[] buffer;

    } else if (strcmp(compName, "double") == 0) {
        auto* buffer = new double[tbl.size()];
        for (int index = 0; index < tbl.size(); index++){
            if (tbl[index + 1].is<double>())
                buffer[index] = tbl[index + 1].get<double>();
        }
        env->SetDoubleArrayRegion(((jdoubleArray) arrObj),0, tbl.size(),buffer);
        delete[] buffer;
    } else if (strcmp(compName, "long") == 0) {
        auto* buffer = new int64_t[tbl.size()];
        for (int index = 0; index < tbl.size(); index++){
            if (tbl[index + 1].is<int64_t>())
                buffer[index] = tbl[index + 1].get<int64_t>();
        }
        env->SetLongArrayRegion(((jlongArray) arrObj),0, tbl.size(),buffer);
        delete[] buffer;
    } else if (strcmp(compName, "short") == 0) {
        auto buffer = new short[tbl.size()];
        for (int index = 0; index < tbl.size(); index++){
            if (tbl[index + 1].is<short>())
                buffer[index] = tbl[index + 1].get<short>();
        }
        env->SetShortArrayRegion(((jshortArray) arrObj),0, tbl.size(),buffer);
        delete[] buffer;
    } else if (strcmp(compName, "char") == 0) {
        auto buffer = new jchar[tbl.size()];
        for (int index = 0; index < tbl.size(); index++){
            if (tbl[index + 1].is<jchar>())
                buffer[index] = tbl[index + 1].get<jchar>();
        }
        env->SetCharArrayRegion(((jcharArray) arrObj),0, tbl.size(),buffer);
        delete[] buffer;
    } else if (strcmp(compName, "byte") == 0) {
        auto* buffer = new jbyte [tbl.size()];
        for (int index = 0; index < tbl.size(); index++){
            if (tbl[index + 1].is<jbyte>())
                buffer[index] = tbl[index + 1].get<jbyte>();
        }
        env->SetByteArrayRegion(((jbyteArray) arrObj),0, tbl.size(),buffer);
        delete[] buffer;
    } else {
        // 对象数组
        jobject elemObj = env->GetObjectArrayElement((jobjectArray)arrObj, 0);
        if (elemObj == nullptr) {
            //不知道，理论不会发生
        } else if (env->IsInstanceOf(elemObj, stringClass)) {
            //是String[]数组
            //String不可变，不能修改，只能替换
            for (int index = 0; index < tbl.size(); index++){
                if (tbl[index + 1].is<std::string>()){
                    jstring newJstr = env->NewStringUTF(tbl[index + 1].get<std::string>().c_str());
                    env->SetObjectArrayElement((jobjectArray)arrObj, index, newJstr);
                    env->DeleteLocalRef(newJstr);
                }
            }
        } else {
            // 调用结构体应用函数 注意，这里也是只应用了一层，并不会递归
            for (int index = 0; index < tbl.size(); index++){
                if (tbl[index + 1].is<sol::table>()){
                    jobject objElem =  env->GetObjectArrayElement((jobjectArray)arrObj, index);
                    apply_soltable_to_existing_jobject(ts,tbl[index + 1].get<sol::table>(),objElem);
                    env->SetObjectArrayElement((jobjectArray)arrObj, index, objElem);
                }
            }
        }
    }


    env->ReleaseStringUTFChars(compNameStr, compName);
    env->DeleteLocalRef(compNameStr);
    env->DeleteLocalRef(compType);
    env->DeleteLocalRef(arrClass);

    return;
}


void WRAP_C_LUA_FUNCTION::apply_soltable_to_existing_javaarray_trampoline(sol::this_state ts, sol::variadic_args args) {
    auto table = args[0].as<sol::table>();
    uint64_t obj = args[1].as<uint64_t>();
    apply_soltable_to_existing_javaarray(ts, table, (jobject) obj);
}

sol::table javalist_to_luatable(sol::this_state ts, jobject listObj) {
    JavaEnv MyEnv;
    JNIEnv* env = MyEnv.get();
    sol::state_view lua(ts);
    sol::table tbl = lua.create_table();

    if (listObj == nullptr) {
        return tbl;  // 空表
    }

    // 获取 List 接口和其方法
    jclass listClass = env->GetObjectClass(listObj);
    jmethodID mid_size = env->GetMethodID(listClass, "size", "()I");
    jmethodID mid_get = env->GetMethodID(listClass, "get", "(I)Ljava/lang/Object;");

    jint size = env->CallIntMethod(listObj, mid_size);
    jclass stringClass = env->FindClass("java/lang/String");

    jclass booleanClass = env->FindClass("java/lang/Boolean");
    jmethodID mid_boolean_value = env->GetMethodID(booleanClass, "booleanValue", "()Z");

    jclass integerClass = env->FindClass("java/lang/Integer");
    jmethodID mid_int_value = env->GetMethodID(integerClass, "intValue", "()I");

    jclass floatClass = env->FindClass("java/lang/Float");
    jmethodID mid_float_value = env->GetMethodID(floatClass, "floatValue", "()F");

    jclass doubleClass = env->FindClass("java/lang/Double");
    jmethodID mid_double_value = env->GetMethodID(doubleClass, "doubleValue", "()D");

    jclass longClass = env->FindClass("java/lang/Long");
    jmethodID mid_long_value = env->GetMethodID(longClass, "longValue", "()J");

    jclass shortClass = env->FindClass("java/lang/Short");
    jmethodID mid_short_value = env->GetMethodID(shortClass, "shortValue", "()S");

    jclass charClass = env->FindClass("java/lang/Character");
    jmethodID  mid_char_value = env->GetMethodID(charClass, "charValue", "()C");

    jclass byteClass = env->FindClass("java/lang/Byte");
    jmethodID mid_byte_value = env->GetMethodID(byteClass, "byteValue", "()B");

    jclass classClass = env->FindClass("java/lang/Class");
    jmethodID isArrayMethod = env->GetMethodID(classClass, "isArray", "()Z");

    for (jint i = 0; i < size; ++i) {
        jobject valueObj = env->CallObjectMethod(listObj, mid_get, i);

        if (valueObj != nullptr) {
            jclass valueClass = env->GetObjectClass(valueObj);

            if (env->IsInstanceOf(valueObj, integerClass)) {
                jint val = env->CallIntMethod(valueObj, mid_int_value);
                tbl[i+1] = val;
            }  else if (env->IsInstanceOf(valueObj, longClass)) {
                jlong val = env->CallLongMethod(valueObj, mid_long_value);
                tbl[i+1] = (int64_t)val;  // sol2 支持 int64_t
            }
            else if (env->IsInstanceOf(valueObj, shortClass)){
                jshort val = env->CallShortMethod(valueObj, mid_short_value);
                tbl[i+1] = val;
            }
            else if (env->IsInstanceOf(valueObj, charClass)){
                char val = env->CallCharMethod(valueObj, mid_char_value);
                tbl[i+1] = val;
            }
            else if (env->IsInstanceOf(valueObj, byteClass)){
                jbyte val = env->CallByteMethod(valueObj, mid_byte_value);
                tbl[i+1] = val;
            }
            else if (env->IsInstanceOf(valueObj, floatClass)) {
                jfloat val = env->CallFloatMethod(valueObj, mid_float_value);
                tbl[i+1] = val;
            } else if (env->IsInstanceOf(valueObj, doubleClass)) {
                jdouble val = env->CallDoubleMethod(valueObj, mid_double_value);
                tbl[i+1] = val;
            } else if (env->IsInstanceOf(valueObj, booleanClass)) {
                jboolean val = env->CallBooleanMethod(valueObj, mid_boolean_value);
                tbl[i+1] = (bool) val;
            } else if (env->IsInstanceOf(valueObj, env->FindClass("java/lang/String"))) {
                const char *str = env->GetStringUTFChars((jstring) valueObj, nullptr);
                tbl[i+1] = std::string(str);
                env->ReleaseStringUTFChars((jstring) valueObj, str);
            } else if (env->CallBooleanMethod(valueClass, isArrayMethod)){
                //这里也只是一层，并不会递归的
                tbl[i+1] = (int64_t)valueObj;//javaarray_to_luatable(ts, valueObj);
            }else {
                // 其他对象
                //不用管，这里不进行递归处理
                tbl[i+1] = (int64_t)valueObj;
            }
        } else {
            tbl[i+1] = sol::nil;
        }
    }

    env->DeleteLocalRef(listClass);
    return tbl;
}

sol::table WRAP_C_LUA_FUNCTION::javalist_to_luatable_trampoline(sol::this_state ts, sol::variadic_args args) {
    uint64_t objPtr = args[0].as<uint64_t>();
    jobject obj = (jobject)objPtr;
    return javalist_to_luatable(ts, obj);
}


void sync_soltable_to_javalist(sol::this_state ts, sol::table tbl, jobject listObj) {
    JavaEnv MyEnv;
    JNIEnv* env = MyEnv.get();
    sol::state_view lua(ts);

    if (listObj == nullptr) return;

    jclass listClass = env->GetObjectClass(listObj);
    jmethodID mid_size = env->GetMethodID(listClass, "size", "()I");
    jmethodID mid_get = env->GetMethodID(listClass, "get", "(I)Ljava/lang/Object;");
    jmethodID mid_set = env->GetMethodID(listClass, "set", "(ILjava/lang/Object;)Ljava/lang/Object;");
    jmethodID mid_add = env->GetMethodID(listClass, "add", "(Ljava/lang/Object;)Z");
    jmethodID mid_remove = env->GetMethodID(listClass, "remove", "(I)Ljava/lang/Object;");

    // 获取 Java 基本类型封装类和构造函数
#define PREPARE(cls, name, sig) \
        jclass cls##Class = env->FindClass("java/lang/" #cls); \
        jmethodID cls##_init = env->GetMethodID(cls##Class, "<init>", sig)

    PREPARE(Integer, int, "(I)V");
    PREPARE(Long, long, "(J)V");
    PREPARE(Float, float, "(F)V");
    PREPARE(Double, double, "(D)V");
    PREPARE(Boolean, boolean, "(Z)V");
    PREPARE(Short, short, "(S)V");
    PREPARE(Character, char, "(C)V");
    PREPARE(Byte, byte, "(B)V");

    jclass stringClass = env->FindClass("java/lang/String");

    jint java_size = env->CallIntMethod(listObj, mid_size);

    // 获取 Lua 表长度（最大索引）
    int lua_len = 0;
    for (auto& kv : tbl) {
        if (kv.first.is<int>()) {
            int key = kv.first.as<int>();
            if (key > lua_len) lua_len = key;
        }
    }

    auto build_java_object = [&](jobject refType, sol::object val) -> jobject {
        if (refType == nullptr || val.is<sol::nil_t>()) return nullptr;
        jclass cls = env->GetObjectClass(refType);
        if (env->IsInstanceOf(refType, IntegerClass)) {
            return env->NewObject(IntegerClass, Integer_init, (jint)val.as<int>());
        } else if (env->IsInstanceOf(refType, LongClass)) {
            return env->NewObject(LongClass, Long_init, (jlong)val.as<int64_t>());
        } else if (env->IsInstanceOf(refType, FloatClass)) {
            return env->NewObject(FloatClass, Float_init, (jfloat)val.as<float>());
        } else if (env->IsInstanceOf(refType, DoubleClass)) {
            return env->NewObject(DoubleClass, Double_init, (jdouble)val.as<double>());
        } else if (env->IsInstanceOf(refType, BooleanClass)) {
            return env->NewObject(BooleanClass, Boolean_init, (jboolean)val.as<bool>());
        } else if (env->IsInstanceOf(refType, ShortClass)) {
            return env->NewObject(ShortClass, Short_init, (jshort)val.as<int>());
        } else if (env->IsInstanceOf(refType, CharacterClass)) {
            return env->NewObject(CharacterClass, Character_init, (jchar)val.as<int>());
        } else if (env->IsInstanceOf(refType, ByteClass)) {
            return env->NewObject(ByteClass, Byte_init, (jbyte)val.as<int>());
        } else if (env->IsInstanceOf(refType, stringClass)) {
            std::string str = val.as<std::string>();
            return env->NewStringUTF(str.c_str());
        } else {
            return nullptr;  // 忽略复杂类型
        }
    };

    // 替换或追加
    for (int i = 0; i < lua_len; ++i) {
        sol::object luaVal = tbl[i + 1];
        if (luaVal.is<sol::nil_t>()) continue;

        if (i < java_size) {
            jobject oldObj = env->CallObjectMethod(listObj, mid_get, i);
            jobject newObj = build_java_object(oldObj, luaVal);
            if (newObj != nullptr)
                env->CallObjectMethod(listObj, mid_set, i, newObj);
            env->DeleteLocalRef(newObj);
            env->DeleteLocalRef(oldObj);
        } else {
            // 获取最后一个已存在的元素类型作为参考
            jobject lastRef = java_size > 0 ? env->CallObjectMethod(listObj, mid_get, java_size - 1) : nullptr;
            jobject newObj = build_java_object(lastRef, luaVal);
            if (newObj != nullptr)
                env->CallBooleanMethod(listObj, mid_add, newObj);
            env->DeleteLocalRef(newObj);
            if (lastRef) env->DeleteLocalRef(lastRef);
        }
    }

    // 删除多余
    for (jint i = java_size - 1; i >= lua_len; --i) {
        env->CallObjectMethod(listObj, mid_remove, i);
    }

    env->DeleteLocalRef(listClass);
}

void WRAP_C_LUA_FUNCTION::apply_soltable_to_existing_javalist_trampoline(sol::this_state ts, sol::variadic_args args) {
    auto table = args[0].as<sol::table>();
    uint64_t obj = args[1].as<uint64_t>();
    sync_soltable_to_javalist(ts, table, (jobject) obj);
}

std::string WRAP_C_LUA_FUNCTION::getJavaStringContent(sol::object solobj){
    std::string result = "";
    JavaEnv myenv;
    auto env = myenv.get();
    if (solobj.is<int64_t>()){
        jobject jobj = (jobject)solobj.as<int64_t>();
        if (env->IsInstanceOf(jobj, env->FindClass("java/lang/String"))) {
            const char *str = env->GetStringUTFChars((jstring) jobj, nullptr);
            result = std::string(str);
            env->ReleaseStringUTFChars((jstring) jobj, str);
        }
    }
    return result;
}

//String不可变，要修改只能用Create
int64_t WRAP_C_LUA_FUNCTION::createJavaString(sol::object solobj, sol::object solstr) {
    JavaEnv myenv;
    auto env = myenv.get();

    if (!solstr.is<std::string>()) {
        return solobj.as<int64_t>();
    }

    std::string content = solstr.as<std::string>();
    jstring jstr = env->NewStringUTF(content.c_str());
    //api33这里需要额外的decode，还没写。
    return (int64_t)jstr;  // 以整数形式返回给 Lua
}
