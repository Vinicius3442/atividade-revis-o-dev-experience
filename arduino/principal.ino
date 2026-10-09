#include <WiFiS3.h>
#include <DHT.h>
#include <Arduino_LED_Matrix.h>
#include "arduino_secrets.h"

// --- MAPEAMENTO DE PINOS ---
#define PIN_RELE    9  // Ventilação
#define PIN_BUZZER  8  // Buzzer
#define PIN_LED_VER 7  // LED Vermelho
#define PIN_LED_AMA 6  // LED Amarelo
#define PIN_LED_VDE 5  // LED Verde
#define PIN_BOTAO   4  // Botão de reconhecimento
#define PIN_DHT     3  // Sensor DHT11
#define PIN_JANELA  2  // Sensor Reed Switch / Tilt (Janela)
#define PIN_LDR     A0 // Sensor de Luz

// --- CONFIGURAÇÃO DO DHT ---
#define DHTTYPE DHT11
DHT dht(PIN_DHT, DHTTYPE);

// --- MATRIZ DE LED NATIVA DO UNO R4 ---
ArduinoLEDMatrix matrix;

// Padrões das letras 8x12 para a Matriz de LED
const uint32_t iconN[] = { 0x81c1e1, 0xf1d1b1, 0x918181, 0x0 }; // Letra N (NORMAL)
const uint32_t iconA[] = { 0x183c66, 0x667e66, 0x666600, 0x0 }; // Letra A (ATENCAO)
const uint32_t iconC[] = { 0x3c66c0, 0xc0c0c0, 0x663c00, 0x0 }; // Letra C (CRITICO)
const uint32_t iconR[] = { 0x7c6666, 0x7c6c66, 0x666600, 0x0 }; // Letra R (RECONHECIDO)
const uint32_t iconF[] = { 0x7e6060, 0x7c6060, 0x606000, 0x0 }; // Letra F (FALHA_SENSOR)

// --- MÁQUINA DE ESTADOS ---
enum EstadoEstufa {
  NORMAL,
  ATENCAO,
  CRITICO,
  RECONHECIDO,
  FALHA_SENSOR
};

EstadoEstufa estadoAtual = NORMAL;
EstadoEstufa estadoAnterior = NORMAL;

// --- ESTRUTURA PARA BUFFER OFFLINE (ATÉ 20 LEITURAS) ---
struct Telemetria {
  unsigned long seq;
  float temp;
  float umid;
  bool janela;
  int luz_pct;
  int estado;
};

#define BUFFER_SIZE 20
Telemetria bufferOffline[BUFFER_SIZE];
int bufferHead = 0;
int bufferTail = 0;
int bufferCount = 0;

// --- VARIÁVEIS DE CONTROLE E TEMPO ---
const float TEMP_MIN_IDEAL = 20.0;
const float TEMP_MAX_IDEAL = 28.0;

int falhasDHT = 0;
unsigned long tempoJanelaAbertaInicio = 0;
bool janelaEstavaAberta = false;

unsigned long tempoTempForaFaixaInicio = 0;
bool tempEstavaForaFaixa = false;

unsigned long seqTelemetria = 0;

// Timers não bloqueantes (millis)
unsigned long lastLeituraMillis = 0;
unsigned long lastBuzzerBipeMillis = 0;
unsigned long lastWifiCheckMillis = 0;

const unsigned long INTERVALO_LEITURA = 5000; // Leituras a cada 5 segundos

// Wi-Fi Client
WiFiClient client;
int statusWifi = WL_IDLE_STATUS;

// --- PROTÓTIPOS DE FUNÇÕES ---
void lerSensores(float &t, float &u, int &luzPct, bool &janelaAberta);
void atualizarMaquinaEstados(float t, int luzPct, bool janelaAberta, bool leituraInvalida);
void aplicarAtuacao();
void atualizarMatrizLED();
void enviarTelemetria(float t, float u, int luzPct, bool janelaAberta);
void salvarNoBuffer(float t, float u, int luzPct, bool janelaAberta);
void reenviarBuffer();
void conectarWiFi();

void setup() {
  Serial.begin(115200);
  
  // Configuração dos pinos de saída
  pinMode(PIN_LED_VDE, OUTPUT);
  pinMode(PIN_LED_AMA, OUTPUT);
  pinMode(PIN_LED_VER, OUTPUT);
  pinMode(PIN_BUZZER, OUTPUT);
  pinMode(PIN_RELE, OUTPUT);

  // Configuração dos pinos de entrada
  pinMode(PIN_BOTAO, INPUT_PULLUP);
  pinMode(PIN_JANELA, INPUT_PULLUP);

  // Garantir que atuadores comecem desligados
  digitalWrite(PIN_RELE, LOW);
  digitalWrite(PIN_BUZZER, LOW);

  // Inicializa DHT e Matriz de LED
  dht.begin();
  matrix.begin();

  // Conexão Inicial ao Wi-Fi
  conectarWiFi();
  
  atualizarMatrizLED();
}

void loop() {
  unsigned long currentMillis = millis();

  // --- TRATAMENTO DO BOTÃO DE RECONHECIMENTO ---
  if (digitalRead(PIN_BOTAO) == LOW) {
    if (estadoAtual == CRITICO) {
      estadoAtual = RECONHECIDO;
      digitalWrite(PIN_BUZZER, LOW); // Silencia o alarme imediatamente
      atualizarMatrizLED();
    }
  }

  // --- CICLO PRINCIPAL DE LEITURA E LÓGICA (A CADA 5 SEGUNDOS) ---
  if (currentMillis - lastLeituraMillis >= INTERVALO_LEITURA) {
    lastLeituraMillis = currentMillis;

    float t, u;
    int luzPct;
    bool janelaAberta;
    
    lerSensores(t, u, luzPct, janelaAberta);
    
    bool leituraInvalida = isnan(t) || isnan(u);
    
    if (leituraInvalida) {
      falhasDHT++;
    } else {
      falhasDHT = 0;
    }

    atualizarMaquinaEstados(t, luzPct, janelaAberta, leituraInvalida);
    aplicarAtuacao();
    atualizarMatrizLED();

    // Tenta enviar a telemetria ou guarda no buffer offline
    enviarTelemetria(t, u, luzPct, janelaAberta);
  }

  // --- BIPE DO BUZZER NO ESTADO ATENÇÃO (A CADA 10 SEGUNDOS) ---
  if (estadoAtual == ATENCAO) {
    if (currentMillis - lastBuzzerBipeMillis >= 10000) {
      lastBuzzerBipeMillis = currentMillis;
      tone(PIN_BUZZER, 1000, 150); // Bipe rápido sem usar delay
    }
  }

  // --- VERIFICAÇÃO PERIÓDICA DA CONEXÃO WI-FI ---
  if (currentMillis - lastWifiCheckMillis >= 15000) {
    lastWifiCheckMillis = currentMillis;
    if (WiFi.status() != WL_CONNECTED) {
      conectarWiFi();
    } else if (bufferCount > 0) {
      reenviarBuffer(); // Tenta esvaziar o buffer se a conexão voltou
    }
  }
}

// --- FUNÇÃO PARA LER OS SENSORES ---
void lerSensores(float &t, float &u, int &luzPct, bool &janelaAberta) {
  u = dht.readHumidity();
  t = dht.readTemperature();

  // Janela: pino LOW indica contato fechado
  janelaAberta = (digitalRead(PIN_JANELA) == HIGH);

  // LDR: Normalizado em 0 - 100%
  int rawLdr = analogRead(PIN_LDR);
  luzPct = map(rawLdr, 0, 1023, 0, 100);
  luzPct = constrain(luzPct, 0, 100);
}

// --- MÁQUINA DE ESTADOS DO SISTEMA ---
void atualizarMaquinaEstados(float t, int luzPct, bool janelaAberta, bool leituraInvalida) {
  unsigned long agora = millis();

  // 1. Condição de Falha de Sensor (3 leituras inválidas seguidas)
  if (falhasDHT >= 3) {
    estadoAtual = FALHA_SENSOR;
    return;
  }

  // Controle de tempo com a Janela Aberta
  if (janelaAberta) {
    if (!janelaEstavaAberta) {
      tempoJanelaAbertaInicio = agora;
      janelaEstavaAberta = true;
    }
  } else {
    janelaEstavaAberta = false;
  }
  unsigned long tempoJanelaAberta = janelaEstavaAberta ? (agora - tempoJanelaAbertaInicio) : 0;

  // Controle de tempo da Temperatura fora da faixa ideal (20°C - 28°C)
  bool tempForaFaixa = (t < TEMP_MIN_IDEAL || t > TEMP_MAX_IDEAL);
  if (tempForaFaixa) {
    if (!tempEstavaForaFaixa) {
      tempoTempForaFaixaInicio = agora;
      tempEstavaForaFaixa = true;
    }
  } else {
    tempEstavaForaFaixa = false;
  }
  unsigned long tempoTempForaFaixa = tempEstavaForaFaixa ? (agora - tempoTempForaFaixaInicio) : 0;

  // 2. Transição para Estado CRÍTICO
  if (tempoTempForaFaixa > 30000 || tempoJanelaAberta > 60000) {
    if (estadoAtual != RECONHECIDO) {
      estadoAtual = CRITICO;
    }
    return;
  }

  // 3. Estado RECONHECIDO: mantêm até normalizar
  if (estadoAtual == RECONHECIDO) {
    if (!tempForaFaixa && !janelaAberta) {
      estadoAtual = NORMAL;
    }
    return;
  }

  // 4. Transição para Estado ATENÇÃO
  bool pertodoLimite = (t >= 27.0 && t <= 28.0) || (t >= 20.0 && t <= 21.0);
  bool luzMuitoAlta = (luzPct > 80);

  if (pertodoLimite || tempoJanelaAberta > 20000 || luzMuitoAlta) {
    estadoAtual = ATENCAO;
    return;
  }

  // 5. Estado NORMAL
  if (!tempForaFaixa && !janelaAberta) {
    estadoAtual = NORMAL;
  }
}

// --- ATUAÇÃO (LEDS, BUZZER E RELÉ DE VENTILAÇÃO) ---
void aplicarAtuacao() {
  switch (estadoAtual) {
    case NORMAL:
      digitalWrite(PIN_LED_VDE, HIGH);
      digitalWrite(PIN_LED_AMA, LOW);
      digitalWrite(PIN_LED_VER, LOW);
      digitalWrite(PIN_BUZZER, LOW);
      digitalWrite(PIN_RELE, LOW); // Ventilação desligada
      break;

    case ATENCAO:
      digitalWrite(PIN_LED_VDE, LOW);
      digitalWrite(PIN_LED_AMA, HIGH);
      digitalWrite(PIN_LED_VER, LOW);
      digitalWrite(PIN_RELE, LOW);
      break;

    case CRITICO:
      digitalWrite(PIN_LED_VDE, LOW);
      digitalWrite(PIN_LED_AMA, LOW);
      digitalWrite(PIN_LED_VER, HIGH);
      digitalWrite(PIN_BUZZER, HIGH); // Alarme sonoro contínuo
      digitalWrite(PIN_RELE, HIGH);  // Liga a ventilação automaticamente
      break;

    case RECONHECIDO:
      digitalWrite(PIN_LED_VDE, LOW);
      digitalWrite(PIN_LED_AMA, LOW);
      digitalWrite(PIN_LED_VER, HIGH);
      digitalWrite(PIN_BUZZER, LOW); // Silenciado
      digitalWrite(PIN_RELE, HIGH);  // Mantém ventilação ligada se necessário
      break;

    case FALHA_SENSOR:
      digitalWrite(PIN_LED_VDE, LOW);
      digitalWrite(PIN_LED_AMA, HIGH); // Amarelo + Vermelho juntos
      digitalWrite(PIN_LED_VER, HIGH);
      digitalWrite(PIN_BUZZER, LOW);
      digitalWrite(PIN_RELE, LOW);
      break;
  }
}

// --- ATUALIZA A MATRIZ DE LED (12x8 NATIVA) ---
void atualizarMatrizLED() {
  switch (estadoAtual) {
    case NORMAL:       matrix.loadFrame(iconN); break;
    case ATENCAO:      matrix.loadFrame(iconA); break;
    case CRITICO:      matrix.loadFrame(iconC); break;
    case RECONHECIDO:  matrix.loadFrame(iconR); break;
    case FALHA_SENSOR: matrix.loadFrame(iconF); break;
  }
}

// --- COMUNICAÇÃO HTTP / ENVIO DE TELEMETRIA EM JSON ---
void enviarTelemetria(float t, float u, int luzPct, bool janelaAberta) {
  seqTelemetria++;

  if (WiFi.status() != WL_CONNECTED) {
    salvarNoBuffer(t, u, luzPct, janelaAberta);
    return;
  }

  if (client.connect(SERVER_HOST, SERVER_PORT)) {
    String jsonPayload = "{";
    jsonPayload += "\"device_id\":\"ESTUFA_SALA01\",";
    jsonPayload += "\"placa\":\"UNO R4 WiFi\",";
    jsonPayload += "\"seq\":" + String(seqTelemetria) + ",";
    jsonPayload += "\"temp\":" + String(t, 1) + ",";
    jsonPayload += "\"umid\":" + String(u, 1) + ",";
    jsonPayload += "\"janela\":" + String(janelaAberta ? "true" : "false") + ",";
    jsonPayload += "\"luz_pct\":" + String(luzPct) + ",";
    jsonPayload += "\"estado\":" + String(estadoAtual);
    jsonPayload += "}";

    client.println("POST /api/telemetria HTTP/1.1");
    client.print("Host: "); client.println(SERVER_HOST);
    client.println("Content-Type: application/json");
    client.print("Content-Length: "); client.println(jsonPayload.length());
    client.println("Connection: close");
    client.println();
    client.println(jsonPayload);

    client.stop();
  } else {
    // Se a API não responder, armazena no buffer
    salvarNoBuffer(t, u, luzPct, janelaAberta);
  }
}

// --- GERENCIAMENTO DE BUFFER OFFLINE (SISTEMA FIFO) ---
void salvarNoBuffer(float t, float u, int luzPct, bool janelaAberta) {
  if (bufferCount < BUFFER_SIZE) {
    bufferOffline[bufferHead].seq = seqTelemetria;
    bufferOffline[bufferHead].temp = t;
    bufferOffline[bufferHead].umid = u;
    bufferOffline[bufferHead].janela = janelaAberta;
    bufferOffline[bufferHead].luz_pct = luzPct;
    bufferOffline[bufferHead].estado = estadoAtual;

    bufferHead = (bufferHead + 1) % BUFFER_SIZE;
    bufferCount++;
  }
}

void reenviarBuffer() {
  while (bufferCount > 0 && WiFi.status() == WL_CONNECTED) {
    Telemetria item = bufferOffline[bufferTail];

    if (client.connect(SERVER_HOST, SERVER_PORT)) {
      String jsonPayload = "{";
      jsonPayload += "\"device_id\":\"ESTUFA_SALA01\",";
      jsonPayload += "\"placa\":\"UNO R4 WiFi\",";
      jsonPayload += "\"seq\":" + String(item.seq) + ",";
      jsonPayload += "\"temp\":" + String(item.temp, 1) + ",";
      jsonPayload += "\"umid\":" + String(item.umid, 1) + ",";
      jsonPayload += "\"janela\":" + String(item.janela ? "true" : "false") + ",";
      jsonPayload += "\"luz_pct\":" + String(item.luz_pct) + ",";
      jsonPayload += "\"estado\":" + String(item.estado);
      jsonPayload += "}";

      client.println("POST /api/telemetria HTTP/1.1");
      client.print("Host: "); client.println(SERVER_HOST);
      client.println("Content-Type: application/json");
      client.print("Content-Length: "); client.println(jsonPayload.length());
      client.println("Connection: close");
      client.println();
      client.println(jsonPayload);

      client.stop();

      bufferTail = (bufferTail + 1) % BUFFER_SIZE;
      bufferCount--;
    } else {
      break; // Se falhar novamente, para de desempilhar
    }
    
  }
}

// --- CONEXÃO WI-FI ---
void conectarWiFi() {
  if (WiFi.status() == WL_CONNECTED) return;

  WiFi.begin(SECRET_SSID, SECRET_PASS);
}