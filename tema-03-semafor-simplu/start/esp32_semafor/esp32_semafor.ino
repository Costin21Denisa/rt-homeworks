// Semafor pentru pietoni, cu buton, pe ESP32-C3 Super Mini.
//
// Placa este controlerul semaforului; lămpile sunt desenate de dashboard, care
// le aprinde după mesajele "lights" publicate de placă. Patru activități:
//
//   ISR buton BOOT ----+
//                      +--(coada butonului)--> task sporadic
//   command_received --+   (BUTTON)                 |
//                                                   v
//                                         [ starea semaforului ] <-- mutex
//                                            ^          |
//                                            |          v
//                         task periodic de control   task periodic de ieșire
//                         (fazele, T = 100 ms)       (lămpile, T = 500 ms) --> lights
//
// Gata implementate (nu trebuie modificate):
//   config.h         setările: Wi-Fi, broker, STUDENT, pinii, perioadele, duratele fazelor
//   connection.h/cpp Wi-Fi, ora exactă (SNTP), clientul MQTT, mqtt_publish(), epoch_ms()
//
// Așa cum este, schița se conectează la Wi-Fi și la broker și afișează pe
// Serial comenzile primite. Restul îl scrieți voi: TODO 0 ... TODO 3.
//
// Placa: ESP32C3 Dev Module, USB CDC On Boot: Enabled. Serial Monitor: 115200 baud.

#include "config.h"
#include "connection.h"

// Fazele semaforului (vezi tabelul și diagrama din README.md). Numele din
// PHASE_NAMES sunt cele din mesajul "lights", în aceeași ordine ca în enum.
enum Phase { CAR_GREEN, CAR_YELLOW, ALL_RED, PED_GREEN };
const char* const PHASE_NAMES[] = {"CAR_GREEN", "CAR_YELLOW", "ALL_RED", "PED_GREEN"};

// Culorile unei lămpi, cu numele din mesajul "lights".
enum Light { RED, YELLOW, GREEN };
const char* const LIGHT_NAMES[] = {"RED", "YELLOW", "GREEN"};

// TODO 0 (c): STAREA SEMAFORULUI, partajată de taskuri și protejată de un
//   mutex (xSemaphoreCreateMutex). De exemplu un struct cu:
//   - faza curentă
//   - momentul intrării în faza curentă (millis()), din care se calculează
//     cât a durat faza și cât mai are
//   - cererea pietonului (lampa AȘTEPTAȚI): setată de taskul sporadic,
//     ștearsă de taskul de control la intrarea în PED_GREEN
//   Mutexul se ține cât mai puțin: în el doar citiți sau scrieți câmpurile
//   (eventual copiați starea într-o variabilă locală). Fără mqtt_publish(),
//   Serial sau așteptări cât timp țineți mutexul.
struct TrafficState {
  Phase phase;
  uint32_t phaseStartedMs;
  bool pedestrianRequest;
};

static TrafficState trafficState = {CAR_GREEN, 0, false};
static SemaphoreHandle_t stateMutex = nullptr;

struct ButtonPress {
  TickType_t arrivedAtTicks;
};

static QueueHandle_t buttonQueue = nullptr;

// TODO 1: TASK PERIODIC DE CONTROL, cu perioada CONTROL_PERIOD_MS.
//   La fiecare job, cu mutexul luat, decide dacă faza curentă s-a terminat și
//   trece în faza următoare, după tabelul din README.md:
//     CAR_GREEN  -> CAR_YELLOW  după CAR_GREEN_MIN_MS, doar dacă există o cerere
//     CAR_YELLOW -> ALL_RED     după CAR_YELLOW_MS
//     ALL_RED    -> PED_GREEN   după ALL_RED_MS; aici se șterge cererea pietonului
//     PED_GREEN  -> CAR_GREEN   după PED_GREEN_MS
//   La fiecare schimbare de fază notați momentul intrării în faza nouă.
//   Taskul nu publică nimic: doar actualizează starea. După ce eliberează
//   mutexul, afișează pe Serial faza nouă, dacă s-a schimbat.
//   Eliberările respectă grila r_k = r_0 + kT: așteptare absolută (xTaskDelayUntil).
//   Durata unei faze se măsoară cu un timp monoton (millis()), nu cu epoch_ms().
      static void task_control(void* parameter) {
  (void)parameter;
  TickType_t lastWake = xTaskGetTickCount();

  for (;;) {
    const uint32_t now = millis();
    Phase phaseToPrint = CAR_GREEN;
    bool phaseChanged = false;

    if (xSemaphoreTake(stateMutex, portMAX_DELAY) == pdTRUE) {
      const uint32_t elapsed = now - trafficState.phaseStartedMs;
      Phase nextPhase = trafficState.phase;

      switch (trafficState.phase) {
        case CAR_GREEN:
          if (trafficState.pedestrianRequest &&
              elapsed >= CAR_GREEN_MIN_MS) {
            nextPhase = CAR_YELLOW;
          }
          break;

        case CAR_YELLOW:
          if (elapsed >= CAR_YELLOW_MS) {
            nextPhase = ALL_RED;
          }
          break;

        case ALL_RED:
          if (elapsed >= ALL_RED_MS) {
            nextPhase = PED_GREEN;
          }
          break;

        case PED_GREEN:
          if (elapsed >= PED_GREEN_MS) {
            nextPhase = CAR_GREEN;
          }
          break;
      }

      if (nextPhase != trafficState.phase) {
        trafficState.phase = nextPhase;
        trafficState.phaseStartedMs = now;

        if (nextPhase == PED_GREEN) {
          trafficState.pedestrianRequest = false;
        }

        phaseToPrint = nextPhase;
        phaseChanged = true;
      }

      xSemaphoreGive(stateMutex);
    }

    if (phaseChanged) {
      Serial.printf("[control] faza nouă: %s\n", PHASE_NAMES[phaseToPrint]);
    }

    xTaskDelayUntil(&lastWake, pdMS_TO_TICKS(CONTROL_PERIOD_MS));
  }
}
// TODO 2: TASK PERIODIC DE IEȘIRE, cu perioada OUTPUT_PERIOD_MS.
//   La fiecare job:
//   - copiază starea (cu mutexul luat, cât mai scurt)
//   - stabilește culorile celor două lămpi din faza curentă (tabelul din README.md)
//   - calculează secundele rămase din verdele pietonilor: în PED_GREEN,
//     rotunjite în sus (7,2 s rămase -> 8); 0 în celelalte faze
//   - publică: mqtt_publish("lights", "deviceId;timestamp;faza;masini;pietoni;asteptati;secunde")
//     de exemplu "esp32-a1b2c3;1727600000000;PED_GREEN;RED;GREEN;0;8"
//   - aprinde LED-ul de pe placă (LED_PIN, aprins pe LOW) cât timp lampa
//     AȘTEPTAȚI este aprinsă
//   Așteptare absolută (xTaskDelayUntil), ca și la taskul de control.
static void task_output(void* parameter) {
  (void)parameter;
  TickType_t lastWake = xTaskGetTickCount();

  for (;;) {
    TrafficState snapshot;

    if (xSemaphoreTake(stateMutex, portMAX_DELAY) == pdTRUE) {
      snapshot = trafficState;
      xSemaphoreGive(stateMutex);
    }

    Light carLight = RED;
    Light pedestrianLight = RED;

    switch (snapshot.phase) {
      case CAR_GREEN:
        carLight = GREEN;
        break;
      case CAR_YELLOW:
        carLight = YELLOW;
        break;
      case ALL_RED:
        break;
      case PED_GREEN:
        pedestrianLight = GREEN;
        break;
    }

    uint32_t secondsRemaining = 0;
    if (snapshot.phase == PED_GREEN) {
      const uint32_t elapsed = millis() - snapshot.phaseStartedMs;
      if (elapsed < PED_GREEN_MS) {
        const uint32_t remainingMs = PED_GREEN_MS - elapsed;
        secondsRemaining = (remainingMs + 999) / 1000;
      }
    }

    const int waiting = snapshot.pedestrianRequest ? 1 : 0;
    digitalWrite(LED_PIN, waiting ? LOW : HIGH);

    char message[160];
    snprintf(
      message,
      sizeof(message),
      "%s;%llu;%s;%s;%s;%d;%lu",
      DEVICE_ID,
      (unsigned long long)epoch_ms(),
      PHASE_NAMES[snapshot.phase],
      LIGHT_NAMES[carLight],
      LIGHT_NAMES[pedestrianLight],
      waiting,
      (unsigned long)secondsRemaining
    );

    mqtt_publish("lights", message);

    xTaskDelayUntil(&lastWake, pdMS_TO_TICKS(OUTPUT_PERIOD_MS));
  }
}
// TODO 3: TASK SPORADIC, tratează apăsările butonului pentru pietoni.
//   Apăsările vin din două surse, în aceeași coadă (coada butonului):
//   - butonul BOOT de pe placă, printr-o rutină de întrerupere (ISR, mai jos)
//   - butonul virtual din dashboard: comanda "BUTTON", pusă în coadă de
//     command_received()
//   Taskul:
//   - așteaptă o apăsare din coada butonului
//   - dacă de la ultima apăsare acceptată au trecut mai puțin de
//     MIN_INTERARRIVAL_MS, o respinge (mesaj pe Serial). Așa se filtrează și
//     contactele imperfecte ale butonului (bounce), care produc mai multe
//     întreruperi la o singură apăsare.
//   - altfel înregistrează cererea pietonului în starea semaforului (cu mutexul)
//     și afișează pe Serial că a acceptat-o
//   Momentul sosirii se ia când apăsarea intră în coadă (în ISR sau în
//   command_received()), nu când o preia taskul, și se pune în elementul cozii.

// TODO 3: RUTINA DE ÎNTRERUPERE a butonului BOOT, înregistrată în setup() cu
//   attachInterrupt(digitalPinToInterrupt(BUTTON_PIN), button_isr, FALLING).
//   Scurtă: pune o apăsare în coada butonului cu xQueueSendFromISR() și atât.
//   Fără Serial, fără mqtt_publish(), fără mutex. Se declară cu IRAM_ATTR:
//
//   void IRAM_ATTR button_isr(void) { ... }
static void task_button(void* parameter) {
  (void)parameter;
  ButtonPress press;
  TickType_t lastAcceptedTicks = 0;
  bool hasAcceptedPress = false;

  for (;;) {
    if (xQueueReceive(buttonQueue, &press, portMAX_DELAY) == pdTRUE) {
      const TickType_t intervalTicks =
        press.arrivedAtTicks - lastAcceptedTicks;

      if (hasAcceptedPress &&
          intervalTicks < pdMS_TO_TICKS(MIN_INTERARRIVAL_MS)) {
        Serial.println("[button] apăsare respinsă: prea rapidă");
        continue;
      }

      lastAcceptedTicks = press.arrivedAtTicks;
      hasAcceptedPress = true;

      if (xSemaphoreTake(stateMutex, portMAX_DELAY) == pdTRUE) {
        trafficState.pedestrianRequest = true;
        xSemaphoreGive(stateMutex);
      }

      Serial.println("[button] cerere pieton acceptată");
    }
  }
}
void IRAM_ATTR button_isr(void) {
  if (buttonQueue == nullptr) {
    return;
  }

  ButtonPress press;
  press.arrivedAtTicks = xTaskGetTickCountFromISR();

  BaseType_t higherPriorityTaskWoken = pdFALSE;
  if (xQueueSendFromISR(
        buttonQueue,
        &press,
        &higherPriorityTaskWoken
      ) == pdTRUE &&
      higherPriorityTaskWoken == pdTRUE) {
    portYIELD_FROM_ISR();
  }
}
// Apelată de clientul MQTT pentru fiecare comandă primită (vezi connection.h).
void command_received(const char* text) {
    Serial.printf("[mqtt] comandă primită: %s\n", text);
    // TODO 3: "BUTTON" merge în coada butonului (o apăsare, la fel ca cea din
    //   ISR), fără să vă blocați (timeout 0). Celelalte comenzi se ignoră:
    //   în această temă placa nu are alte comenzi.
    if (text == nullptr || strcmp(text, "BUTTON") != 0) {
  return;
}

ButtonPress press;
press.arrivedAtTicks = xTaskGetTickCount();

if (buttonQueue == nullptr ||
    xQueueSend(buttonQueue, &press, 0) != pdTRUE) {
  Serial.println("[button] coada este plină; cererea nu a fost înregistrată");
}
}

void setup() {
    Serial.begin(115200);
    // Fără așteptare la scriere: cu placa la USB, dar fără Serial Monitor deschis,
    // fiecare Serial.printf() ar aștepta până la 2 s să fie citit și ar întârzia
    // taskul care scrie. Așa, textul pe care nu-l citește nimeni se pierde.
    //Serial.setTxTimeoutMs(0);
    uint32_t t = millis();
    while (!Serial && millis() - t < 3000) delay(10);  // așteptăm Serial Monitor, cel mult 3 s
    delay(200);
    Serial.println();
    Serial.printf("=== tema 03: STUDENT %s, DEVICE_ID %s ===\n", STUDENT, DEVICE_ID);

    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, HIGH);  // LED stins
    pinMode(BUTTON_PIN, INPUT_PULLUP);

    wifi_connect();
    time_sync();

    // TODO 0 (a): creați mutexul stării (xSemaphoreCreateMutex) și coada
    //   butonului (xQueueCreate). Înainte de mqtt_start(): de acolo încolo pot
    //   sosi comenzi, iar command_received() pune "BUTTON" în coadă.
    //   Notați și momentul intrării în faza inițială (CAR_GREEN).
      stateMutex = xSemaphoreCreateMutex();
      buttonQueue = xQueueCreate(8, sizeof(ButtonPress));

      if (stateMutex == nullptr || buttonQueue == nullptr) {
      Serial.println("Eroare la crearea mutexului sau a cozii");
       while (true) {
       delay(1000);
  }
}

trafficState.phase = CAR_GREEN;
trafficState.phaseStartedMs = millis();
trafficState.pedestrianRequest = false;
    mqtt_start();

    // TODO 0 (b): porniți cele trei taskuri (xTaskCreate), apoi înregistrați
    //   întreruperea butonului (attachInterrupt). Alegeți prioritățile între
    //   2 și 19 (peste loopTask, sub taskurile sistemului); pentru cele două
    //   taskuri periodice, după regula Rate Monotonic. Stiva, în octeți: cel puțin 4096.
      BaseType_t controlCreated = xTaskCreate(
  task_control, "control", 4096, nullptr, 19, nullptr
);
BaseType_t outputCreated = xTaskCreate(
  task_output, "output", 4096, nullptr, 18, nullptr
);
BaseType_t buttonCreated = xTaskCreate(
  task_button, "button", 4096, nullptr, 17, nullptr
);

if (controlCreated != pdPASS ||
    outputCreated != pdPASS ||
    buttonCreated != pdPASS) {
  Serial.println("Eroare la pornirea taskurilor");
  while (true) {
    delay(1000);
  }
}

attachInterrupt(
  digitalPinToInterrupt(BUTTON_PIN),
  button_isr,
  FALLING
);
}

void loop() {
    // Totul se face în taskuri: loop() nu are nimic de făcut.
    delay(1000);
}
