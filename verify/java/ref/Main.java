package ref;

import java.io.BufferedReader;
import java.io.FileReader;
import java.io.FileWriter;
import java.io.PrintWriter;
import java.time.LocalDate;
import java.util.ArrayList;
import java.util.List;

/**
 * Java 参考实现驱动（真实 TdxFun + MonthlyBullSignal2 逻辑）
 *
 * 输入 CSV：date,open,high,low,close,preBreak,expectedYueDuo
 * 输出 CSV：idx,date,javaYueDuo,expected
 */
public class Main {

    public static void main(String[] args) throws Exception {
        String in = args[0];
        String out = args[1];

        List<KBar> bars = new ArrayList<>();
        List<Boolean> preBreak = new ArrayList<>();
        List<Boolean> expected = new ArrayList<>();

        try (BufferedReader br = new BufferedReader(new FileReader(in))) {
            String line;
            while ((line = br.readLine()) != null) {
                line = line.trim();
                if (line.isEmpty() || line.startsWith("#") || line.startsWith("date,")) continue;
                String[] p = line.split(",");
                LocalDate d = LocalDate.parse(p[0]);
                bars.add(new KBar(d, Double.parseDouble(p[1]), Double.parseDouble(p[2]),
                        Double.parseDouble(p[3]), Double.parseDouble(p[4])));
                preBreak.add(p.length > 5 && !p[5].isEmpty() && Double.parseDouble(p[5]) != 0);
                expected.add(p.length > 6 && !p[6].isEmpty() && Double.parseDouble(p[6]) != 0);
            }
        }

        boolean[] pb = new boolean[preBreak.size()];
        for (int i = 0; i < pb.length; i++) pb[i] = preBreak.get(i);
        MonthlyBull2Ref.preBreakInjected = pb;

        boolean[] res = MonthlyBull2Ref.computeMonthlyBull(bars);

        try (PrintWriter pw = new PrintWriter(new FileWriter(out))) {
            pw.println("idx,date,java,expected");
            for (int i = 0; i < res.length; i++) {
                pw.println(i + "," + bars.get(i).date + "," + (res[i] ? 1 : 0) + "," + (expected.get(i) ? 1 : 0));
            }
        }
        System.out.println("java done: " + res.length + " days");
    }
}
