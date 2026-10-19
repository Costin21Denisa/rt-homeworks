package ssatr.device;

import java.util.Locale;
import java.util.concurrent.BlockingQueue;

/**
 * TASK APERIODIC: execută comenzile primite de la business service.
 *
 * Comenzile sosesc oricând, pe MQTT, fără un interval minim între ele, și sunt
 * puse în coada commands. Fiecare comandă eliberează un job al taskului.
 * Comenzi care trebuie suportate:
 *
 *   SET_INTERVAL <ms>       schimbă perioada T a taskului periodic
 *   SET_THRESHOLD <valoare> schimbă pragul de alarmă
 *   RESET                   readuce senzorul la valoarea inițială
 *   STATUS                  raportează perioada și pragul curente
 *
 * După executarea fiecărei comenzi, device-ul publică pe Config.statusTopic(deviceId)
 * o confirmare: "deviceId;timestamp;text", de exemplu
 * "device-1;1727600000000;OK SET_INTERVAL 1000".
 */
public class AperiodicCommandTask implements Runnable {

    private final String deviceId;
    private final TemperatureSensor sensor;
    private final MqttConnection mqtt;
    private final BlockingQueue<String> commands;
    private final PeriodicTelemetryTask periodic;

    public AperiodicCommandTask(String deviceId, TemperatureSensor sensor, MqttConnection mqtt,
                                BlockingQueue<String> commands, PeriodicTelemetryTask periodic) {
        this.deviceId = deviceId;
        this.sensor = sensor;
        this.mqtt = mqtt;
        this.commands = commands;
        this.periodic = periodic;
    }

    @Override
    public void run() {
        // TODO 3: implementați taskul aperiodic:
        //   - preluați comenzile din coadă: commands.take()
        //   - separați numele comenzii de parametru: command.split(" ")
        //   - executați comanda (periodic.setPeriodMs(...), sensor.reset() etc.)
        //   - publicați confirmarea pe Config.statusTopic(deviceId)
        //   - o comandă necunoscută sau un parametru invalid nu trebuie să
        //     oprească taskul: publicați "ERROR <motiv>"
        //
        // Vezi exemplul exemple/01-taskuri/.../AperiodicTaskExample.java

        try {
            while (!Thread.currentThread().isInterrupted()) {
                String command = commands.take();
                String response = executeCommand(command);
                publishStatus(response);
                System.out.println(deviceId + ": " + response);
            }
        } catch (InterruptedException e) {
            Thread.currentThread().interrupt();
        }
    }

    private String executeCommand(String command) {
        String[] parts = command.trim().split("\\s+");

        if (parts.length == 0 || parts[0].isEmpty()) {
            return "ERROR comanda este goală";
        }

        String action = parts[0].toUpperCase(Locale.ROOT);

        switch (action) {
            case "SET_INTERVAL":
                if (parts.length != 2) {
                    return "ERROR format: SET_INTERVAL <ms>";
                }
                try {
                    long periodMs = Long.parseLong(parts[1]);
                    if (periodMs <= 0) {
                        return "ERROR intervalul trebuie să fie mai mare decât 0";
                    }
                    periodic.setPeriodMs(periodMs);
                    return "OK SET_INTERVAL " + periodMs;
                } catch (NumberFormatException e) {
                    return "ERROR interval invalid";
                }

            case "SET_THRESHOLD":
                if (parts.length != 2) {
                    return "ERROR format: SET_THRESHOLD <valoare>";
                }
                try {
                    double threshold = Double.parseDouble(parts[1]);
                    if (!Double.isFinite(threshold)) {
                        return "ERROR prag invalid";
                    }
                    periodic.setThreshold(threshold);
                    return String.format(Locale.US, "OK SET_THRESHOLD %.2f", threshold);
                } catch (NumberFormatException e) {
                    return "ERROR prag invalid";
                }

            case "RESET":
                if (parts.length != 1) {
                    return "ERROR format: RESET";
                }
                sensor.reset();
                return "OK RESET";

            case "STATUS":
                if (parts.length != 1) {
                    return "ERROR format: STATUS";
                }
                return String.format(
                        Locale.US,
                        "OK STATUS periodMs=%d threshold=%.2f",
                        periodic.getPeriodMs(),
                        periodic.getThreshold()
                );

            default:
                return "ERROR comandă necunoscută: " + parts[0];
        }
    }

    private void publishStatus(String text) {
        String message = String.format(
                Locale.US,
                "%s;%d;%s",
                deviceId,
                System.currentTimeMillis(),
                text
        );
        mqtt.publish(Config.statusTopic(deviceId), message);
    }
}