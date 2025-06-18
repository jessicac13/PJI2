#include <Wire.h>
#include <Adafruit_ADS1X15.h>
#include "DHT.h"
#include "RTClib.h"
#include <SPI.h>
#include <SD.h>
#include <WiFi.h>
#include <PubSubClient.h>

// === Definições ===
#define DHTPIN 4
#define DHTTYPE DHT22
#define LED_LUZ 25
#define LED_AR 26
#define SD_CS 5
#define limiarLuminosidade 1.1
#define temperaturaConforto 25.0
#define umidadeConforto 40.0
#define pinoRele 13

// === Objetos ===
DHT dht(DHTPIN, DHTTYPE);
RTC_DS3231 rtc;
Adafruit_ADS1115 ads;
WiFiClient espClient;
PubSubClient client(espClient);


// === Wi-Fi e MQTT ===
const char* ssid = "AndroidAP";
const char* password = "ravi010219";
const char* mqtt_server = "broker.hivemq.com";
const int mqtt_port = 1883;
const char* mqtt_sd_semana_topic = "projeto/sddata/semana";
const char* mqtt_sd_final_topic = "projeto/sddata/final";
const char* mqtt_sensor_topic = "projeto/sensores";

// === Outros ===
String dias[7] = {"domingo", "segunda", "terça", "quarta", "quinta", "sexta", "sábado"};

void setup_wifi() 
{
  Serial.println("Conectando Wi-Fi...");
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) 
  {
    delay(1000); Serial.print(".");
  }
  Serial.println("Wi-Fi conectado");
}

void reconnect() 
{
  while (!client.connected()) 
  {
    String clientId = "ESP32Client-";
    clientId += String(random(0xffff), HEX);  // Garante ID único
    if (client.connect(clientId.c_str())) 
    {
      Serial.println("MQTT conectado");

      // Reinscreve nos tópicos após reconexão
      client.subscribe("projeto/sddata/semanaajustada");
      client.subscribe("projeto/sddata/finalajustada");
    } 
    else 
    {
      Serial.print("Falha na conexão MQTT. rc=");
      Serial.print(client.state());
      Serial.println(" Tentando novamente em 2s...");
      delay(2000);
    }
  }
}


void mqttCallback(char* topic, byte* payload, unsigned int length) 
{
  String mensagem = "";
  for (unsigned int i = 0; i < length; i++) 
  {
    mensagem += (char)payload[i];
  }

  Serial.print("Mensagem recebida no tópico: ");
  Serial.println(topic);

  const char* nomeArquivo = nullptr;
  const char* topicoPublicar = nullptr;

  // Determina qual arquivo será atualizado
  if (strcmp(topic, "projeto/sddata/semanaajustada") == 0) 
  {
    nomeArquivo = "/semana.txt";
    topicoPublicar = mqtt_sd_semana_topic;
  } else if (strcmp(topic, "projeto/sddata/finalajustada") == 0) 
  {
    nomeArquivo = "/final.txt";
    topicoPublicar = mqtt_sd_final_topic;
  }

  // Atualiza o arquivo apenas se o tópico for válido
  if (nomeArquivo && topicoPublicar) 
  {
    File arquivo = SD.open(nomeArquivo, FILE_WRITE);
    if (!arquivo) {
      Serial.println("Erro ao abrir arquivo para escrita.");
      return;
    }

    // Limpa o conteúdo anterior antes de escrever
    arquivo.print(mensagem);
    arquivo.close();
    Serial.println("Arquivo sobrescrito com sucesso.");

    // Republica conteúdo atualizado (sem cabeçalho)
    publicarArquivo(nomeArquivo, topicoPublicar);
  }
}

void publicarArquivo(const char* caminho, const char* topico) 
{
  File arquivo = SD.open(caminho);

  if (!arquivo) 
  {
    Serial.print("Erro ao abrir arquivo: ");
    Serial.println(caminho);
    return;
  }

  Serial.print("Lendo TODO conteúdo de ");
  Serial.print(caminho);
  Serial.println("...");

  arquivo.readStringUntil('\n'); // Pula cabeçalho

  String conteudoCompleto = "";

  while (arquivo.available()) 
  {
    String linha = arquivo.readStringUntil('\n');
    linha.trim();
    if (linha.length() == 0) continue;

    conteudoCompleto += linha + "\n";
  }

  arquivo.close();

  Serial.print("Publicando conteúdo no tópico ");
  Serial.println(topico);

  // Tenta publicar até conseguir
  if(!client.publish(topico, conteudoCompleto.c_str(), true)) 
  {
    Serial.println("FALHA na publicação MQTT. Tentando novamente...");
    delay(1000); // espera 1 segundo antes de tentar de novo
  }

  Serial.println("Publicação MQTT COMPLETA OK");
}

void publicarConteudoSD() 
{
  publicarArquivo("/semana.txt", mqtt_sd_semana_topic);
  publicarArquivo("/final.txt", mqtt_sd_final_topic);
}

String horaParaString(int h, int m) 
{
  char buf[6];
  sprintf(buf, "%02d:%02d", h, m);
  return String(buf);
}

void setup() 
{
  Serial.begin(115200);
  Wire.begin();
  dht.begin();
  pinMode(LED_LUZ, OUTPUT);
  pinMode(LED_AR, OUTPUT);
  pinMode(pinoRele, OUTPUT);

  setup_wifi();
  client.setServer(mqtt_server, mqtt_port);
 
  if (!rtc.begin()) 
  {
    Serial.println("Não foi possível encontrar o RTC.");
    while (1);
  }

  ads.begin(0x49);
  SD.begin(SD_CS);

  if (!SD.begin(SD_CS)) 
  {
    Serial.println("Falha ao iniciar cartão SD!");
    while (1);
  }

  client.setCallback(mqttCallback);
  client.subscribe("projeto/sddata/semanaajustada");
  client.subscribe("projeto/sddata/finalajustada");

  publicarConteudoSD();

  rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
  Serial.println("Sistema pronto.");
}

void loop() 
{

  //***************************************************LEITURA SENSORES *************************************************************************

  if (!client.connected()) reconnect();
  client.loop();


  DateTime now = rtc.now();
  String horaAtual = horaParaString(now.hour(), now.minute());
  String diaAtual = dias[now.dayOfTheWeek()];
  float tensao = ads.computeVolts(ads.readADC_SingleEnded(0));
  float t = dht.readTemperature();
  float u = dht.readHumidity();

  Serial.println("\n************************************************************");
  Serial.println("Hora: " + horaAtual + " | Dia: " + diaAtual);
  Serial.printf("Temp: %.1f°C | Umidade: %.1f%% | Luz: %.2fV\n", t, u, tensao);
  Serial.println("************************************************************\n");

  //*********************************************ENVIO PRO BROKER DOS VALORES DOS SENSORES************************************************************

  // === Enviar dados sensores via MQTT às 8h, 12h, 14h, 18h ===
  if (horaAtual == "08:00" || horaAtual == "12:00" || horaAtual == "14:00" || horaAtual == "18:00") 
  {
    String payload = "{\"hora\":\"" + horaAtual + "\",\"temp\":" + t + ",\"umid\":" + u + ",\"luz\":" + tensao + "}";
    if(!client.publish(mqtt_sensor_topic, payload.c_str(), true)) 
    {
      Serial.println("FALHA na publicação MQTT (SENSORES). Tentando novamente...");
      delay(1000); // espera 1 segundo antes de tentar de novo
    }
    Serial.println("Publicação MQTT (SENSORES) COMPLETA OK");

  }

  //*******************************************LEITURA SD*************************************************************************************************

  // === Nome do arquivo (semana ou fim de semana) ===
  String nomeArquivo = (now.dayOfTheWeek() == 0 || now.dayOfTheWeek() == 6) ? "/final.txt" : "/semana.txt";
  File arquivo = SD.open(nomeArquivo);
  if (!arquivo) 
  {
    Serial.println("Erro ao abrir arquivo.");
    delay(10000);
    return;
  }

  arquivo.readStringUntil('\n'); // pula o cabeçalho
  while (arquivo.available()) 
  {
    String linha = arquivo.readStringUntil('\n');
    linha.trim();
    if (linha.length() == 0) continue;

    // Processar horário
    String dados[5];
    int i = 0;
    while (linha.length() > 0 && i < 5) 
    {
      int idx = linha.indexOf(',');
      if (idx == -1) 
      {
        dados[i++] = linha;
        break;
      }
      dados[i++] = linha.substring(0, idx);
      linha = linha.substring(idx + 1);
    }

    for (; i < 5; i++) dados[i] = "";

    //*******************************************************************LÓGICA LEDS DE ACORDO COM O SD***********************************************************************
    // === Lógica dos LEDs ===
    if (horaAtual == dados[1])
    {
      if (tensao > limiarLuminosidade) 
      {
        digitalWrite(LED_LUZ, HIGH);
        digitalWrite(pinoRele, HIGH);
        Serial.println("Luz ligada");
      } 
      else 
      {
        Serial.println("Ignorado: já claro");
      }
    }

    if (horaAtual == dados[2]) 
    {
      if (t > temperaturaConforto && u >= umidadeConforto) 
      {
        digitalWrite(LED_AR, HIGH);
        Serial.println("Ar ligado");
      } 
      else 
      {
        Serial.println("Ignorado: conforto não atingido");
      }
    }

    if (horaAtual == dados[3]) 
    {
      if (tensao < limiarLuminosidade) 
      {
        digitalWrite(LED_LUZ, LOW);
        digitalWrite(pinoRele, LOW);
        Serial.println("Luz desligada");
      } 
      else 
      {
        Serial.println("Ignorado: ambiente escuro");
      }
    }

    if (horaAtual == dados[4]) 
    {
      digitalWrite(LED_AR, LOW);
      Serial.println("Ar desligado");
    }
  }

  arquivo.close();
  delay(1000);
}