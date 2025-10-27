/********************************************************************
  Smart-Mailbox – Deep-Sleep + Button-Wake (Kalibrier-Erweiterung)
*********************************************************************
  Board  : Arduino Nano ESP32 (oder jeder andere ESP32)
  Sensor : HC-SR04  (Trig = GPIO 2, Echo = GPIO 4)
  Taster : GPIO 13 ↔ GND  (internes Pull-Up, EXT0-Wake)
  Push   : ntfy.sh Topic „briefkasten123“
********************************************************************/

#include <WiFi.h>
#include <HTTPClient.h>
#include <time.h>

/* ---------------- WLAN / ntfy ---------------------------------- */
#define TZ "CET-1CEST,M3.5.0/2,M10.5.0/3"
const char* ssid       = "IOT";
const char* password   = "hallo123";
const char* notify_url = "https://ntfy.sh/briefkasten123";

/*---------------- Webserver -----------------------*/ // NICHT HIER VERÄNDERN; HARDCODED!!

const char* API_URL   = "https://postkasten.4lima.de/mailbox.php";
const char* API_KEY   = "tPmAT5Ab3j7F9"; 

/* ---------------- Pins ----------------------------------------- */
constexpr int TRIG_PIN = 14;
constexpr int ECHO_PIN = 12;
constexpr int BTN_PIN  = 13;                 // LOW = gedrückt (RTC-GPIO)
 
/* ---------------- Intervall (Sekunden) ------------------------- */
const uint32_t SLEEP_INTERVAL_SEC = 5;      // zum Testen 10 s
const uint64_t SLEEP_TIME_US =
        (uint64_t)SLEEP_INTERVAL_SEC * 1000000ULL;

/* ---------------- Konstanten ----------------------------------- */
const float DELTA_CM = 3.0f;                 // Schwelle Post erkannt

/* ---------------- Persistente Daten (RTC-RAM) ------------------ */
RTC_DATA_ATTR float baselineCm = 10.0f;      // Referenz „leer“ (Standard: 40cm)
RTC_DATA_ATTR bool  hasMail    = false;      // letzter Status

/* ---------- Ultraschall-Messung -------------------------------- */
float measureDistanceCm()
{
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);

  long duration = pulseIn(ECHO_PIN, HIGH);   // blocking
  return duration * 0.0343f / 2.0f;          // cm
}

/* ---------- Push via ntfy -------------------------------------- */
bool connectWiFi()
{
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  Serial.print("WLAN …");
  uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < 8000) {
    Serial.print('.');
    delay(250);
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println(" verbunden");
    configTzTime(TZ, "pool.ntp.org", "time.nist.gov");
    return true;
  }
  Serial.println(" fehlgeschlagen");
  return false;
}

void pushToNtfy(const String& txt)
{
  if (!connectWiFi()) return;
  HTTPClient http;
  http.begin(notify_url);
  http.addHeader("Content-Type", "text/plain");
  int code = http.POST(txt);
  Serial.printf("ntfy HTTP-Code: %d\n", code);
  http.end();
}

void pushToWebserver(const String& text) {
  if (!connectWiFi()) return;

  HTTPClient http;
  http.begin("https://postkasten.4lima.de/mailbox.php");
  http.addHeader("Content-Type", "application/x-www-form-urlencoded");

  String post = "api_key=tPmAT5Ab3j7F9&msg=" + text;
  int code = http.POST(post);
  Serial.printf("HTTP-Code: %d\n", code);
  http.end();
}

/* ---------- SETUP (läuft bei jedem Wake) ----------------------- */
void setup()
{
  Serial.begin(115200);
  delay(100);

  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  pinMode(BTN_PIN , INPUT_PULLUP);

  /* ---- Wake-Ursache bestimmen -------------------------------- */
  esp_sleep_wakeup_cause_t wakeReason = esp_sleep_get_wakeup_cause();
  bool wokeByBtn = (wakeReason == ESP_SLEEP_WAKEUP_EXT0);

  /* ---- Kalibrier-Pfad: EXT0-Wake (Button) --------------------- */
  if (wokeByBtn) {
    float dist = measureDistanceCm();
    baselineCm = dist;       // neue Referenz
    hasMail    = false;      // Status zurücksetzen
    Serial.printf("*** Neue Referenz: %.1f cm ***\n", dist);

    String msg = String("Neue Referenz: ") + String(dist, 1) + " cm";
    
    //pushToNtfy(msg);         // nur die Referenz melden

    pushToWebserver(msg);

    // sofort wieder schlafen
    Serial.printf("Schlafe %lu s …\n", SLEEP_INTERVAL_SEC);
    esp_sleep_enable_timer_wakeup(SLEEP_TIME_US);
    esp_sleep_enable_ext0_wakeup((gpio_num_t)BTN_PIN, 0); // Button erneut
    esp_deep_sleep_start();
  }

  /* ---- Kalibrier-Knopf beim Kaltstart gedrückt? --------------- */
  if (digitalRead(BTN_PIN) == LOW) {
    delay(20);
    if (digitalRead(BTN_PIN) == LOW) {
      baselineCm = measureDistanceCm();
      hasMail    = false;
      Serial.printf("*** Neue Referenz (Kaltstart): %.1f cm ***\n",
                    baselineCm);
    }
  }

  /* ---- Einmalige Messung pro Zyklus --------------------------- */
  float dist    = measureDistanceCm();
  bool  mailNow = (dist < (baselineCm - DELTA_CM));

  if (mailNow && !hasMail) {                     // leer → Post
    Serial.printf("Messung %.1f cm | Ereignis: Post\n", dist);
    //pushToNtfy("Neue Post im Briefkasten!");
    pushToWebserver("MAIL"); 
    hasMail = true;
  }
  else if (!mailNow && hasMail) {                // Post → leer
    Serial.printf("Messung %.1f cm | Briefkasten geleert\n", dist);
    //pushToNtfy("Briefkasten leer");
    pushToWebserver("EMPTY"); 
    hasMail = false;
  }
  else {                                         // unverändert
    Serial.printf("Messung %.1f cm | %s\n",
                  dist, mailNow ? "Post (bereits gemeldet)" : "leer");
    
  }

  /* ---- Deep-Sleep mit Timer- & Button-Wake -------------------- */
  Serial.printf("Schlafe %lu s …\n", SLEEP_INTERVAL_SEC);
  esp_sleep_enable_timer_wakeup(SLEEP_TIME_US);           // Zeitwecker
  esp_sleep_enable_ext0_wakeup((gpio_num_t)BTN_PIN, 0);   // Button weckt
  esp_deep_sleep_start();
}

/* ---------- loop() wird nie erreicht --------------------------- */
void loop() {}
