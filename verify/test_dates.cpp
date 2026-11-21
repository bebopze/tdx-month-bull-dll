// test_dates.cpp
// 单独验证 DLL 内的「日期键解析 + ISO 周」实现，防止周边界算错（这是 v2 修复的核心）。
// 用法：g++ -O2 -std=c++17 -I. -o test_dates test_dates.cpp && ./test_dates
// 校验脚本：python3 check_dates.py

#define TDX_MONTHBULL_TEST_NO_EXPORTS 1
#include "../TDXMonthBull.cpp"

#include <cstdio>

int main() {
    const int dates[][3] = {
        {1970, 1, 1},   {1990, 1, 1},   {1990, 11, 19}, {1999, 12, 31}, {2000, 1, 1},
        {2000, 2, 29},  {2015, 1, 1},   {2016, 1, 1},   {2017, 1, 1},   {2017, 10, 9},
        {2018, 12, 31}, {2019, 1, 1},   {2019, 12, 30}, {2020, 1, 1},   {2020, 2, 29},
        {2020, 12, 31}, {2021, 1, 1},   {2021, 1, 4},   {2024, 9, 30},  {2024, 10, 8},
        {2024, 11, 19}, {2024, 12, 30}, {2024, 12, 31}, {2025, 1, 1},   {2025, 1, 27},
        {2025, 2, 5},   {2025, 10, 9},  {2026, 1, 1},   {2026, 11, 19}, {2030, 12, 31},
        {2035, 1, 1},   {2040, 2, 29},  {2099, 12, 31},
    };
    for (const auto& dt : dates) {
        int wy = 0;
        int wk = 0;
        IsoWeekKey(dt[0], dt[1], dt[2], &wy, &wk);
        const float key = static_cast<float>((dt[0] - 1900) * 10000 + dt[1] * 100 + dt[2]);
        const Ymd back = DecodeDateKey(key);
        std::printf("%04d-%02d-%02d %d %d | key=%.0f -> %04d-%02d-%02d %s\n",
                    dt[0], dt[1], dt[2], wy, wk, static_cast<double>(key),
                    back.y, back.m, back.d, back.valid ? "ok" : "INVALID");
    }
    return 0;
}
