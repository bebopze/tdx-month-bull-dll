// TDXMonthBull.cpp
// 日线因果聚合版：只用截至每个交易日已知的 C/H/L 动态聚合周/月 K。
//
// 计算语义基准（逐行对齐）：
//   bebopze/tdx-quant  →  common/tdxfun/TdxFun.java        （EMA / MACD / TDX_SAR / HHV / rd）
//                      →  common/tdxfun/MonthlyBullSignal2.java（月多 的动态周/月聚合与判定）
//
// 周期边界（本版核心修复）：
//   旧版依赖公式里的 WEEK_START := WEEKDAY < REF(WEEKDAY,1)，遇到「整周休市」（国庆/春节长假）
//   时无法识别新周，会把两个自然周合并成一根周K，导致周线 MACD / SAR 序列整体偏移，
//   进而使「月多」在长假之后长期错位（实测 249 个真实序列 / 474692 个交易日中 2383 天不一致）。
//   本版改为由 DLL 依据真实交易日历（ISO 周：周一为周首、跨年按 week-based-year）自行切分，
//   与 Java 侧 WeekFields.ISO 完全一致；同时保留旧版 0/1 标志入参的兼容模式。
//
// 入参约定（第 2/3 个参数）：传入日期键 DATE_KEY，形如 YYMMDD（年 - 1900），
// 例如 2024-11-19 → 1241119；通达信公式里用 (YEAR-1900)*10000 + MONTH*100 + DAY 生成。
//   1) 若整个入参序列都是 0/1 → 视为旧版「周/月起始标志」模式（兼容老公式）；
//   2) 否则 → 视为日期键模式（推荐，全部周期边界由 DLL 自行判定）。

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

// 通达信官方 PluginTCalcFunc.h 约定：回调函数为默认调用约定（cdecl），
// 结构体 1 字节对齐，注册函数名 RegisterTdxFunc。此处保持一致，勿加 WINAPI。

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
constexpr int kDailyMacdId = 4;

// ===========================================================================
// 日期工具：纯整数公历运算（Howard Hinnant days_from_civil），无时区/无依赖
// ===========================================================================
struct Ymd {
    int y = 0;
    int m = 0;
    int d = 0;
    bool valid = false;
};

inline bool IsLeap(int y) { return (y % 4 == 0 && y % 100 != 0) || (y % 400 == 0); }

inline int DaysInMonth(int y, int m) {
    static const int kDays[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (m == 2) return IsLeap(y) ? 29 : 28;
    return kDays[m - 1];
}

// 1970-01-01 为第 0 天
inline long long DaysFromCivil(int y, int m, int d) {
    const long long yy = static_cast<long long>(y) - (m <= 2 ? 1 : 0);
    const long long era = (yy >= 0 ? yy : yy - 399) / 400;
    const unsigned yoe = static_cast<unsigned>(yy - era * 400);
    const unsigned doy = static_cast<unsigned>((153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1);
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + static_cast<long long>(doe) - 719468;
}

// 1=Mon ... 7=Sun
inline int IsoWeekday(long long daysSinceEpoch) {
    const long long r = ((daysSinceEpoch % 7) + 7 + 3) % 7; // 1970-01-01 为星期四
    return static_cast<int>(r) + 1;
}

// ISO 周（周一起算 + week-based-year）：与 Java WeekFields.ISO 等价
inline void IsoWeekKey(int y, int m, int d, int* outWeekYear, int* outWeek) {
    const long long e = DaysFromCivil(y, m, d);
    const int wd = IsoWeekday(e);
    const long long thursday = e + (4 - wd); // 本周四决定所属年度
    // 求该周四所在公历年
    long long t = thursday;
    int ty = 1970;
    for (;;) {
        const long long yearDays = IsLeap(ty) ? 366 : 365;
        if (t >= yearDays) {
            t -= yearDays;
            ++ty;
        } else if (t < 0) {
            --ty;
            t += IsLeap(ty) ? 366 : 365;
        } else {
            break;
        }
    }
    const long long jan1 = DaysFromCivil(ty, 1, 1);
    *outWeekYear = ty;
    *outWeek = static_cast<int>((thursday - jan1) / 7) + 1;
}

// 解析日期键：支持 YYMMDD(年-1900，通达信 DATE 惯例) / YYMMDD(2000 起) / YYYYMMDD
inline Ymd DecodeDateKey(float raw) {
    Ymd out;
    if (!std::isfinite(raw)) return out;
    const long long n = static_cast<long long>(std::floor(static_cast<double>(raw) + 0.5));
    if (n <= 0) return out;
    long long ymd = n;
    if (n >= 19000000LL) {          // YYYYMMDD（float32 精度有限，仅容错）
        out.y = static_cast<int>(n / 10000);
    } else if (n >= 500000LL) {     // YYMMDD，年 + 1900（通达信 DATE 惯例：1241119 → 2024-11-19）
        out.y = 1900 + static_cast<int>(n / 10000);
    } else {                        // YYMMDD，2000 起（如 241119）
        out.y = 2000 + static_cast<int>(n / 10000);
    }
    out.m = static_cast<int>((ymd / 100) % 100);
    out.d = static_cast<int>(ymd % 100);
    if (out.m < 1 || out.m > 12 || out.d < 1 || out.d > DaysInMonth(out.y, out.m)) {
        return Ymd{};
    }
    out.valid = true;
    return out;
}

// 周期切分器：同一序列既要判断「新周」也要判断「新月」
class PeriodSplitter {
public:
    PeriodSplitter(const float* keySeries, int dataLen) : series_(keySeries) {
        for (int i = 0; i < dataLen; ++i) {
            if (std::isfinite(series_[i]) && series_[i] > 1.5f) {
                dateMode_ = true;
                break;
            }
        }
    }

    bool DateMode() const { return dateMode_; }

    bool NewWeek(int i) const {
        if (i <= 0) return true;
        if (!dateMode_) return Flag(series_[i]);
        const Ymd cur = DecodeDateKey(series_[i]);
        if (!cur.valid) return false; // 当日日期缺失：并入上一根，避免凭空造出新周
        const Ymd prev = DecodeDateKey(series_[i - 1]);
        if (!prev.valid) return true;
        int wy1 = 0, wk1 = 0, wy0 = 0, wk0 = 0;
        IsoWeekKey(cur.y, cur.m, cur.d, &wy1, &wk1);
        IsoWeekKey(prev.y, prev.m, prev.d, &wy0, &wk0);
        return wy1 != wy0 || wk1 != wk0;
    }

    bool NewMonth(int i) const {
        if (i <= 0) return true;
        if (!dateMode_) return Flag(series_[i]);
        const Ymd cur = DecodeDateKey(series_[i]);
        if (!cur.valid) return false;
        const Ymd prev = DecodeDateKey(series_[i - 1]);
        if (!prev.valid) return true;
        return cur.y != prev.y || cur.m != prev.m;
    }

private:
    static bool Flag(float v) { return std::isfinite(v) && v > 0.5f; }

    const float* series_;
    bool dateMode_ = false;
};

// ===========================================================================
// TdxFun 对齐工具
// ===========================================================================
struct MacdResult {
    std::vector<double> dif;
    std::vector<double> dea;
    std::vector<double> macd;
};

// 与 TdxFun.rd / rdArray 对齐：Math.round(N * 10^d) / 10^d。
// Java Math.round(NaN) 返回 0；正负无穷及越界值饱和到 long 边界。
inline double JavaRound(double value, int digits = kDigits) {
    const double factor = std::pow(10.0, digits);
    if (std::isnan(value)) return 0.0;
    const double scaled = value * factor;
    const double maxLong = static_cast<double>(std::numeric_limits<long long>::max());
    const double minLong = static_cast<double>(std::numeric_limits<long long>::min());
    if (scaled >= maxLong) return static_cast<double>(std::numeric_limits<long long>::max()) / factor;
    if (scaled <= minLong) return static_cast<double>(std::numeric_limits<long long>::min()) / factor;
    return std::floor(scaled + 0.5) / factor;
}

// 与 TdxFun.EMA(double[], N) 一致：从第一个 finite 样本初始化；
// 后续仅在当前输入与上一 EMA 均为 finite 时递推。
std::vector<double> Ema(const std::vector<double>& input, int period) {
    std::vector<double> out(input.size(), std::numeric_limits<double>::quiet_NaN());
    if (input.empty() || period <= 0) return out;
    const double alpha = 2.0 / (static_cast<double>(period) + 1.0);
    std::size_t start = input.size();
    for (std::size_t i = 0; i < input.size(); ++i) {
        if (std::isfinite(input[i])) {
            start = i;
            out[i] = input[i];
            break;
        }
    }
    if (start == input.size()) return out;
    for (std::size_t i = start + 1; i < input.size(); ++i) {
        const double value = input[i];
        if (std::isfinite(value) && std::isfinite(out[i - 1])) {
            out[i] = alpha * value + (1.0 - alpha) * out[i - 1];
        } else {
            out[i] = std::numeric_limits<double>::quiet_NaN();
        }
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

inline double MonthlyRatio(const MacdResult& macd) {
    if (macd.dif.empty() || macd.dea.empty()) return 0.0;
    const std::size_t last = macd.dif.size() - 1;
    const double a = std::fabs(macd.dif[last]);
    const double b = std::fabs(macd.dea[last]);
    const double mx = std::max(a, b);
    if (mx == 0.0) return 0.0;
    return std::min(a, b) / mx;
}

// BARSLASTCOUNT(DIF_M >= REF(DIF_M,1)) 与 (DIF_M > REF(DIF_M,1))
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
    // Java 侧阈值：(int)(1.2 * 20) == 24
    return (blc1 >= 24 && ratio >= 0.9) || (blc2 >= 1 && ratio >= 0.95);
}

inline bool MonthlyMacdCross(const MacdResult& macd) {
    const std::size_t n = macd.macd.size();
    if (n == 0) return false;
    const double current = macd.macd[n - 1];
    if (current >= 0.0) return true;
    // Java: HHV(MACD_M,9) 在月数 < 9 时为 NaN → 等值比较恒为 false
    if (n < 9 || !MonthlyNearCross(macd)) return false;
    double hhv = -std::numeric_limits<double>::infinity();
    for (std::size_t i = n - 9; i < n; ++i) hhv = std::max(hhv, macd.macd[i]);
    return current == hhv;
}

// DLL 函数 ID=1: MONTH_MACD_INFO(C, DATE_KEY, MODE)
// MODE 0=月线 DIF/DEA 绝对值比率，1=月线接近金叉，2=月线金叉判定，
// 3=当月动态 MACD，4=当月动态 DIF，5=当月动态 DEA。
void MonthMacdInfo(int dataLen, float* out, float* inC, float* inKey, float* inMode) {
    if (out == nullptr || dataLen <= 0) return;
    std::fill(out, out + dataLen, 0.0f);
    if (inC == nullptr || inKey == nullptr || inMode == nullptr) return;
    try {
        const PeriodSplitter period(inKey, dataLen);
        std::vector<double> monthClose;
        monthClose.reserve(static_cast<std::size_t>(dataLen) / 20 + 2);
        for (int i = 0; i < dataLen; ++i) {
            const double close = std::isfinite(inC[i]) ? static_cast<double>(inC[i]) : 0.0;
            if (monthClose.empty() || period.NewMonth(i)) monthClose.push_back(close);
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

// DLL 函数 ID=2: WEEK_MACD_CROSS(C, DATE_KEY, UNUSED)
// MACD.MACD#WEEK >= 0
void WeekMacdCross(int dataLen, float* out, float* inC, float* inKey, float* /*inUnused*/) {
    if (out == nullptr || dataLen <= 0) return;
    std::fill(out, out + dataLen, 0.0f);
    if (inC == nullptr || inKey == nullptr) return;
    try {
        const PeriodSplitter period(inKey, dataLen);
        std::vector<double> weekClose;
        weekClose.reserve(static_cast<std::size_t>(dataLen) / 5 + 2);
        for (int i = 0; i < dataLen; ++i) {
            const double close = std::isfinite(inC[i]) ? static_cast<double>(inC[i]) : 0.0;
            if (weekClose.empty() || period.NewWeek(i)) weekClose.push_back(close);
            else weekClose.back() = close;
            const MacdResult macd = CalcMacd(weekClose);
            if (!macd.macd.empty()) out[i] = macd.macd.back() >= 0.0 ? 1.0f : 0.0f;
        }
    } catch (...) {
        std::fill(out, out + dataLen, 0.0f);
    }
}

// 逐行对应 TdxFun.TDX_SAR(high, low, iAFStep, iAFLimit)。
// 默认调用语义固定为 TDX_SAR(high, low) -> TDX_SAR(high, low, 2, 20)。
std::vector<double> CalcTdxSar(const std::vector<double>& high,
                              const std::vector<double>& low,
                              int iAFStep = 2,
                              int iAFLimit = 20) {
    const std::size_t n = std::min(high.size(), low.size());
    std::vector<double> sar(n, 0.0);
    if (n == 0) return sar;
    const double afStep = iAFStep / 100.0;
    const double afLimit = iAFLimit / 100.0;
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

// DLL 函数 ID=3: WEEK_SAR(H, L, DATE_KEY)
void WeekSar(int dataLen, float* out, float* inH, float* inL, float* inKey) {
    if (out == nullptr || dataLen <= 0) return;
    std::fill(out, out + dataLen, 0.0f);
    if (inH == nullptr || inL == nullptr || inKey == nullptr) return;
    try {
        const PeriodSplitter period(inKey, dataLen);
        std::vector<double> weekHigh;
        std::vector<double> weekLow;
        weekHigh.reserve(static_cast<std::size_t>(dataLen) / 5 + 2);
        weekLow.reserve(static_cast<std::size_t>(dataLen) / 5 + 2);
        for (int i = 0; i < dataLen; ++i) {
            const double high = std::isfinite(inH[i]) ? static_cast<double>(inH[i]) : 0.0;
            const double low = std::isfinite(inL[i]) ? static_cast<double>(inL[i]) : 0.0;
            if (weekHigh.empty() || period.NewWeek(i)) {
                weekHigh.push_back(high);
                weekLow.push_back(low);
            } else {
                weekHigh.back() = std::max(weekHigh.back(), high);
                weekLow.back() = std::min(weekLow.back(), low);
            }
            const std::vector<double> sar = CalcTdxSar(weekHigh, weekLow, 2, 20);
            if (!sar.empty()) out[i] = static_cast<float>(sar.back());
        }
    } catch (...) {
        std::fill(out, out + dataLen, 0.0f);
    }
}

// DLL 函数 ID=4: DAILY_MACD(C, MODE, UNUSED)
// MODE 0=DIF，1=DEA，2=MACD（均按 TdxFun.rdArray(...,5) 保留 5 位小数），
// MODE 3=MACD_日上0轴 判定结果（0/1）：
//       (DIF>=0 AND DEA>=0) OR (MACD>=0 AND MACD=HHV(MACD,10))
//       HHV 采用与 Java 参考实现一致的语义（不足 10 根时为 NaN → 该分支不成立），
//       并且在 DLL 内部用 double 比较，避免平台层 HHV 差异与 float 相等比较带来的边界抖动。
void DailyMacd(int dataLen, float* out, float* inC, float* inMode, float* /*inUnused*/) {
    if (out == nullptr || dataLen <= 0) return;
    std::fill(out, out + dataLen, 0.0f);
    if (inC == nullptr || inMode == nullptr) return;
    try {
        std::vector<double> close(static_cast<std::size_t>(dataLen));
        for (int i = 0; i < dataLen; ++i) {
            close[static_cast<std::size_t>(i)] = static_cast<double>(inC[i]);
        }
        const MacdResult macd = CalcMacd(close);

        bool needDayAbove0 = false;
        for (int i = 0; i < dataLen; ++i) {
            const double rawMode = std::isfinite(inMode[i]) ? static_cast<double>(inMode[i]) : 0.0;
            if (static_cast<int>(std::floor(rawMode + 0.5)) == 3) {
                needDayAbove0 = true;
                break;
            }
        }
        std::vector<unsigned char> dayAbove0;
        if (needDayAbove0) {
            dayAbove0.assign(static_cast<std::size_t>(dataLen), 0);
            for (int i = 0; i < dataLen; ++i) {
                const bool aboveZeroAxis = (macd.dif[i] >= 0.0 && macd.dea[i] >= 0.0);
                bool atWindowHigh = false;
                if (i >= 9) {
                    double hhv = -std::numeric_limits<double>::infinity();
                    for (int j = i - 9; j <= i; ++j) hhv = std::max(hhv, macd.macd[j]);
                    atWindowHigh = (macd.macd[i] >= 0.0 && macd.macd[i] == hhv);
                }
                dayAbove0[i] = (aboveZeroAxis || atWindowHigh) ? 1 : 0;
            }
        }

        for (int i = 0; i < dataLen; ++i) {
            const double rawMode = std::isfinite(inMode[i]) ? static_cast<double>(inMode[i]) : 0.0;
            const int mode = static_cast<int>(std::floor(rawMode + 0.5));
            const std::size_t index = static_cast<std::size_t>(i);
            switch (mode) {
                case 0: out[i] = static_cast<float>(macd.dif[index]); break;
                case 1: out[i] = static_cast<float>(macd.dea[index]); break;
                case 2: out[i] = static_cast<float>(macd.macd[index]); break;
                case 3: out[i] = needDayAbove0 ? static_cast<float>(dayAbove0[index]) : 0.0f; break;
                default: out[i] = 0.0f; break;
            }
        }
    } catch (...) {
        std::fill(out, out + dataLen, 0.0f);
    }
}
} // namespace

// ---------------------------------------------------------------------------
// 通达信 DLL 导出表（单元测试 harness 通过定义 TDX_MONTHBULL_TEST_NO_EXPORTS 屏蔽）
// ---------------------------------------------------------------------------
#ifndef TDX_MONTHBULL_TEST_NO_EXPORTS
PluginTCalcFuncInfo g_CalcFuncSets[] = {
    {kMonthlyMacdInfoId, &MonthMacdInfo},
    {kWeeklyMacdCrossId, &WeekMacdCross},
    {kWeeklySarId, &WeekSar},
    {kDailyMacdId, &DailyMacd},
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
#endif // TDX_MONTHBULL_TEST_NO_EXPORTS
