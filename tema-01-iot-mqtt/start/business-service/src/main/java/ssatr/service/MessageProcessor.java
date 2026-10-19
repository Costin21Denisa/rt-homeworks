package ssatr.service;

import java.util.concurrent.BlockingQueue;

/**
 * Preia mesajele de un anumit tip ("telemetry", "alarm" sau "status") din coada
 * inbox, le salvează și actualizează dashboard-ul.
 *
 * Formatul mesajelor (câmpuri separate prin ";"):
 *   telemetry:  deviceId;timestamp;temperatura
 *   alarm:      deviceId;timestamp;temperatura;prag
 *   status:     deviceId;timestamp;text
 */
public class MessageProcessor implements Runnable {

    private final String kind;
    private final BlockingQueue<String> inbox;
    private final CsvStorage storage;
    private final Dashboard dashboard;

    public MessageProcessor(String kind, BlockingQueue<String> inbox, CsvStorage storage, Dashboard dashboard) {
        this.kind = kind;
        this.inbox = inbox;
        this.storage = storage;
        this.dashboard = dashboard;
    }

    @Override
    public void run() {
        // TODO 4: într-o buclă:
        //   - preluați un mesaj: inbox.take()
        //   - salvați mesajul în fișierul CSV: storage.append(kind, mesaj)
        //     (CsvStorage este gata implementată)
        //   - actualizați dashboard-ul, în funcție de kind
        //   - alarmele și confirmările se afișează imediat în consolă
        //   - un mesaj greșit (format invalid) nu trebuie să oprească thread-ul
        while (!Thread.currentThread().isInterrupted()) {
            try {
                String message = inbox.take();
                storage.append(kind, message);
                processMessage(message);
            } catch (InterruptedException e) {
                Thread.currentThread().interrupt();
            } catch (RuntimeException e) {
                System.out.println("Mesaj " + kind + " invalid: " + e.getMessage());
            }
        }
    }

    private void processMessage(String message) {
        if ("telemetry".equals(kind)) {
            String[] fields = message.split(";", -1);
            if (fields.length != 3) {
                throw new IllegalArgumentException("format așteptat: deviceId;timestamp;temperatura");
            }

            String deviceId = fields[0].trim();
            long timestamp = Long.parseLong(fields[1].trim());
            double temperature = Double.parseDouble(fields[2].trim());

            if (deviceId.isEmpty()) {
                throw new IllegalArgumentException("deviceId lipsă");
            }

            dashboard.updateTelemetry(deviceId, timestamp, temperature);
        } else if ("alarm".equals(kind)) {
            String[] fields = message.split(";", -1);
            if (fields.length != 4) {
                throw new IllegalArgumentException("format așteptat: deviceId;timestamp;temperatura;prag");
            }

            String deviceId = fields[0].trim();
            long timestamp = Long.parseLong(fields[1].trim());
            double temperature = Double.parseDouble(fields[2].trim());
            Double.parseDouble(fields[3].trim());

            if (deviceId.isEmpty()) {
                throw new IllegalArgumentException("deviceId lipsă");
            }

            dashboard.updateAlarm(deviceId, timestamp, temperature);
            System.out.println("[ALARM] " + message);
        } else if ("status".equals(kind)) {
            String[] fields = message.split(";", 3);
            if (fields.length != 3) {
                throw new IllegalArgumentException("format așteptat: deviceId;timestamp;text");
            }

            String deviceId = fields[0].trim();
            long timestamp = Long.parseLong(fields[1].trim());
            String text = fields[2].trim();

            if (deviceId.isEmpty() || text.isEmpty()) {
                throw new IllegalArgumentException("deviceId sau text lipsă");
            }

            dashboard.updateStatus(deviceId, timestamp);
            System.out.println("[STATUS] " + deviceId + ": " + text);
        } else {
            throw new IllegalArgumentException("tip de mesaj necunoscut: " + kind);
        }
    }
}