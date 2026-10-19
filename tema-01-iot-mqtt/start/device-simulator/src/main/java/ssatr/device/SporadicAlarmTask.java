package ssatr.device;

import java.util.Locale;
import java.util.concurrent.BlockingQueue;
import java.util.concurrent.TimeUnit;

/**
 * TASK SPORADIC: tratează alarmele de temperatură.
 *
 * Evenimentele sosesc în coada alarmEvents (puse de taskul periodic).
 * Intervalul minim între sosiri este MIN_INTERARRIVAL_MS: între două eliberări
 * ale taskului (două alarme publicate) trebuie să treacă cel puțin atât.
 * Intervalul este impus de cod: evenimentele care sosesc mai des sunt
 * respinse, ca să nu fie inundat brokerul.
 */
public class SporadicAlarmTask implements Runnable {

    static final long MIN_INTERARRIVAL_MS = 5000;

    private final String deviceId;
    private final MqttConnection mqtt;
    private final BlockingQueue<Double> alarmEvents;
    private final PeriodicTelemetryTask periodic;

    public SporadicAlarmTask(String deviceId, MqttConnection mqtt,
                             BlockingQueue<Double> alarmEvents,
                             PeriodicTelemetryTask periodic) {
        this.deviceId = deviceId;
        this.mqtt = mqtt;
        this.alarmEvents = alarmEvents;
        this.periodic = periodic;
    }

    @Override
    public void run() {
        // TODO 2: implementați taskul sporadic:
        //   - așteptați un eveniment: alarmEvents.take()
        //   - dacă de la ultima eliberare au trecut mai puțin de
        //     MIN_INTERARRIVAL_MS, respingeți evenimentul (afișați un mesaj)
        //   - altfel publicați pe Config.alarmTopic(deviceId) mesajul
        //     "deviceId;timestamp;temperatura;prag"
        //     (pragul curent poate fi transmis împreună cu temperatura sau
        //      citit din PeriodicTelemetryTask; alegeți și justificați în raport)
        //

        long lastReleaseNanos = 0;
        boolean hasPreviousRelease = false;
        long minimumIntervalNanos =
                TimeUnit.MILLISECONDS.toNanos(MIN_INTERARRIVAL_MS);

        try {
            while (!Thread.currentThread().isInterrupted()) {
                double temperature = alarmEvents.take();
                long now = System.nanoTime();

                if (hasPreviousRelease
                        && now - lastReleaseNanos < minimumIntervalNanos) {
                    System.out.printf(
                            Locale.US,
                            "Eveniment de alarmă respins: %.2f °C; nu au trecut încă %d ms.%n",
                            temperature,
                            MIN_INTERARRIVAL_MS
                    );
                    continue;
                }

                long timestamp = System.currentTimeMillis();
                double currentThreshold = periodic.getThreshold();

                String message = String.format(
                        Locale.US,
                        "%s;%d;%.2f;%.2f",
                        deviceId,
                        timestamp,
                        temperature,
                        currentThreshold
                );

                mqtt.publish(Config.alarmTopic(deviceId), message);

                lastReleaseNanos = now;
                hasPreviousRelease = true;
            }
        } catch (InterruptedException e) {
            Thread.currentThread().interrupt();
        }
    }
}