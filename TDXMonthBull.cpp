// TDXMonthBull.cpp
// 日线因果聚合版：只用截至每个交易日已知的 C/H/L 动态聚合周/月 K。
// 计算语义以用户提供的 MonthlyBullSignal2.java 为基准。

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <limits>
#include <vector>

#ifdef _WIN32
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
#else
using BOOL = int;
constexpr BOOL TRUE = 1;
constexpr BOOL FALSE = 0;
#endif

#pragma pack(push, 1)
typedef void (*pPluginFUNC)(int, float*, float*, float*, float*);
struct PluginTCalcFuncInfo {
    unsigned short nFuncMark;
    pPluginFUNC pCallFunc;
};
#pragma pack(pop)

namespace {
constexpr int kDigits = 5;
constexpr int kMonthlyMacdInfoId = 1;
constexpr int kWeeklyMacdCrossId = 2;
constexpr int kWeeklySarId = 3;

struct MacdResult {
    std::vector<double> dif;
    std::vector<double> dea;
    std::vector<double> macd;
};

inline double JavaRound(double value, int digits = kDigits) {
    if (!std::isfinite(value)) return value;
    const double factor = std::pow(10.0, digits);
    return std::floor(value * factor + 0.5) / factor;
}

std::vector<double> Ema(const std::vector<double>& input, int period) {
    std::vector<double> out(input.size(), 0.0);
    if (input.empty() || period <= 0) return out;
    const double alpha = 2.0 / (static_cast<double>(period) + 1.0);
    out[0] = input[0];
    for (std::size_t i = 1; i < input.size(); ++i) {
        out[i] = alpha * input[i] + (1.0 - alpha) * out[i - 1];
    }
    return out;
}

MacdResult CalcMacd(const std::vector<double>& close) {
    MacdResult out;
    const std::size_t n = close.size();
    out.dif.assign(n, 0.0);
    out.dea.assign(n, 0.0);
    out.macd.assign(n, 0.0);
    if (n == 0) return out;
    const std::vector<double> ema12 = Ema(close, 12);
    const std::vector<double> ema26 = Ema(close, 26);
    std::vector<double> difRaw(n, 0.0);
    for (std::size_t i = 0; i < n; ++i) difRaw[i] = ema12[i] - ema26[i];
    const std::vector<double> deaRaw = Ema(difRaw, 9);
    for (std::size_t i = 0; i < n; ++i) {
        out.dif[i] = JavaRound(difRaw[i]);
        out.dea[i] = JavaRound(deaRaw[i]);
        out.macd[i] = JavaRound((difRaw[i] - deaRaw[i]) * 2.0);
    }
    return out;
}

inline bool FlagAt(const float* series, int i) {
    return series != nullptr && std::isfinite(series[i]) && series[i] > 0.5f;
}

inline double MonthlyRatio(const MacdResult& macd) {
    if (macd.dif.empty() || macd.dea.empty()) return 0.0;
    const std::size_t last = macd.dif.size() - 1;
    const double a = std::fabs(macd.dif[last]);
    const double b = std::fabs(macd.dea[last]);
    const double mx = std::max(a, b);
    if (mx == 0.0) return 0.0;
    return std::min(a, b) / mx;
}

inline bool MonthlyNearCross(const MacdResult& macd) {
    const std::size_t n = macd.dif.size();
    if (n < 2) return false;
    int blc1 = 0;
    int blc2 = 0;
    for (std::size_t i = 1; i < n; ++i) {
        if (macd.dif[i] >= macd.dif[i - 1]) ++blc1;
        else blc1 = 0;
        if (macd.dif[i] > macd.dif[i - 1]) ++blc2;
        else blc2 = 0;
    }
    const double ratio = MonthlyRatio(macd);
    return (blc1 >= 24 && ratio >= 0.9) || (blc2 >= 1 && ratio >= 0.95);
}

inline bool MonthlyMacdCross(const MacdResult& macd) {
    const std::size_t n = macd.macd.size();
    if (n == 0) return false;
    const double current = macd.macd[n - 1];
    if (current >= 0.0) return true;
    if (n < 9 || !MonthlyNearCross(macd)) return false;
    double hhv = -std::numeric_limits<double>::infinity();
    for (std::size_t i = n - 9; i < n; ++i) hhv = std::max(hhv, macd.macd[i]);
    return current == hhv;
}

// DLL 函数 ID=1: MONTH_MACD_INFO(C, MONTH_START, MODE)
// MODE 0=月线 DIF/DEA 绝对值比率，1=月线接近金叉，2=月线金叉判定，
// 3=当月动态 MACD，4=当月动态 DIF，5=当月动态 DEA。
void MonthMacdInfo(int dataLen, float* out, float* inC, float* inMonthStart, float* inMode) {
    if (out == nullptr || dataLen <= 0) return;
    std::fill(out, out + dataLen, 0.0f);
    if (inC == nullptr || inMonthStart == nullptr || inMode == nullptr) return;
    try {
        std::vector<double> monthClose;
        monthClose.reserve(static_cast<std::size_t>(dataLen) / 20 + 2);
        for (int i = 0; i < dataLen; ++i) {
            const double close = std::isfinite(inC[i]) ? static_cast<double>(inC[i]) : 0.0;
            if (monthClose.empty() || FlagAt(inMonthStart, i)) monthClose.push_back(close);
            else monthClose.back() = close;
            const MacdResult macd = CalcMacd(monthClose);
            if (macd.macd.empty()) continue;
            const int mode = static_cast<int>(std::floor(static_cast<double>(inMode[i]) + 0.5));
            const std::size_t last = macd.macd.size() - 1;
            double value = 0.0;
            switch (mode) {
                case 0: value = MonthlyRatio(macd); break;
                case 1: value = MonthlyNearCross(macd) ? 1.0 : 0.0; break;
                case 2: value = MonthlyMacdCross(macd) ? 1.0 : 0.0; break;
                case 3: value = macd.macd[last]; break;
                case 4: value = macd.dif[last]; break;
                case 5: value = macd.dea[last]; break;
                default: value = 0.0; break;
            }
            out[i] = static_cast<float>(value);
        }
    } catch (...) {
        std::fill(out, out + dataLen, 0.0f);
    }
}

// DLL 函数 ID=2: WEEK_MACD_CROSS(C, WEEK_START, UNUSED)
void WeekMacdCross(int dataLen, float* out, float* inC, float* inWeekStart, float* /*inUnused*/) {
    if (out == nullptr || dataLen <= 0) return;
    std::fill(out, out + dataLen, 0.0f);
    if (inC == nullptr || inWeekStart == nullptr) return;
    try {
        std::vector<double> weekClose;
        weekClose.reserve(static_cast<std::size_t>(dataLen) / 5 + 2);
        for (int i = 0; i < dataLen; ++i) {
            const double close = std::isfinite(inC[i]) ? static_cast<double>(inC[i]) : 0.0;
            if (weekClose.empty() || FlagAt(inWeekStart, i)) weekClose.push_back(close);
            else weekClose.back() = close;
            const MacdResult macd = CalcMacd(weekClose);
            if (!macd.macd.empty()) out[i] = macd.macd.back() >= 0.0 ? 1.0f : 0.0f;
        }
    } catch (...) {
        std::fill(out, out + dataLen, 0.0f);
    }
}

std::vector<double> CalcTdxSar(const std::vector<double>& high, const std::vector<double>& low) {
    const std::size_t n = std::min(high.size(), low.size());
    std::vector<double> sar(n, 0.0);
    if (n == 0) return sar;
    constexpr double afStep = 0.02;
    constexpr double afLimit = 0.20;
    bool bull = true;
    double af = afStep;
    double ep = high[0];
    sar[0] = low[0];
    for (std::size_t i = 1; i < n; ++i) {
        if (bull) {
            if (high[i] > ep) {
                ep = high[i];
                af = std::min(af + afStep, afLimit);
            }
        } else {
            if (low[i] < ep) {
                ep = low[i];
                af = std::min(af + afStep, afLimit);
            }
        }
        sar[i] = sar[i - 1] + af * (ep - sar[i - 1]);
        if (bull) {
            sar[i] = std::min(std::min(sar[i], low[i]), low[i - 1]);
            sar[i] = std::max(sar[i], sar[i - 1]);
        } else {
            sar[i] = std::max(std::max(sar[i], high[i]), high[i - 1]);
            sar[i] = std::min(sar[i], sar[i - 1]);
        }
        if (bull) {
            if (low[i] < sar[i]) {
                bull = false;
                const double tmpSar = ep;
                ep = low[i];
                af = afStep;
                if (high[i - 1] == tmpSar) sar[i] = tmpSar;
                else sar[i] = tmpSar + af * (ep - tmpSar);
            }
        } else {
            if (high[i] > sar[i]) {
                bull = true;
                ep = high[i];
                af = afStep;
                sar[i] = std::min(low[i], low[i - 1]);
            }
        }
    }
    return sar;
}

// DLL 函数 ID=3: WEEK_SAR(H, L, WEEK_START)
void WeekSar(int dataLen, float* out, float* inH, float* inL, float* inWeekStart) {
    if (out == nullptr || dataLen <= 0) return;
    std::fill(out, out + dataLen, 0.0f);
    if (inH == nullptr || inL == nullptr || inWeekStart == nullptr) return;
    try {
        std::vector<double> weekHigh;
        std::vector<double> weekLow;
        weekHigh.reserve(static_cast<std::size_t>(dataLen) / 5 + 2);
        weekLow.reserve(static_cast<std::size_t>(dataLen) / 5 + 2);
        for (int i = 0; i < dataLen; ++i) {
            const double high = std::isfinite(inH[i]) ? static_cast<double>(inH[i]) : 0.0;
            const double low = std::isfinite(inL[i]) ? static_cast<double>(inL[i]) : 0.0;
            if (weekHigh.empty() || FlagAt(inWeekStart, i)) {
                weekHigh.push_back(high);
                weekLow.push_back(low);
            } else {
                weekHigh.back() = std::max(weekHigh.back(), high);
                weekLow.back() = std::min(weekLow.back(), low);
            }
            const std::vector<double> sar = CalcTdxSar(weekHigh, weekLow);
            if (!sar.empty()) out[i] = static_cast<float>(sar.back());
        }
    } catch (...) {
        std::fill(out, out + dataLen, 0.0f);
    }
}
} // namespace

PluginTCalcFuncInfo g_CalcFuncSets[] = {
    {kMonthlyMacdInfoId, reinterpret_cast<pPluginFUNC>(&MonthMacdInfo)},
    {kWeeklyMacdCrossId, reinterpret_cast<pPluginFUNC>(&WeekMacdCross)},
    {kWeeklySarId, reinterpret_cast<pPluginFUNC>(&WeekSar)},
    {0, nullptr}
};

extern "C" BOOL RegisterTdxFunc(PluginTCalcFuncInfo** pFun) {
    if (pFun == nullptr) return FALSE;
    if (*pFun == nullptr) {
        *pFun = g_CalcFuncSets;
        return TRUE;
    }
    return FALSE;
}
