package ssatr.service;

import java.util.HashMap;
import java.util.Locale;
import java.util.Map;

/**
 * Ultima stare cunoscută a fiecărui device: ultima temperatură, momentul
 * ultimului mesaj, numărul de alarme etc.
 *
 * Este actualizată de thread-urile MessageProcessor și citită de CommandConsole,
 * deci accesul trebuie sincronizat.
 */
public class Dashboard {

    // TODO 5: alegeți structura de date pentru starea device-urilor
    // (de exemplu un Map<String, ...> cu cheia deviceId)
    // și adăugați metodele de actualizare apelate din MessageProcessor.
    private final Map<String, DeviceState> devices = new HashMap<>();

    public synchronized void updateTelemetry(String deviceId, long timestamp, double temperature) {
        DeviceState state = devices.computeIfAbsent(deviceId, id -> new DeviceState());
        state.temperature = temperature;
        state.lastMessageTimestamp = timestamp;
    }

    public synchronized void updateAlarm(String deviceId, long timestamp, double temperature) {
        DeviceState state = devices.computeIfAbsent(deviceId, id -> new DeviceState());
        state.temperature = temperature;
        state.lastMessageTimestamp = timestamp;
        state.alarmCount++;
    }

    public synchronized void updateStatus(String deviceId, long timestamp) {
        DeviceState state = devices.computeIfAbsent(deviceId, id -> new DeviceState());
        state.lastMessageTimestamp = timestamp;
    }

    /** Afișează un tabel cu starea tuturor device-urilor. */
    public synchronized void print() {
        System.out.printf("%-16s %-10s %-18s %s%n", "DEVICE", "TEMP", "ULTIMUL MESAJ", "ALARME");

        long now = System.currentTimeMillis();

        for (Map.Entry<String, DeviceState> entry : devices.entrySet()) {
            DeviceState state = entry.getValue();
            String temperature = Double.isNaN(state.temperature)
                    ? "-"
                    : String.format(Locale.US, "%.2f °C", state.temperature);
            long ageSeconds = Math.max(0, (now - state.lastMessageTimestamp) / 1000);

            System.out.printf(
                    "%-16s %-10s acum %d s           %d%n",
                    entry.getKey(),
                    temperature,
                    ageSeconds,
                    state.alarmCount
            );
        }
    }

    private static class DeviceState {
        private double temperature = Double.NaN;
        private long lastMessageTimestamp;
        private int alarmCount;
    }
}