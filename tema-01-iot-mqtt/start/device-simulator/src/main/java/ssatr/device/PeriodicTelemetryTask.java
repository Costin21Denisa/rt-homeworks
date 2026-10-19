package ssatr.device;

import java.util.Locale;
import java.util.concurrent.BlockingQueue;
import java.util.concurrent.TimeUnit;

/**
 * TASK PERIODIC, cu perioada T = periodMs: fiecare job citește senzorul și
 * publică valoarea pe topic-ul de telemetrie.
 *
 * Dacă temperatura depășește pragul, taskul NU tratează el alarma, ci doar
 * semnalează un eveniment în coada alarmEvents. Alarma este tratată de
 * taskul sporadic.
 */
public class PeriodicTelemetryTask implements Runnable {

    private final String deviceId;
    private final TemperatureSensor sensor;
    private final MqttConnection mqtt;
    private final BlockingQueue<Double> alarmEvents;

    // volatile: sunt citite aici, dar modificate de taskul aperiodic (alt thread)
    private volatile long periodMs = 2000;
    private volatile double threshold = 28.0;

    public PeriodicTelemetryTask(String deviceId, TemperatureSensor sensor, MqttConnection mqtt,
                                 BlockingQueue<Double> alarmEvents) {
        this.deviceId = deviceId;
        this.sensor = sensor;
        this.mqtt = mqtt;
        this.alarmEvents = alarmEvents;
    }

    @Override
    public void run() {
        // TODO 1: implementați bucla taskului periodic. Fiecare job:
        //   - citiți temperatura: sensor.read()
        //   - publicați pe Config.telemetryTopic(deviceId) mesajul "deviceId;timestamp;temperatura"
        //     (timestamp = System.currentTimeMillis(); temperatura cu punct zecimal,
        //      de ex. String.format(Locale.US, "%.2f", t), ca să poată fi citită
        //      cu Double.parseDouble în business-service)
        //   - dacă temperatura > threshold, puneți valoarea în coada alarmEvents
        //
        // Folosiți așteptare absolută, nu relativă: Thread.sleep(periodMs) după
        // fiecare job duce la derivă (perioada reală devine T + C + întârzieri).
        // Calculați eliberarea următorului job pe grilă (next = next + periodMs)
        // și așteptați doar cât a mai rămas până la ea. Pentru grilă folosiți
        // un timp monoton: System.nanoTime().
        //
        // periodMs se poate schimba în timpul rulării (comanda SET_INTERVAL),
        // deci citiți-l la fiecare job.

        long nextRelease = System.nanoTime();

        try {
            while (!Thread.currentThread().isInterrupted()) {
                long currentPeriodMs = periodMs;

                double temperature = sensor.read();
                long timestamp = System.currentTimeMillis();

                String message = String.format(
                        Locale.US,
                        "%s;%d;%.2f",
                        deviceId,
                        timestamp,
                        temperature
                );

                mqtt.publish(Config.telemetryTopic(deviceId), message);

                if (temperature > threshold) {
                    alarmEvents.put(temperature);
                }

                nextRelease += TimeUnit.MILLISECONDS.toNanos(currentPeriodMs);
                long remainingNanos = nextRelease - System.nanoTime();

                if (remainingNanos > 0) {
                    TimeUnit.NANOSECONDS.sleep(remainingNanos);
                }
            }
        } catch (InterruptedException e) {
            Thread.currentThread().interrupt();
        }
    }

    public void setPeriodMs(long periodMs) {
        this.periodMs = periodMs;
    }

    public void setThreshold(double threshold) {
        this.threshold = threshold;
    }

    public long getPeriodMs() {
        return periodMs;
    }

    public double getThreshold() {
        return threshold;
    }
}