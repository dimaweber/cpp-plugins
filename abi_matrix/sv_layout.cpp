#include <cstdio>
#include <string_view>
int main() {
    std::string_view sv{"abcd", 4};
    unsigned long w0{}, w1{};
    __builtin_memcpy(&w0, &sv, 8);
    __builtin_memcpy(&w1, reinterpret_cast<const char*>(&sv) + 8, 8);
    std::printf("sizeof=%zu  word0=0x%016lx  word1=0x%016lx   (data=%p size=%zu)\n",
                sizeof(sv), w0, w1, static_cast<const void*>(sv.data()), sv.size());
    return 0;
}
