package ref;

import java.time.LocalDate;

/** 与 MonthlyBullSignal.KlineBar 完全一致的 K 线结构（@Data @AllArgsConstructor） */
public class KBar {
    public LocalDate date;
    public double open, high, low, close;

    public KBar(LocalDate date, double open, double high, double low, double close) {
        this.date = date;
        this.open = open;
        this.high = high;
        this.low = low;
        this.close = close;
    }
}
