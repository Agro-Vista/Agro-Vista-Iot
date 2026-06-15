#include <WiFi.h>
#include <esp_system.h>   // esp_random()
#include <PubSubClient.h>
#include <DHT.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <ArduinoJson.h>

// ─── Wi-Fi / MQTT ─────────────────────────────────────────────────────────────
const char* SSID        = "Wokwi-GUEST";
const char* PASSWORD    = "";
const char* MQTT_SERVER = "broker.hivemq.com";
const int   MQTT_PORT   = 1883;

// Client ID único para evitar conflito de sessões simultâneas
String mqttClientId = "AgroVista-" + String((uint32_t)esp_random(), HEX);

// ─── Pinos ────────────────────────────────────────────────────────────────────
#define DHTPIN       4
#define DHTTYPE      DHT22
#define LDR_PIN      34   // simula sensor de umidade do solo
#define LED_R        25
#define LED_G        26
#define LED_B        27
#define BUZZER_PIN   32
#define BTN_SECA     18   // botão vermelho — simula seca extrema
#define BTN_GEADA    19   // botão azul    — simula risco de geada

// ─── Tópicos MQTT ─────────────────────────────────────────────────────────────
#define T_TEMP   "agrovista/talhao/temperatura"
#define T_SOLO   "agrovista/talhao/solo"
#define T_STATUS "agrovista/talhao/status"
#define T_ALERTA "agrovista/talhao/alerta"
#define T_SIM    "agrovista/talhao/simulacao"

// ─── Limiares ─────────────────────────────────────────────────────────────────
#define SOLO_CRITICO   25   // % solo — abaixo = seca crítica
#define SOLO_ATENCAO   50   // % solo — abaixo = atenção
#define TEMP_GEADA     8.0  // °C    — abaixo = risco de geada
#define TEMP_CRITICA   40.0 // °C    — acima  = calor extremo

// ─── Objetos ──────────────────────────────────────────────────────────────────
DHT              dht(DHTPIN, DHTTYPE);
LiquidCrystal_I2C lcd(0x27, 16, 2);
WiFiClient       espClient;
PubSubClient     mqtt(espClient);

// ─── Estado global ────────────────────────────────────────────────────────────
float       temperatura   = 0;
float       umidadeAr     = 0;
int         umidadeSolo   = 0;
int         luminosidade  = 0;
String      statusAtual   = "normal";
String      motivoAlerta  = "";
bool        alertaAtivo   = false;
bool        modoSeca      = false;
bool        modoGeada     = false;
unsigned long ultimoEnvio = 0;
unsigned long ultimoBuzzer = 0;
int         lcdPagina     = 0;
unsigned long ultimoLCD   = 0;

// ════════════════════════════════════════════════════════════════════════════════
//  LED RGB
// ════════════════════════════════════════════════════════════════════════════════
void setLED(int r, int g, int b) {
  digitalWrite(LED_R, r);
  digitalWrite(LED_G, g);
  digitalWrite(LED_B, b);
}

void atualizarLED() {
  if (statusAtual == "critico")       setLED(HIGH, LOW,  LOW ); // vermelho
  else if (statusAtual == "atencao")  setLED(HIGH, HIGH, LOW ); // amarelo
  else if (modoGeada)                 setLED(LOW,  LOW,  HIGH); // azul = geada
  else                                setLED(LOW,  HIGH, LOW ); // verde = normal
}

// ════════════════════════════════════════════════════════════════════════════════
//  BUZZER — bips conforme severidade
// ════════════════════════════════════════════════════════════════════════════════
void bipBuzzer(int vezes, int duracao) {
  for (int i = 0; i < vezes; i++) {
    digitalWrite(BUZZER_PIN, HIGH);
    delay(duracao);
    digitalWrite(BUZZER_PIN, LOW);
    delay(100);
  }
}

// ════════════════════════════════════════════════════════════════════════════════
//  LCD — alterna entre 2 páginas
// ════════════════════════════════════════════════════════════════════════════════
void atualizarLCD() {
  if (millis() - ultimoLCD < 3000) return;
  ultimoLCD = millis();

  lcd.clear();

  if (lcdPagina == 0) {
    // Página 1: temperatura e umidade do ar
    lcd.setCursor(0, 0);
    lcd.print("T:");
    lcd.print(temperatura, 1);
    lcd.print("C Ar:");
    lcd.print((int)umidadeAr);
    lcd.print("%");

    lcd.setCursor(0, 1);
    lcd.print("Solo:");
    lcd.print(umidadeSolo);
    lcd.print("% ");
    lcd.print(statusAtual == "critico" ? "ALERTA!" :
              statusAtual == "atencao" ? "ATENCAO " : "NORMAL  ");
  } else {
    // Página 2: modo de simulação + recomendação
    lcd.setCursor(0, 0);
    if (modoSeca)       lcd.print("SIM: SECA CRITICA");
    else if (modoGeada) lcd.print("SIM: RISCO GEADA");
    else                lcd.print("AgroVista v2.0  ");

    lcd.setCursor(0, 1);
    if (alertaAtivo)    lcd.print("! Verificar campo");
    else                lcd.print("Talhao Sul - OK ");
  }

  lcdPagina = 1 - lcdPagina; // alterna página
}

// ════════════════════════════════════════════════════════════════════════════════
//  CLASSIFICAÇÃO DE STATUS
// ════════════════════════════════════════════════════════════════════════════════
String classificarStatus() {
  alertaAtivo  = false;
  motivoAlerta = "";

  // Seca crítica
  if (umidadeSolo < SOLO_CRITICO || temperatura > TEMP_CRITICA) {
    alertaAtivo  = true;
    motivoAlerta = umidadeSolo < SOLO_CRITICO ? "seca_critica" : "calor_extremo";
    return "critico";
  }

  // Geada
  if (temperatura < TEMP_GEADA) {
    alertaAtivo  = true;
    motivoAlerta = "risco_geada";
    return "critico";
  }

  // Atenção
  if (umidadeSolo < SOLO_ATENCAO) {
    motivoAlerta = "solo_seco";
    return "atencao";
  }

  return "normal";
}

// ════════════════════════════════════════════════════════════════════════════════
//  LEITURA DOS SENSORES
// ════════════════════════════════════════════════════════════════════════════════
void lerSensores() {
  float t = dht.readTemperature();
  float h = dht.readHumidity();
  if (!isnan(t)) temperatura = t;
  if (!isnan(h)) umidadeAr   = h;

  // LDR (0–4095) → umidade do solo (0–100%)
  int raw  = analogRead(LDR_PIN);
  luminosidade = raw;
  umidadeSolo  = map(raw, 0, 4095, 100, 0); // LDR: mais luz = mais seco

  // ── Botões de simulação de crise ──────────────────
  modoSeca  = !digitalRead(BTN_SECA);
  modoGeada = !digitalRead(BTN_GEADA);

  if (modoSeca) {
    umidadeSolo = 10;   // solo quase seco
    temperatura = 41.5;
    Serial.println(">>> [SIM] SECA CRITICA ativada");
  }

  if (modoGeada) {
    temperatura = 4.0;  // temperatura de geada
    Serial.println(">>> [SIM] RISCO DE GEADA ativado");
  }

  statusAtual = classificarStatus();
  atualizarLED();

  // Buzzer em situação crítica (a cada 10s para não travar)
  if (alertaAtivo && millis() - ultimoBuzzer > 10000) {
    ultimoBuzzer = millis();
    bipBuzzer(statusAtual == "critico" ? 3 : 1, 200);
  }
}

// ════════════════════════════════════════════════════════════════════════════════
//  PUBLICAÇÃO MQTT
// ════════════════════════════════════════════════════════════════════════════════
void publicarMQTT() {
  StaticJsonDocument<192> doc;
  char buf[192];

  // Tópico 1: temperatura
  doc.clear();
  doc["valor"]      = temperatura;
  doc["umidade_ar"] = umidadeAr;
  serializeJson(doc, buf);
  mqtt.publish(T_TEMP, buf);

  // Tópico 2: solo
  doc.clear();
  doc["umidade_solo"] = umidadeSolo;
  doc["luminosidade"] = luminosidade;
  serializeJson(doc, buf);
  mqtt.publish(T_SOLO, buf);

  // Tópico 3: status consolidado
  doc.clear();
  doc["status"] = statusAtual;
  doc["temp"]   = temperatura;
  doc["solo"]   = umidadeSolo;
  doc["ar"]     = umidadeAr;
  serializeJson(doc, buf);
  mqtt.publish(T_STATUS, buf);

  // Tópico 4: alerta (só se ativo)
  if (alertaAtivo) {
    doc.clear();
    doc["alerta"]   = true;
    doc["motivo"]   = motivoAlerta;
    doc["talhao"]   = "Sul";
    doc["produtor"] = "Joao Batista";
    serializeJson(doc, buf);
    mqtt.publish(T_ALERTA, buf);
  }

  // Tópico 5: simulacao (só se algum botão pressionado)
  if (modoSeca || modoGeada) {
    doc.clear();
    doc["modo"]  = modoSeca ? "SECA" : "GEADA";
    doc["ativo"] = true;
    serializeJson(doc, buf);
    mqtt.publish(T_SIM, buf);
  }

  Serial.printf("[MQTT] Temp:%.1f°C Solo:%d%% Status:%s Alerta:%s\n",
    temperatura, umidadeSolo, statusAtual.c_str(),
    alertaAtivo ? motivoAlerta.c_str() : "nao");
}

// ════════════════════════════════════════════════════════════════════════════════
//  CONEXÕES
// ════════════════════════════════════════════════════════════════════════════════
void conectarWiFi() {
  Serial.print("Conectando WiFi");
  WiFi.mode(WIFI_STA);
  WiFi.begin(SSID, PASSWORD);
  int t = 0;
  while (WiFi.status() != WL_CONNECTED && t < 40) { // até 20s
    delay(500); Serial.print("."); t++;
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println(" OK! IP: " + WiFi.localIP().toString());
  } else {
    Serial.println(" FALHOU — modo offline");
  }
}

void conectarMQTT() {
  mqtt.setBufferSize(512); // garante espaço para JSONs maiores
  int tentativas = 0;
  while (!mqtt.connected() && tentativas < 5) {
    Serial.print("Conectando MQTT...");
    if (mqtt.connect(mqttClientId.c_str())) {
      Serial.println(" OK — ID: " + mqttClientId);
      mqtt.publish(T_STATUS, "{\"status\":\"online\",\"dispositivo\":\"AgroVista-v2\"}");
    } else {
      Serial.printf(" falhou rc=%d — tentando em 3s\n", mqtt.state());
      delay(3000);
      tentativas++;
    }
  }
}

// ════════════════════════════════════════════════════════════════════════════════
//  SETUP
// ════════════════════════════════════════════════════════════════════════════════
void setup() {
  Serial.begin(115200);
  Serial.println("\n╔══ AgroVista IoT v2.0 ══╗");

  // Pinos de saída
  pinMode(LED_R,     OUTPUT);
  pinMode(LED_G,     OUTPUT);
  pinMode(LED_B,     OUTPUT);
  pinMode(BUZZER_PIN,OUTPUT);

  // Pinos de entrada
  pinMode(BTN_SECA,  INPUT_PULLUP);
  pinMode(BTN_GEADA, INPUT_PULLUP);

  setLED(LOW,  LOW,  HIGH); // azul = inicializando

  // LCD
  Wire.begin();
  lcd.init();
  lcd.backlight();
  lcd.setCursor(0, 0); lcd.print("AgroVista v2.0");
  lcd.setCursor(0, 1); lcd.print("Iniciando...");

  // Sensores
  dht.begin();

  // Rede
  conectarWiFi();
  mqtt.setServer(MQTT_SERVER, MQTT_PORT);
  conectarMQTT();

  // LCD — mostra IP
  lcd.clear();
  lcd.setCursor(0, 0); lcd.print("WiFi OK!");
  lcd.setCursor(0, 1);
  if (WiFi.status() == WL_CONNECTED)
    lcd.print(WiFi.localIP().toString());
  else
    lcd.print("Sem rede");
  delay(2000);

  setLED(LOW,  HIGH, LOW ); // verde = pronto
  bipBuzzer(2, 100); // 2 bips = sistema pronto

  Serial.println("Sistema pronto.");
}

// ════════════════════════════════════════════════════════════════════════════════
//  LOOP
// ════════════════════════════════════════════════════════════════════════════════
void loop() {
  // Mantém MQTT vivo
  if (!mqtt.connected()) conectarMQTT();
  mqtt.loop();

  // Lê sensores + atualiza LED continuamente
  lerSensores();
  atualizarLCD();

  // Publica a cada 4 segundos
  if (millis() - ultimoEnvio >= 4000) {
    ultimoEnvio = millis();
    if (mqtt.connected()) publicarMQTT();
  }

  delay(100);
}
