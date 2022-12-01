/*
 * Atividade Final IT012
 * DOIT ESP32 DEVKIT V1
 *
 * Implementação da Atividade 2 proposta como trabalho final do curso.
 * 
 *  Aluno: Eloilton da Silva
 *  e-mail: eloilton.silva@pg.inatel.br
 */

/*******************************************************************************
    Inclusões
*******************************************************************************/
#include "DHT.h"
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_Sensor.h>
#include <Arduino.h>
#include <WiFi.h>
#include "LittleFS.h"
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <Fonts/FreeSerif9pt7b.h>
extern "C" {
#include "freertos/FreeRTOS.h"
#include "freertos/timers.h"
}
#include <ArduinoJson.h>
#include <AsyncMqttClient.h>
#include "ThingSpeak.h" 

/*******************************************************************************
    Definições de constantes e variáveis globais
*******************************************************************************/
const char *WIFI_SSID = "it012-accesspoint";
const char *WIFI_PASSWORD = "12345678";
WiFiClient client;

// Configurações para acesso à um canal predefinido da ThingSpeak
unsigned long THINGSPEAK_CHANNEL_ID = 1948099;
const char *THINGSPEAK_WRITE_API_KEY = "UN764MVSPYBZPK5L";
const char *THINGSPEAK_READ_API_KEY = "W4R7BJ1K3BKOD5U3";

// Sensor DHT11
#define DHT_READ (15) // pino de leitura do sensor
#define DHT_TYPE DHT11 // tipo de sensor utilizado pela lib DHT
DHT dht(DHT_READ, DHT_TYPE); // Objeto de controle do DHT11
float g_temperatura;
float g_umidade;

// Display OLED SSD1306
#define OLED_WIDTH (128) // largura do display OLED (pixels)
#define OLED_HEIGHT (64) // altura do display OLED (pixels)
#define OLED_ADDRESS (0x3C) // endereço I²C do display
static Adafruit_SSD1306 display // objeto de controle do SSD1306
    (OLED_WIDTH, OLED_HEIGHT, &Wire, -1);

// Cria objeto do Webserver na porta 80 (padrão HTTP)
AsyncWebServer server(80);

// Variáveis para armazenar valores obtidos da página HTML
String g_ssid;
String g_password;
String g_disp;
String g_thingspeak_channel;
String g_thingspeak_key;

// Caminhos dos arquivos criados durante a execução do programa,
// para salvar os valores das credenciais da Wifi
const char *g_ssidPath = "/ssid.txt";
const char *g_passwordPath = "/password.txt";
const char *g_dispPath = "/disp.txt";
const char *g_thingspeak_channelPath = "/channel.txt";
const char *g_thingspeak_keyPath = "/key.txt";

// Temporização - intervalo de espera por conexão Wifi
unsigned long g_previousMillis = 0;
const long g_interval = 10000;

/*******************************************************************************
    Implementação: Funções auxiliares
*******************************************************************************/
void littlefsInit()
{
  if (!LittleFS.begin(true))
  {
    Serial.println("Erro ao montar o sistema de arquivos LittleFS");
    return;
  }
  Serial.println("Sistema de arquivos LittleFS montado com sucesso.");
}

esp_err_t updateChannel()
{
    // Envia dados à plataforma ThingSpeak. Cada dado dos sensores é setado em um campo (field) distinto.
    int errorCode;
    ThingSpeak.setField(1, g_temperatura);
    ThingSpeak.setField(2, g_umidade);

    //errorCode = ThingSpeak.writeFields((long)THINGSPEAK_CHANNEL_ID, THINGSPEAK_WRITE_API_KEY);
    errorCode = ThingSpeak.writeFields((long)g_thingspeak_channel.c_str(), g_thingspeak_key.c_str());
    if (errorCode != 200)
    {
        Serial.println("Erro ao atualizar os canais - código HTTP: " + String(errorCode));
        return ESP_FAIL;
    } else {
        Serial.println("Dados publicados no ThingSpeak.");
    }


    // Leitura falhou; apenas retorna
    return ESP_OK;
}

// Lê arquivos com o LittleFS
String readFile(const char *path)
{
  Serial.printf("Lendo arquivo: %s\r\n", path);

  File file = LittleFS.open(path);
  if (!file || file.isDirectory())
  {
    Serial.printf("\r\nfalha ao abrir o arquivo... %s", path);
    return String();
  }

  String fileContent;
  while (file.available())
  {
    fileContent = file.readStringUntil('\n');
    break;
  }
  return fileContent;
}

// Escreve arquivos com o LittleFS
void writeFile(const char *path, const char *message)
{
  Serial.printf("Escrevendo arquivo: %s\r\n", path);

  File file = LittleFS.open(path, FILE_WRITE);
  if (!file)
  {
    Serial.printf("\r\nfalha ao abrir o arquivo... %s", path);
    return;
  }
  if (file.print(message))
  {
    Serial.printf("\r\narquivo %s editado.", path);
  }
  else
  {
    Serial.printf("\r\nescrita no arquivo %s falhou... ", path);
  }
}

// Callbacks para requisições de recursos do servidor
void serverOnGetRoot(AsyncWebServerRequest *request)
{
  request->send(LittleFS, "/index.html", "text/html");
}

void serverOnGetStyle(AsyncWebServerRequest *request)
{
  request->send(LittleFS, "/style.css", "text/css");
}

void serverOnGetFavicon(AsyncWebServerRequest *request)
{
  request->send(LittleFS, "/favicon.png", "image/png");
}

void serverOnPost(AsyncWebServerRequest *request)
{
  int params = request->params();

  for (int i = 0; i < params; i++)
  {
    AsyncWebParameter *p = request->getParam(i);
    if (p->isPost())
    {
      if (p->name() == "ssid")
      {
        g_ssid = p->value().c_str();
        Serial.print("SSID definido como ");
        Serial.println(g_ssid);

        // Escreve WIFI_SSID no arquivo
        writeFile(g_ssidPath, g_ssid.c_str());
      }
      if (p->name() == "password")
      {
        g_password = p->value().c_str();
        Serial.print("Senha definida como ");
        Serial.println(g_password);

        // Escreve Nome do Display no arquivo
        writeFile(g_passwordPath, g_password.c_str());
      }
      if (p->name() == "disp")
      {
        g_disp = p->value().c_str();
        Serial.print("Nome no display definido como ");
        Serial.println(g_disp);

        // Escreve Nome do Display no arquivo
        writeFile(g_dispPath, g_disp.c_str());
      }
      if (p->name() == "g_thingspeak_channel")
      {
        g_thingspeak_channel = p->value().c_str();
        Serial.print("Canal definido como ");
        Serial.println(g_thingspeak_channel);

        // Escreve Canal no arquivo
        writeFile(g_thingspeak_channelPath, g_thingspeak_channel.c_str());
      }
      if (p->name() == "g_thingspeak_key")
      {
        g_thingspeak_key = p->value().c_str();
        Serial.print("Key definida como ");
        Serial.println(g_thingspeak_key);

        // Escreve a Key no arquivo
        writeFile(g_thingspeak_keyPath, g_thingspeak_key.c_str());
      }
    }
  }

  // Após escrever no arquivo, envia mensagem de texto simples ao browser
  request->send(200, "text/plain", "Finalizado - o ESP32 vai reiniciar e se conectar ao seu AP definido.");

  // Reinicia o ESP32
  delay(2000);
  ESP.restart();
}

// Inicializa a conexão Wifi
bool initWiFi()
{
  // Se o valor de g_ssid for não-nulo, uma rede Wifi foi provida pela página do
  // servidor. Se for, o ESP32 iniciará em modo AP.
  if (g_ssid == "")
  {
    Serial.println("SSID indefinido (ainda não foi escrito no arquivo, ou a leitura falhou).");
    return false;
  }

  // Se há um SSID e PASSWORD salvos, conecta-se à esta rede.
  WiFi.mode(WIFI_STA);
  WiFi.begin(g_ssid.c_str(), g_password.c_str());
  Serial.println("Conectando à Wifi...");

  unsigned long currentMillis = millis();
  g_previousMillis = currentMillis;

  while (WiFi.status() != WL_CONNECTED)
  {
    currentMillis = millis();
    if (currentMillis - g_previousMillis >= g_interval)
    {
      Serial.println("Falha em conectar.");
      return false;
    }
  }

  // Exibe o endereço IP local obtido
  Serial.println(WiFi.localIP());

  // ThingSpeak

  ThingSpeak.begin(client);

  return true;
}

/*******************************************************************************
    Implementação: setup & loop
*******************************************************************************/
void setup()
{
  // Log inicial da placa
  Serial.begin(115200);
  Serial.print("\r\n --- exemplo_webserver_provisioning --- \n");

  // Inicia o sistema de arquivos
  littlefsInit();

  // Inicializa o sensor DHT11
  dht.begin();

  // Inicializa o display OLED SSD1306
  display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDRESS);
  display.setTextColor(WHITE);

  // Configura LED_BUILTIN (GPIO2) como pino de saída
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, LOW);

  // Carrega os valores lidos com o LittleFS
  g_ssid = readFile(g_ssidPath);
  g_password = readFile(g_passwordPath);
  g_disp = readFile(g_dispPath);
  g_thingspeak_channel = readFile(g_thingspeak_channelPath);
  g_thingspeak_key = readFile(g_thingspeak_keyPath);
  Serial.println(g_ssid);
  Serial.println(g_password);
  Serial.println(g_disp);
  Serial.println(g_thingspeak_channel);
  Serial.println(g_thingspeak_key);

  if (!initWiFi())
  {
    // Seta o ESP32 para o modo AP
    WiFi.mode(WIFI_AP);
    WiFi.softAP(WIFI_SSID, WIFI_PASSWORD);

    Serial.print("Access Point criado com endereço IP ");
    Serial.println(WiFi.softAPIP());

    // Callbacks da página principal do servidor de provisioning
    server.on("/", HTTP_GET, serverOnGetRoot);
    server.on("/style.css", HTTP_GET, serverOnGetStyle);
    server.on("/favicon.png", HTTP_GET, serverOnGetFavicon);

    // Ao clicar no botão "Enviar" para enviar as credenciais, o servidor receberá uma
    // requisição do tipo POST, tratada a seguir
    server.on("/", HTTP_POST, serverOnPost);

    // Como ainda não há credenciais para acessar a rede wifi,
    // Inicia o Webserver em modo AP
    server.begin();
  } 
}

void loop()
{

  unsigned long currentMillis = millis();

  if (WiFi.status() == WL_CONNECTED && WiFi.getMode() == WIFI_MODE_STA)
  {

        digitalWrite(LED_BUILTIN, HIGH);
        delay(100);
        digitalWrite(LED_BUILTIN, LOW);
        delay(900);

        // Atraso entre medidas do DHT11
        delay(2000);

        float umidade = dht.readHumidity();
        float temperatura = dht.readTemperature();

        // Verifica se alguma leitura falhou
        if (isnan(umidade) || isnan(temperatura)) {
            Serial.println("Erro - leitura inválida...");
            return;
        }

        // Log da temperatura e umidade pela serial
        Serial.printf("[DHT11] ");
        Serial.printf("umidade: %0.2f ", umidade);
        Serial.printf("temperatura: %0.2f °C\r\n", temperatura);

        // Limpa a tela do display e mostra o nome do exemplo
        display.clearDisplay();
        display.setCursor(0, 0);
        display.print(g_disp);

        // Mostra temperatura no display OLED
        display.drawRoundRect(0, 16, 72, 40, 6, WHITE);
        display.setCursor(4, 20);
        display.printf("Temperatura");
        display.setCursor(4, 40);
        display.setFont(&FreeSerif9pt7b);
        display.printf("%0.1f", temperatura);
        display.printf(" C");
        display.setFont();

        g_temperatura = temperatura;

        // Mostra umidade no display OLED
        display.drawRoundRect(74, 16, 54, 40, 6, WHITE);
        display.setCursor(80, 20);
        display.printf("Umidade");
        display.setCursor(78, 40);
        display.setFont(&FreeSerif9pt7b);
        display.printf("%0.1f", umidade);
        display.printf("%%");
        display.setFont();

        g_umidade = umidade;

        // Atualiza tela do display OLED
        display.display();

        if ((currentMillis - g_previousMillis >= 30000) && (WiFi.status() == WL_CONNECTED))
        {
            g_previousMillis = currentMillis;
            updateChannel();
        }
      
  }
  else
  {
    digitalWrite(LED_BUILTIN, HIGH);
    delay(1000);
    digitalWrite(LED_BUILTIN, LOW);
    delay(1000);
  }
}