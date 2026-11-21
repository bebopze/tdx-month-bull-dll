package ref;

import java.io.BufferedReader;
import java.io.FileReader;
import java.time.LocalDate;
import java.time.temporal.WeekFields;
import java.util.ArrayList;
import java.util.List;

/**
 * 诊断：按 Java 参考语义逐日聚合周 K，并打印指定 idx 当天的 周 SAR / close 全精度值
 *
 * 用法: DumpWeekSar <series.csv> <idx>
 */
public class DumpWeekSar {

    public static void main(String[] args) throws Exception {
        String in = args[0];
        int target = Integer.parseInt(args[1]);

        List<LocalDate> dates = new ArrayList<>();
        List<Double> O = new ArrayList<>(), H = new ArrayList<>(), L = new ArrayList<>(), Cn = new ArrayList<>();
        try (BufferedReader br = new BufferedReader(new FileReader(in))) {
            String line;
            while ((line = br.readLine()) != null) {
                line = line.trim();
                if (line.isEmpty() || line.startsWith("#") || line.startsWith("date,")) continue;
                String[] p = line.split(",");
                dates.add(LocalDate.parse(p[0]));
                O.add(Double.parseDouble(p[1]));
                H.add(Double.parseDouble(p[2]));
                L.add(Double.parseDouble(p[3]));
                Cn.add(Double.parseDouble(p[4]));
            }
        }

        final WeekFields ISO = WeekFields.ISO;
        Integer curWeekYear = null, curWeekNo = null;
        List<double[]> weeks = new ArrayList<>(); // {high, low, close}
        for (int i = 0; i <= target; i++) {
            LocalDate d = dates.get(i);
            int wYear = d.get(ISO.weekBasedYear());
            int wNo = d.get(ISO.weekOfWeekBasedYear());
            if (curWeekYear == null || curWeekYear != wYear || curWeekNo != wNo) {
                weeks.add(new double[]{H.get(i), L.get(i), Cn.get(i)});
                curWeekYear = wYear;
                curWeekNo = wNo;
            } else {
                double[] last = weeks.get(weeks.size() - 1);
                last[0] = Math.max(last[0], H.get(i));
                last[1] = Math.min(last[1], L.get(i));
                last[2] = Cn.get(i);
            }
        }
        int nw = weeks.size();
        double[] wH = new double[nw], wL = new double[nw], wC = new double[nw];
        for (int k = 0; k < nw; k++) {
            wH[k] = weeks.get(k)[0];
            wL[k] = weeks.get(k)[1];
            wC[k] = weeks.get(k)[2];
        }
        double[] sar = TdxFun.TDX_SAR(wH, wL);
        System.out.printf("date=%s weeks=%d%n", dates.get(target), nw);
        for (int k = Math.max(0, nw - 4); k < nw; k++) {
            System.out.printf("  week[%d] H=%.17g L=%.17g C=%.17g SAR=%.17g%n", k, wH[k], wL[k], wC[k], sar[k]);
        }
        System.out.printf("  close=%.17g  sarLast=%.17g  close>=sar : %s  diff=%.17g%n",
                Cn.get(target), sar[nw - 1], (Cn.get(target) >= sar[nw - 1]), (Cn.get(target) - sar[nw - 1]));
    }
}
