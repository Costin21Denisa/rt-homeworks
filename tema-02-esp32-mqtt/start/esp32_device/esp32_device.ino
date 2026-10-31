// Device-ul IoT din tema 01, de data aceasta pe un ESP32-C3 Super Mini.
//
// Aceeași logică și același protocol ca device-simulator din tema 01, cu trei
// taskuri FreeRTOS în loc de trei thread-uri Java:
//
//   task periodic --(coada de alarme)--> task sporadic
//        |                                     |
//        +---- telemetry ---> MQTT <--- alarm -+
//                              |
//                          commands --> command_received() --(coada de comenzi)--> task aperiodic ---> status
//
// Gata implementate (nu trebuie modificate):
//   config.h         setările: Wi-Fi, broker, STUDENT, valorile inițiale; DEVICE_ID
//                    se citește din adresa MAC a plăcii
//   connection.h/cpp Wi-Fi, ora exactă (SNTP), clientul MQTT, mqtt_publish(), epoch_ms()
//
// Așa cum este, schița se conectează la Wi-Fi și la broker și afișează pe
// Serial comenzile primite. Taskurile le scrieți voi: TODO 0 ... TODO 3.
// Exemplele de pornire sunt în laboratorul 0 din rt-labs-esp32c3; vezi
// exemple/README.md din folderul temei.
//
// Placa: ESP32C3 Dev Module, USB CDC On Boot: Enabled. Serial Monitor: 115200 baud.

#include "config.h"
#include "connection.h"

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>

#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

struct AlarmEvent {
    float temperature;
    float threshold;
};

struct CommandMessage {
    char text[COMMAND_MAX_LEN];
};

static QueueHandle_t alarmQueue = nullptr;
static QueueHandle_t commandQueue = nullptr;

static volatile uint32_t currentPeriodMs = PERIOD_MS_INITIAL;
static volatile float currentThreshold = THRESHOLD_INITIAL;

// Temperatura cipului, în °C, de la senzorul intern al ESP32-C3.
// Este temperatura siliciului, nu a camerei: imediat după pornirea Wi-Fi-ului
// poate fi în jur de 50 °C, apoi scade în câteva minute spre 35-40 °C.
// Rezoluția este de aproximativ 1 °C. Ca să provocați alarme, puneți pragul
// cu SET_THRESHOLD cu un grad sub valoarea citită.
float read_temperature(void) {
    return temperatureRead();
}
static void task_periodic(void* parameter) {
    (void)parameter;

    TickType_t lastWake = xTaskGetTickCount();

    for (;;) {
        const float temperature = read_temperature();
        const float threshold = currentThreshold;

        char message[96];
        snprintf(
            message,
            sizeof(message),
            "%s;%lld;%.2f",
            DEVICE_ID,
            (long long)epoch_ms(),
            temperature
        );

        mqtt_publish("telemetry", message);
        Serial.printf("[periodic] temperatura = %.2f C\n", temperature);

        if (temperature > threshold) {
            AlarmEvent event{temperature, threshold};

            if (xQueueSend(alarmQueue, &event, 0) != pdPASS) {
                Serial.println("[periodic] coada de alarme este plina; evenimentul a fost pierdut");
            }
        }

        xTaskDelayUntil(&lastWake, pdMS_TO_TICKS(currentPeriodMs));
    }
}

// TODO 1: TASK PERIODIC, cu perioada T (inițial PERIOD_MS_INITIAL).
//   Fiecare job:
//   - citește temperatura: read_temperature()
//   - publică telemetria: mqtt_publish("telemetry", "deviceId;timestamp;temperatura")
//     (timestamp = epoch_ms(), tipărit cu %lld; temperatura cu "%.2f")
//   - dacă temperatura depășește pragul (inițial THRESHOLD_INITIAL), pune
//     un eveniment în coada de alarme, fără să se blocheze (timeout 0).
//     Taskul periodic NU tratează alarma: o tratează taskul sporadic.
//   Eliberările respectă grila r_k = r_0 + kT: folosiți așteptare absolută
//   (xTaskDelayUntil), nu relativă (delay, vTaskDelay), ca să nu apară derivă.
//   Perioada și pragul se schimbă prin comenzi (taskul aperiodic), deci sunt
//   variabile globale volatile, citite la fiecare job.
static void task_sporadic(void* parameter) {
    (void)parameter;

    bool hasPublishedAlarm = false;
    uint32_t lastAlarmMs = 0;
    AlarmEvent event;

    for (;;) {
        if (xQueueReceive(alarmQueue, &event, portMAX_DELAY) == pdTRUE) {
            const uint32_t now = millis();

            if (hasPublishedAlarm &&
                (uint32_t)(now - lastAlarmMs) < MIN_INTERARRIVAL_MS) {
                Serial.println("[sporadic] alarmă respinsă: intervalul minim nu a trecut");
                continue;
            }

            char message[112];
            snprintf(
                message,
                sizeof(message),
                "%s;%lld;%.2f;%.2f",
                DEVICE_ID,
                (long long)epoch_ms(),
                event.temperature,
                event.threshold
            );

            mqtt_publish("alarm", message);
            Serial.printf(
                "[sporadic] alarmă publicată: temperatura=%.2f C, prag=%.2f C\n",
                event.temperature,
                event.threshold
            );

            lastAlarmMs = now;
            hasPublishedAlarm = true;
        }
    }
}

static void task_aperiodic(void* parameter) {
    (void)parameter;

    CommandMessage command;
    char reply[96];
    char statusMessage[160];

    for (;;) {
        if (xQueueReceive(commandQueue, &command, portMAX_DELAY) != pdTRUE) {
            continue;
        }

        if (strcmp(command.text, "RESET") == 0) {
            currentPeriodMs = PERIOD_MS_INITIAL;
            currentThreshold = THRESHOLD_INITIAL;
            snprintf(reply, sizeof(reply), "OK RESET");
        } else if (strcmp(command.text, "STATUS") == 0) {
            snprintf(
                reply, sizeof(reply),
                "OK STATUS PERIOD_MS=%lu THRESHOLD=%.2f",
                (unsigned long)currentPeriodMs,
                (double)currentThreshold
            );
        } else if (strncmp(command.text, "SET_INTERVAL", 12) == 0) {
            const char* argument = command.text + 12;
            while (isspace((unsigned char)*argument)) argument++;

            char* end = nullptr;
            errno = 0;
            unsigned long value = strtoul(argument, &end, 10);

            while (isspace((unsigned char)*end)) end++;

            if (argument == end || !isdigit((unsigned char)*argument) ||
                errno == ERANGE || *end != '\0' || value == 0 ||
                pdMS_TO_TICKS(value) == 0) {
                snprintf(reply, sizeof(reply), "ERROR invalid SET_INTERVAL");
            } else {
                currentPeriodMs = (uint32_t)value;
                snprintf(
                    reply, sizeof(reply),
                    "OK SET_INTERVAL %lu", value
                );
            }
        } else if (strncmp(command.text, "SET_THRESHOLD", 13) == 0) {
            const char* argument = command.text + 13;
            while (isspace((unsigned char)*argument)) argument++;

            char* end = nullptr;
            errno = 0;
            float value = strtof(argument, &end);

            while (isspace((unsigned char)*end)) end++;

            if (argument == end || errno == ERANGE ||
                *end != '\0' || !isfinite(value)) {
                snprintf(reply, sizeof(reply), "ERROR invalid SET_THRESHOLD");
            } else {
                currentThreshold = value;
                snprintf(
                    reply, sizeof(reply),
                    "OK SET_THRESHOLD %.2f", (double)value
                );
            }
        } else {
            snprintf(reply, sizeof(reply), "ERROR unknown command");
        }

        snprintf(
            statusMessage, sizeof(statusMessage),
            "%s;%lld;%s",
            DEVICE_ID,
            (long long)epoch_ms(),
            reply
        );

        mqtt_publish("status", statusMessage);
        Serial.printf("[aperiodic] %s -> %s\n", command.text, reply);
    }
}
// TODO 2: TASK SPORADIC, tratează alarmele de temperatură.
//   - așteaptă un eveniment din coada de alarme
//   - dacă de la ultima alarmă publicată au trecut mai puțin de
//     MIN_INTERARRIVAL_MS, respinge evenimentul (afișați un mesaj pe Serial)
//   - altfel publică: mqtt_publish("alarm", "deviceId;timestamp;temperatura;prag")
//   Pentru intervale folosiți un timp monoton, millis(), nu epoch_ms().

// TODO 3: TASK APERIODIC, execută comenzile primite de la business service.
//   Comenzile ajung în command_received() (mai jos), care le pune în coada de
//   comenzi; fiecare comandă eliberează un job al taskului:
//     SET_INTERVAL <ms>       schimbă perioada T a taskului periodic
//     SET_THRESHOLD <valoare> schimbă pragul de alarmă
//     RESET                   readuce perioada și pragul la valorile inițiale
//     STATUS                  raportează perioada și pragul curente
//   După fiecare comandă publică o confirmare:
//     mqtt_publish("status", "deviceId;timestamp;OK SET_INTERVAL 1000")
//   O comandă necunoscută sau un parametru invalid nu oprește taskul: se
//   publică "ERROR <motiv>".

// Apelată de clientul MQTT pentru fiecare comandă primită (vezi connection.h).
void command_received(const char* text) {
    Serial.printf("[mqtt] comandă primită: %s\n", text);
    // TODO 3: puneți comanda în coada de comenzi, fără să vă blocați
    //   (timeout 0). text este valid doar în timpul acestui apel: elementul
    //   cozii trebuie să conțină textul însuși (de exemplu un struct cu
    //   char text[COMMAND_MAX_LEN]), nu un pointer.
    CommandMessage command{};
snprintf(command.text, sizeof(command.text), "%s", text);

if (commandQueue == nullptr ||
    xQueueSend(commandQueue, &command, 0) != pdPASS) {
    Serial.println("[mqtt] coada de comenzi este plina; comanda a fost pierduta");
}
}

void setup() {
    Serial.begin(115200);
    // Fără așteptare la scriere: cu placa la USB, dar fără Serial Monitor deschis,
    // fiecare Serial.printf() ar aștepta până la 2 s să fie citit și ar întârzia
    // taskul care scrie. Așa, textul pe care nu-l citește nimeni se pierde.
   // Serial.setTxTimeoutMs(0);
    uint32_t t = millis();
    while (!Serial && millis() - t < 3000) delay(10);  // așteptăm Serial Monitor, cel mult 3 s
    delay(200);
    Serial.println();
    Serial.printf("=== tema 02: STUDENT %s, DEVICE_ID %s ===\n", STUDENT, DEVICE_ID);

    wifi_connect();
    time_sync();

    // TODO 0 (a): creați cele două cozi (xQueueCreate): coada de alarme și
    //   coada de comenzi. Înainte de mqtt_start(): de acolo încolo pot sosi
    //   comenzi, iar command_received() le pune în coada de comenzi.
    alarmQueue = xQueueCreate(10, sizeof(AlarmEvent));
    commandQueue = xQueueCreate(10, sizeof(CommandMessage));

    if (alarmQueue == nullptr || commandQueue == nullptr) {
      Serial.println("EROARE: nu s-au putut crea cozile FreeRTOS");
      for (;;) {
          delay(1000);
     }
}

    mqtt_start();

  BaseType_t periodicResult = xTaskCreate(
    task_periodic, "periodic", 4096, nullptr, 3, nullptr
);
BaseType_t sporadicResult = xTaskCreate(
    task_sporadic, "sporadic", 4096, nullptr, 2, nullptr
);
BaseType_t aperiodicResult = xTaskCreate(
    task_aperiodic, "aperiodic", 4096, nullptr, 3, nullptr
);

if (periodicResult != pdPASS ||
    sporadicResult != pdPASS ||
    aperiodicResult != pdPASS) {
    Serial.println("EROARE: nu s-au putut porni toate taskurile");
}
    // TODO 0 (b): porniți cele trei taskuri (xTaskCreate). După mqtt_start(),
    //   ca primele mesaje publicate să nu se piardă. Alegeți prioritățile
    //   între 2 și 19: peste loopTask (1), sub taskurile sistemului pentru
    //   evenimente și Wi-Fi (20, 23). Stiva, în octeți: cel puțin 4096.
}

void loop() {
    // Totul se face în taskuri: loop() nu are nimic de făcut.
    delay(1000);
}