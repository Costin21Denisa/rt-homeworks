package ssatr.service;

import java.util.Scanner;

/**
 * Consola operatorului: citește comenzi de la tastatură.
 *
 *   show                               afișează dashboard-ul
 *   <deviceId> <COMANDA> [parametru]   trimite o comandă unui device, de exemplu
 *                                      device-1 SET_INTERVAL 1000
 *   help                               afișează comenzile
 *   exit                               oprește serviciul
 */
public class CommandConsole implements Runnable {

    private final MqttConnection mqtt;
    private final Dashboard dashboard;

    public CommandConsole(MqttConnection mqtt, Dashboard dashboard) {
        this.mqtt = mqtt;
        this.dashboard = dashboard;
    }

    @Override
    public void run() {
        Scanner scanner = new Scanner(System.in);
        printHelp();

        while (scanner.hasNextLine()) {
            String line = scanner.nextLine().trim();

            if (line.isEmpty()) {
                continue;
            } else if (line.equals("exit")) {
                break;
            } else if (line.equals("help")) {
                printHelp();
            } else if (line.equals("show")) {
                dashboard.print();
            } else {
                sendCommand(line);
            }
        }
    }

    private void sendCommand(String line) {
        // TODO 6: separați deviceId de restul liniei
        // ("device-1 SET_INTERVAL 1000" -> "device-1" și "SET_INTERVAL 1000")
        // și publicați comanda pe Config.commandTopic(deviceId).
        // Afișați un mesaj de eroare dacă linia nu are formatul corect.
        String[] parts = line.trim().split("\\s+", 2);

        if (parts.length != 2 || !parts[0].matches("[A-Za-z0-9_-]+") || parts[1].trim().isEmpty()) {
            System.out.println("Format invalid. Exemplu: device-1 SET_INTERVAL 1000");
            return;
        }

        String deviceId = parts[0];
        String command = parts[1].trim();

        mqtt.publish(Config.commandTopic(deviceId), command);
        System.out.println("Comandă trimisă către " + deviceId + ": " + command);
    }

    private void printHelp() {
        System.out.println("Comenzi: show | <deviceId> SET_INTERVAL <ms> | <deviceId> SET_THRESHOLD <valoare>");
        System.out.println("         <deviceId> RESET | <deviceId> STATUS | help | exit");
    }
}