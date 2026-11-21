// harness.cpp
// 复现通达信公式层 CON_月多_DLL.txt，直接调用 TDXMonthBull.cpp 里的 4 个 DLL 函数，
// 逐日输出 月多（以及用于诊断的中间信号），与 Java 版本 / ext_data 对比。
//
// 用法:
//   harness <series.csv> <out.csv> [--key=date|flags] [--week=formula|iso] [--dump=idx]
//
// --key=date   : 新接口。第二个参数传日期键 (YEAR-1900)*10000+MONTH*100+DAY，
//                周期边界由 DLL 依据 ISO 周自行判定（推荐）
// --key=flags  : 旧接口。传 0/1 的 WEEK_START / MONTH_START 标志
// --week=formula: 仅 flags 模式有效，严格按原公式 WEEKDAY<REF(WEEKDAY,1) 判定新周
// --week=iso    : 仅 flags 模式有效，按 ISO 周判定新周（用于隔离「只有周边界有问题」）

#define TDX_MONTHBULL_TEST_NO_EXPORTS 1
#include "../TDXMonthBull.cpp"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace {

struct Bar {
    int y, m, d;
    double open, high, low, close;
    double preBreak;
    double expected;
};

int HDaysInMonth(int y, int m) {
    static const int dom[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (m == 2 && ((y % 4 == 0 && y % 100 != 0) || y % 400 == 0)) return 29;
    return dom[m - 1];
}

long long HDaysSinceEpoch(int y, int m, int d) {
    long long days = 0;
    for (int yy = 1970; yy < y; ++yy) days += ((yy % 4 == 0 && yy % 100 != 0) || yy % 400 == 0) ? 366 : 365;
    for (int mm = 1; mm < m; ++mm) days += HDaysInMonth(y, mm);
    days += (d - 1);
    return days;
}

int HIsoWeekday(int y, int m, int d) { // 1=Mon .. 7=Sun
    long long e = HDaysSinceEpoch(y, m, d);
    int w = static_cast<int>(((e % 7) + 7 + 3) % 7); // 1970-01-01=Thu(4)
    return w + 1;
}

void HIsoWeek(int y, int m, int d, int* outYear, int* outWeek) {
    int wd = HIsoWeekday(y, m, d);
    long long e = HDaysSinceEpoch(y, m, d);
    long long thursday = e + (4 - wd);
    long long t = thursday;
    int ty = 1970;
    for (;;) {
        long long yearDays = ((ty % 4 == 0 && ty % 100 != 0) || ty % 400 == 0) ? 366 : 365;
        if (t >= yearDays) { t -= yearDays; ++ty; }
        else break;
    }
    long long jan1 = HDaysSinceEpoch(ty, 1, 1);
    *outYear = ty;
    *outWeek = static_cast<int>((thursday - jan1) / 7) + 1;
}

std::vector<Bar> LoadCsv(const char* path, bool* ok) {
    std::vector<Bar> bars;
    std::ifstream in(path);
    if (!in) { *ok = false; return bars; }
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#' || line.rfind("date,", 0) == 0) continue;
        std::stringstream ss(line);
        std::string f[7];
        int n = 0;
        while (n < 7 && std::getline(ss, f[n], ',')) ++n;
        if (n < 7) continue;
        Bar b{};
        int y, m, d;
        if (std::sscanf(f[0].c_str(), "%d-%d-%d", &y, &m, &d) != 3) continue;
        b.y = y; b.m = m; b.d = d;
        b.open = std::atof(f[1].c_str());
        b.high = std::atof(f[2].c_str());
        b.low = std::atof(f[3].c_str());
        b.close = std::atof(f[4].c_str());
        b.preBreak = std::atof(f[5].c_str());
        b.expected = std::atof(f[6].c_str());
        bars.push_back(b);
    }
    *ok = true;
    return bars;
}

// 通达信 MA：不足 N 根为 DRAWNULL（NaN）
std::vector<double> TdxMa(const std::vector<double>& s, int n) {
    std::vector<double> r(s.size(), std::numeric_limits<double>::quiet_NaN());
    double sum = 0.0;
    for (std::size_t i = 0; i < s.size(); ++i) {
        sum += s[i];
        if (static_cast<int>(i) >= n) sum -= s[i - n];
        if (static_cast<int>(i) + 1 >= n) r[i] = sum / n;
    }
    return r;
}

// 通达信 HHV：窗口不足时取已有数据的最大值（不像 Java 版返回 NaN）
std::vector<double> TdxHhv(const std::vector<double>& s, int n) {
    std::vector<double> r(s.size(), std::numeric_limits<double>::quiet_NaN());
    for (std::size_t i = 0; i < s.size(); ++i) {
        std::size_t start = (static_cast<int>(i) + 1 >= n) ? i + 1 - n : 0;
        double mx = -std::numeric_limits<double>::infinity();
        for (std::size_t j = start; j <= i; ++j) {
            if (std::isfinite(s[j]) && s[j] > mx) mx = s[j];
        }
        r[i] = mx;
    }
    return r;
}

inline double ToFloat32(double v) {
    if (!std::isfinite(v)) return v;
    return static_cast<double>(static_cast<float>(v));
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: %s series.csv out.csv [--key=date|flags] [--week=formula|iso] [--dump=idx]\n", argv[0]);
        return 2;
    }
    std::string keyMode = "date";
    std::string weekMode = "formula";
    std::string hhvMode = "java"; // java: i<9 时为 NaN（与 Java 参考实现一致）；tdx: 取已有窗口最大值
    std::string day0Mode = "dll";
    int dumpIdx = -1;
    for (int i = 3; i < argc; ++i) {
        std::string a = argv[i];
        if (a.rfind("--key=", 0) == 0) keyMode = a.substr(6);
        if (a.rfind("--week=", 0) == 0) weekMode = a.substr(7);
        if (a.rfind("--dump=", 0) == 0) dumpIdx = std::atoi(a.c_str() + 7);
        if (a.rfind("--hhv=", 0) == 0) hhvMode = a.substr(6);
        if (a.rfind("--day0=", 0) == 0) day0Mode = a.substr(7);
    }

    bool ok = false;
    std::vector<Bar> bars = LoadCsv(argv[1], &ok);
    if (!ok) { std::fprintf(stderr, "cannot read %s\n", argv[1]); return 2; }
    const int n = static_cast<int>(bars.size());
    if (n == 0) return 0;

    std::vector<float> C(n), H(n), L(n);
    std::vector<float> dateKey(n, 0.0f), weekFlag(n, 0.0f), monthFlag(n, 0.0f), mode0(n, 0.0f), mode1(n, 1.0f), mode2(n, 2.0f), mode3(n, 3.0f);
    for (int i = 0; i < n; ++i) {
        C[i] = static_cast<float>(bars[i].close);
        H[i] = static_cast<float>(bars[i].high);
        L[i] = static_cast<float>(bars[i].low);
        dateKey[i] = static_cast<float>((bars[i].y - 1900) * 10000 + bars[i].m * 100 + bars[i].d);
    }
    for (int i = 0; i < n; ++i) {
        if (i == 0) { weekFlag[i] = 1.0f; monthFlag[i] = 1.0f; continue; }
        const Bar& p = bars[i - 1];
        monthFlag[i] = (bars[i].m != p.m) ? 1.0f : 0.0f;
        if (weekMode == "iso") {
            int wy1, wk1, wy0, wk0;
            HIsoWeek(bars[i].y, bars[i].m, bars[i].d, &wy1, &wk1);
            HIsoWeek(p.y, p.m, p.d, &wy0, &wk0);
            weekFlag[i] = (wy1 != wy0 || wk1 != wk0) ? 1.0f : 0.0f;
        } else {
            weekFlag[i] = (HIsoWeekday(bars[i].y, bars[i].m, bars[i].d) < HIsoWeekday(p.y, p.m, p.d)) ? 1.0f : 0.0f;
        }
    }

    const float* monthKey = (keyMode == "date") ? dateKey.data() : monthFlag.data();
    const float* weekKey = (keyMode == "date") ? dateKey.data() : weekFlag.data();

    // ---- 调用 DLL ----
    std::vector<float> ratio(n, 0), nearCross(n, 0), monthCross(n, 0);
    std::vector<float> weekCross(n, 0), sarW(n, 0), difD(n, 0), deaD(n, 0), macdD(n, 0);
    MonthMacdInfo(n, ratio.data(), C.data(), const_cast<float*>(monthKey), mode0.data());
    MonthMacdInfo(n, nearCross.data(), C.data(), const_cast<float*>(monthKey), mode1.data());
    MonthMacdInfo(n, monthCross.data(), C.data(), const_cast<float*>(monthKey), mode2.data());
    WeekMacdCross(n, weekCross.data(), C.data(), const_cast<float*>(weekKey), nullptr);
    WeekSar(n, sarW.data(), H.data(), L.data(), const_cast<float*>(weekKey));
    DailyMacd(n, difD.data(), C.data(), mode0.data(), nullptr);
    DailyMacd(n, deaD.data(), C.data(), mode1.data(), nullptr);
    DailyMacd(n, macdD.data(), C.data(), mode2.data(), nullptr);

    // ---- 公式层（按通达信 float32 语义：DLL 边界即 float）----
    std::vector<double> close(n), macdDd(n);
    for (int i = 0; i < n; ++i) {
        close[i] = static_cast<double>(C[i]);
        macdDd[i] = ToFloat32(static_cast<double>(macdD[i]));
    }
    std::vector<double> hhvMacd10 = TdxHhv(macdDd, 10);
    if (hhvMode == "java") {
        for (int i = 0; i < n && i < 9; ++i) hhvMacd10[i] = std::numeric_limits<double>::quiet_NaN();
    }
    std::vector<float> day0(n, 0.0f);
    DailyMacd(n, day0.data(), C.data(), mode3.data(), nullptr);

    std::vector<double> MA20 = TdxMa(close, 20), MA50 = TdxMa(close, 50), MA100 = TdxMa(close, 100), MA250 = TdxMa(close, 250);
    for (double& x : MA20) if (std::isfinite(x)) x = ToFloat32(x);
    for (double& x : MA50) if (std::isfinite(x)) x = ToFloat32(x);
    for (double& x : MA100) if (std::isfinite(x)) x = ToFloat32(x);
    for (double& x : MA250) if (std::isfinite(x)) x = ToFloat32(x);
    auto ifNull0 = [](std::vector<double>& v) { for (double& x : v) if (!std::isfinite(x)) x = 0.0; };
    ifNull0(MA50); ifNull0(MA100); ifNull0(MA250);

    int mismatches = 0, firstMismatch = -1;
    std::ofstream out(argv[2]);
    out << "idx,date,calc,pb,expected,monthCross,weekCross,dayAbove0,sarOk,macdMonthBull,macdWeekBull,ratio,nearCross,sarW\n";
    for (int i = 0; i < n; ++i) {
        const double dif = difD[i], dea = deaD[i], macd = macdD[i];
        const bool monthCrossB = monthCross[i] > 0.5f;
        const bool weekCrossB = weekCross[i] > 0.5f;
        const bool dayAbove0Formula = (dif >= 0.0 && dea >= 0.0) ||
                                      (macd >= 0.0 && std::isfinite(hhvMacd10[i]) && macd == hhvMacd10[i]);
        const bool dayAbove0 = (day0Mode == "dll") ? (day0[i] > 0.5f) : dayAbove0Formula;
        const bool macdMonthBull = monthCrossB && weekCrossB && dayAbove0;
        const bool ma250Zero = (MA250[i] == 0.0);
        const double refMA100 = (i >= 1) ? MA100[i - 1] : std::numeric_limits<double>::quiet_NaN();
        const double refMA250 = (i >= 1) ? MA250[i - 1] : std::numeric_limits<double>::quiet_NaN();
        const bool condA = ma250Zero && close[i] > MA20[i] && MA20[i] > MA50[i];
        const bool condB = close[i] > MA100[i] && close[i] > MA250[i] &&
                           (MA50[i] > MA100[i] || MA50[i] > MA250[i]) &&
                           (MA100[i] > refMA100 || MA250[i] > refMA250);
        const bool macdWeekBull = weekCrossB && dayAbove0 && (monthCrossB || condA || condB);
        const bool sarOk = (close[i] >= static_cast<double>(sarW[i]));
        const bool preBreak = bars[i].preBreak > 0.5;
        const bool yueDuo = macdMonthBull && (sarOk || preBreak);
        const int calc = yueDuo ? 1 : 0;
        const int exp = bars[i].expected > 0.5 ? 1 : 0;
        if (calc != exp) {
            ++mismatches;
            if (firstMismatch < 0) firstMismatch = i;
        }
        char date[16];
        std::snprintf(date, sizeof(date), "%04d-%02d-%02d", bars[i].y, bars[i].m, bars[i].d);
        out << i << ',' << date << ',' << calc << ',' << (preBreak ? 1 : 0) << ',' << exp << ','
            << (monthCrossB ? 1 : 0) << ',' << (weekCrossB ? 1 : 0) << ',' << (dayAbove0 ? 1 : 0) << ','
            << (sarOk ? 1 : 0) << ',' << (macdMonthBull ? 1 : 0) << ',' << (macdWeekBull ? 1 : 0) << ','
            << ratio[i] << ',' << nearCross[i] << ',' << sarW[i] << '\n';
    }
    out.close();

    if (dumpIdx >= 0 && dumpIdx < n) {
        std::vector<double> wH, wL, wC;
        PeriodSplitter dumpSplitter(weekKey, n);
        for (int i = 0; i <= dumpIdx; ++i) {
            const double h = std::isfinite(H[i]) ? static_cast<double>(H[i]) : 0.0;
            const double l = std::isfinite(L[i]) ? static_cast<double>(L[i]) : 0.0;
            if (wH.empty() || dumpSplitter.NewWeek(i)) {
                wH.push_back(h); wL.push_back(l); wC.push_back(static_cast<double>(C[i]));
            } else {
                wH.back() = std::max(wH.back(), h);
                wL.back() = std::min(wL.back(), l);
                wC.back() = static_cast<double>(C[i]);
            }
        }
        const std::vector<double> sar = CalcTdxSar(wH, wL, 2, 20);
        const std::size_t nw = wH.size();
        std::printf("DUMP idx=%d date=%04d-%02d-%02d weeks=%zu\n", dumpIdx, bars[dumpIdx].y, bars[dumpIdx].m, bars[dumpIdx].d, nw);
        for (std::size_t k = (nw > 4 ? nw - 4 : 0); k < nw; ++k) {
            std::printf("  week[%zu] H=%.17g L=%.17g C=%.17g SAR=%.17g\n", k, wH[k], wL[k], wC[k], sar[k]);
        }
        std::printf("  close_f=%.17g sarLast=%.17g diff=%.17g\n",
                    static_cast<double>(C[dumpIdx]), sar[nw - 1],
                    static_cast<double>(C[dumpIdx]) - sar[nw - 1]);
    }

    std::printf("series=%s n=%d key=%s week=%s mismatches=%d firstMismatch=%d\n",
                argv[1], n, keyMode.c_str(), weekMode.c_str(), mismatches, firstMismatch);
    return 0;
}
