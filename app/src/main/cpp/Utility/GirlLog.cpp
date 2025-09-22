//
// Created by weibo on 2025/6/21.
//
#include "GirlLog.h"
//hahahaha

namespace Logger{
    void hex_dump_log(const void *addr, size_t size, const char *tag) {
        const uint8_t *data = reinterpret_cast<const uint8_t *>(addr);
        const size_t width = 16;

        for (size_t i = 0; i < size; i += width) {
            std::ostringstream oss;
            oss << tag << " +" << std::setw(4) << std::setfill('0') << std::hex << i << ": ";

            // 十六进制部分
            for (size_t j = 0; j < width; ++j) {
                if (i + j < size)
                    oss << std::setw(2) << std::setfill('0') << std::hex << (int) data[i + j]
                        << " ";
                else
                    oss << "   ";
            }

            oss << "| ";

            // ASCII 部分
            for (size_t j = 0; j < width; ++j) {
                if (i + j < size) {
                    char c = static_cast<char>(data[i + j]);
                    oss << (isprint(c) ? c : '.');
                } else {
                    oss << " ";
                }
            }

            LOGI("%s", oss.str().c_str());
        }
    }
}