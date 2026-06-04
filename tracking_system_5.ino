#include <WiFiS3.h>
#include <TinyGPS.h>
#include <Wire.h>
#include <MPU6050.h>
#include <SPI.h>
#include <MFRC522.h>
#include <math.h>

// RFID
#define SS_PIN 10
#define RST_PIN 8
MFRC522 rfid(SS_PIN, RST_PIN);

// buzzer
#define BUZZER 7

TinyGPS gps;
MPU6050 mpu;

const char* ssid = "Tanuj_wifi";
const char* password = "123123123";

WiFiServer server(80);

// RFID CARDS
String card1 = "131f6128";
String card2 = "b3e4172d";

String activeUser = "NONE";

// person locations
float p1_lat = 0;
float p1_lon = 0;
float p2_lat = 0;
float p2_lon = 0;

// GPS
float lat = 0;
float lon = 0;

// GEOFENCE
double centerLat = 12.841625;
double centerLon = 80.155334;
double radius = 200;

// BUZZER TIMING — 2s ON, 5s OFF interval
unsigned long buzzerLastToggle = 0;
bool buzzerOn = false;

// -------- FALL DETECTION STATE (3-trigger gyro algorithm) --------
boolean fallDetected  = false;
boolean trigger1      = false;
boolean trigger2      = false;
boolean trigger3      = false;
byte    trigger1count = 0;
byte    trigger2count = 0;
byte    trigger3count = 0;
float   magnitude     = 0;

// raw MPU values
int16_t AcX, AcY, AcZ, Tmp, GyX, GyY, GyZ;
float   ax=0, ay=0, az=0, gx=0, gy=0, gz=0;

// UID FUNCTION
String getUID()
{
  String uid = "";
  for (byte i = 0; i < rfid.uid.size; i++)
  {
    if (rfid.uid.uidByte[i] < 0x10) uid += "0";
    uid += String(rfid.uid.uidByte[i], HEX);
  }
  uid.toLowerCase();
  return uid;
}

// BUZZ
void beep()
{
  tone(BUZZER, 3000);
  delay(200);
  noTone(BUZZER);
}

void alertBuzz()
{
  for (int i = 0; i < 3; i++)
  {
    tone(BUZZER, 2000);
    delay(300);
    noTone(BUZZER);
    delay(150);
  }
}

// HAVERSINE
double haversine(double lat1, double lon1, double lat2, double lon2)
{
  const double R = 6371000;
  double dLat = radians(lat2 - lat1);
  double dLon = radians(lon2 - lon1);
  lat1 = radians(lat1);
  lat2 = radians(lat2);
  double a = sin(dLat / 2) * sin(dLat / 2) +
             cos(lat1) * cos(lat2) *
             sin(dLon / 2) * sin(dLon / 2);
  double c = 2 * atan2(sqrt(a), sqrt(1 - a));
  return R * c;
}

// -------- MPU READ --------
void mpu_read()
{
  Wire.beginTransmission(0x68);
  Wire.write(0x3B);
  Wire.endTransmission(false);
  Wire.requestFrom(0x68, 14, true);
  AcX = Wire.read() << 8 | Wire.read();
  AcY = Wire.read() << 8 | Wire.read();
  AcZ = Wire.read() << 8 | Wire.read();
  Tmp = Wire.read() << 8 | Wire.read();
  GyX = Wire.read() << 8 | Wire.read();
  GyY = Wire.read() << 8 | Wire.read();
  GyZ = Wire.read() << 8 | Wire.read();
}

// -------- FALL DETECTION (3-trigger gyro algorithm) --------
void updateFallDetection()
{
  mpu_read();

  ax = (AcX - 2050) / 16384.00;
  ay = (AcY - 77)   / 16384.00;
  az = (AcZ - 1947) / 16384.00;
  gx = (GyX + 270)  / 131.07;
  gy = (GyY - 351)  / 131.07;
  gz = (GyZ + 136)  / 131.07;

  float Raw_Amp = sqrt(ax*ax + ay*ay + az*az);
  magnitude = Raw_Amp;
  int Amp = Raw_Amp * 10;

  if (Amp <= 2 && trigger2 == false)
  {
    trigger1 = true;
    Serial.println("TRIGGER 1 ACTIVATED");
  }

  if (trigger1 == true)
  {
    trigger1count++;
    if (Amp >= 12)
    {
      trigger2      = true;
      trigger1      = false;
      trigger1count = 0;
      Serial.println("TRIGGER 2 ACTIVATED");
    }
  }

  if (trigger2 == true)
  {
    trigger2count++;
    int angleChange = sqrt(gx*gx + gy*gy + gz*gz);
    if (angleChange >= 30 && angleChange <= 400)
    {
      trigger3      = true;
      trigger2      = false;
      trigger2count = 0;
      Serial.println("TRIGGER 3 ACTIVATED");
    }
  }

  if (trigger3 == true)
  {
    trigger3count++;
    if (trigger3count >= 10)
    {
      int angleChange = sqrt(gx*gx + gy*gy + gz*gz);
      if (angleChange >= 0 && angleChange <= 10)
      {
        fallDetected  = true;
        trigger3      = false;
        trigger3count = 0;
        Serial.println("!!! FALL CONFIRMED !!!");
        alertBuzz();
      }
      else
      {
        trigger3      = false;
        trigger3count = 0;
        Serial.println("TRIGGER 3 DEACTIVATED");
      }
    }
  }

  if (trigger2count >= 6)
  {
    trigger2      = false;
    trigger2count = 0;
    Serial.println("TRIGGER 2 DEACTIVATED");
  }
  if (trigger1count >= 6)
  {
    trigger1      = false;
    trigger1count = 0;
    Serial.println("TRIGGER 1 DEACTIVATED");
  }
}


// -------- HTML PAGE --------
void sendWebPage(WiFiClient& client, double dist, bool outside)
{
  String fenceColor   = outside ? "#FF6B6B" : "#51CF66";
  String fenceStatus  = outside ? "Outside fence" : "Inside fence";
  String buzzerColor  = outside ? "#FF6B6B" : "#6B6B78";
  String buzzerStatus = outside ? "ON" : "Off";
  String fallBanner   = "";

  if (fallDetected)
  {
    fallBanner = "<div style='background:#3D1515;border:1px solid #FF6B6B;border-radius:10px;padding:12px 16px;margin-bottom:18px;display:flex;align-items:center;gap:12px;'>"
                 "<svg width='18' height='18' viewBox='0 0 18 18' fill='none'><circle cx='9' cy='9' r='8' stroke='#FF6B6B' stroke-width='1.5'/>"
                 "<rect x='8.25' y='4' width='1.5' height='6' rx='.75' fill='#FF6B6B'/>"
                 "<rect x='8.25' y='11.5' width='1.5' height='1.5' rx='.75' fill='#FF6B6B'/></svg>"
                 "<div><div style='font-size:14px;font-weight:600;color:#FF6B6B;'>Fall detected</div>"
                 "<div style='font-size:12px;color:#FFAAAA;'>Sudden impact registered — check on the user immediately</div></div></div>";
  }

  String p1Bg = (activeUser == "PERSON 1") ? "#1A3A2A" : "#252530";
  String p2Bg = (activeUser == "PERSON 2") ? "#1A2A3A" : "#252530";
  String p1Tc = (activeUser == "PERSON 1") ? "#51CF66" : "#6B6B78";
  String p2Tc = (activeUser == "PERSON 2") ? "#74C0FC" : "#6B6B78";
  String p1Status = (activeUser == "PERSON 1") ? "Active" : "Inactive";
  String p2Status = (activeUser == "PERSON 2") ? "Active" : "Inactive";

  String distStr  = String(dist, 1);
  String magStr   = String(magnitude, 2);
  String latStr   = String(lat, 6);
  String lonStr   = String(lon, 6);
  String p1latStr = String(p1_lat, 6);
  String p1lonStr = String(p1_lon, 6);
  String p2latStr = String(p2_lat, 6);
  String p2lonStr = String(p2_lon, 6);

  client.println("HTTP/1.1 200 OK");
  client.println("Content-type:text/html");
  client.println("Connection: close");
  client.println();

  client.println("<!DOCTYPE html><html><head><meta charset='UTF-8'>");
  client.println("<meta name='viewport' content='width=device-width,initial-scale=1'>");
  client.println("<meta http-equiv='refresh' content='5'>");
  client.println("<title>Tracking System</title>");

  client.println("<style>");
  client.println("*{box-sizing:border-box;margin:0;padding:0;}");
  client.println("body{font-family:'Times New Roman',Times,serif;background:#0F0F17;color:#E0E0DA;padding:16px;font-size:18px;}");
  client.println(".wrap{max-width:700px;margin:0 auto;}");
  client.println(".topbar{display:flex;align-items:center;justify-content:space-between;margin-bottom:20px;padding-bottom:14px;border-bottom:1px solid #252535;}");
  client.println(".topbar-title{font-size:26px;font-weight:600;display:flex;align-items:center;gap:10px;color:#F0F0EA;letter-spacing:-0.01em;}");
  client.println(".live-dot{width:9px;height:9px;border-radius:50%;background:#51CF66;display:inline-block;}");
  client.println(".badge{font-size:11px;padding:5px 13px;border-radius:20px;font-weight:600;letter-spacing:0.02em;}");
  client.println(".badge-ok{background:#1A3A2A;color:#51CF66;border:1px solid #2A5A3A;}");
  client.println(".grid4{display:grid;grid-template-columns:repeat(4,1fr);gap:10px;margin-bottom:14px;}");
  client.println(".grid2{display:grid;grid-template-columns:repeat(2,1fr);gap:10px;margin-bottom:14px;}");
  client.println(".metric{background:#181824;border:1px solid #252535;border-radius:12px;padding:14px 16px;}");
  client.println(".metric-label{font-size:10px;color:#55556A;margin-bottom:6px;letter-spacing:0.08em;text-transform:uppercase;font-weight:600;}");
  client.println(".metric-value{font-size:28px;font-weight:600;line-height:1.2;color:#EEEEE8;}");
  client.println(".metric-sub{font-size:10px;color:#40404E;margin-top:5px;}");
  client.println(".card{background:#181824;border:1px solid #252535;border-radius:12px;padding:16px 18px;margin-bottom:14px;}");
  client.println(".card-title{font-size:10px;font-weight:700;color:#55556A;margin-bottom:14px;letter-spacing:0.1em;text-transform:uppercase;}");
  client.println(".row{display:flex;justify-content:space-between;align-items:center;padding:8px 0;border-bottom:1px solid #1E1E2A;}");
  client.println(".row:last-child{border-bottom:none;}");
  client.println(".row-label{font-size:18px;color:#66667A;}");
  client.println(".row-value{font-size:18px;font-weight:500;font-family:'Times New Roman',Times,serif;color:#C0C0BA;}");
  client.println(".person-card{background:#181824;border:1px solid #252535;border-radius:12px;padding:14px 16px;}");
  client.println(".avatar{width:40px;height:40px;border-radius:50%;display:flex;align-items:center;justify-content:center;font-size:13px;font-weight:700;}");
  client.println(".person-name{font-size:16px;font-weight:600;color:#E8E8E2;}");
  client.println(".person-coords{font-size:10px;color:#55556A;font-family:'Courier New',monospace;margin-top:3px;}");
  client.println(".status-row{display:flex;gap:10px;align-items:flex-start;margin-bottom:10px;}");
  client.println(".status-row:last-child{margin-bottom:0;}");
  client.println(".status-icon{width:28px;height:28px;border-radius:8px;display:flex;align-items:center;justify-content:center;flex-shrink:0;}");
  client.println(".s-ok{background:#182818;} .s-warn{background:#2A2010;} .s-err{background:#2A1010;}");
  client.println(".status-text{font-size:17px;font-weight:500;color:#C8C8C2;}");
  client.println(".status-sub{font-size:11px;color:#55556A;margin-top:2px;}");
  client.println(".refresh-note{font-size:11px;color:#30303C;text-align:right;margin-top:12px;}");
  client.println("@media(max-width:480px){.grid4{grid-template-columns:repeat(2,1fr);}.grid2{grid-template-columns:1fr;}}");
  client.println("</style></head><body><div class='wrap'>");

  // topbar
  client.println("<div class='topbar'>");
  client.println("<div class='topbar-title'><span class='live-dot'></span>Tracking System</div>");
  client.println("<span class='badge badge-ok'>Live</span>");
  client.println("</div>");

  // fall banner
  if (fallDetected) client.println(fallBanner);

  // metric cards
  client.println("<div class='grid4'>");

  client.println("<div class='metric'>");
  client.println("<div class='metric-label'>Active User</div>");
  client.print("<div class='metric-value' style='font-size:13px;color:#74C0FC;'>");
  client.print(activeUser);
  client.println("</div><div class='metric-sub'>RFID matched</div></div>");

  client.println("<div class='metric'>");
  client.println("<div class='metric-label'>Distance</div>");
  client.print("<div class='metric-value'>");
  client.print(distStr);
  client.println("<span style='font-size:12px;font-weight:400;color:#55556A;'> m</span></div>");
  client.println("<div class='metric-sub'>from center</div></div>");

  client.println("<div class='metric'>");
  client.println("<div class='metric-label'>Geofence</div>");
  client.print("<div class='metric-value' style='font-size:13px;color:");
  client.print(fenceColor); client.print(";'>");
  client.print(fenceStatus);
  client.println("</div><div class='metric-sub'>radius 200 m</div></div>");

  client.println("<div class='metric'>");
  client.println("<div class='metric-label'>Buzzer</div>");
  client.print("<div class='metric-value' style='font-size:13px;color:");
  client.print(buzzerColor); client.print(";'>");
  client.print(buzzerStatus);
  client.println("</div><div class='metric-sub'>geofence alarm</div></div>");

  client.println("</div>");

  // GPS + accel cards
  client.println("<div class='grid2'>");

  client.println("<div class='card'>");
  client.println("<div class='card-title'>GPS Position</div>");

  client.print("<div class='row'><span class='row-label'>Latitude</span><span class='row-value'>");
  client.print(latStr); client.println("</span></div>");

  client.print("<div class='row'><span class='row-label'>Longitude</span><span class='row-value'>");
  client.print(lonStr); client.println("</span></div>");

  client.print("<div class='row'><span class='row-label'>Center lat</span><span class='row-value'>");
  client.print(String(centerLat, 6)); client.println("</span></div>");

  client.print("<div class='row'><span class='row-label'>Center lon</span><span class='row-value'>");
  client.print(String(centerLon, 6)); client.println("</span></div>");

  client.println("</div>");

  // accel card
  client.println("<div class='card'>");
  client.println("<div class='card-title'>Accelerometer</div>");

  client.print("<div class='row'><span class='row-label'>Magnitude</span><span class='row-value' style='color:#74C0FC;'>");
  client.print(magStr); client.println(" g</span></div>");

  client.print("<div class='row'><span class='row-label'>Fall phase</span><span class='row-value'>");
  if      (trigger3) client.print("Gyro settling");
  else if (trigger2) client.print("Orientation chk");
  else if (trigger1) client.print("Impact watch");
  else               client.print("Monitoring");
  client.println("</span></div>");

  client.print("<div class='row'><span class='row-label'>Fall status</span><span class='row-value' style='color:");
  client.print(fallDetected ? "#FF6B6B" : "#51CF66");
  client.print(";font-weight:600;'>");
  client.print(fallDetected ? "DETECTED" : "Clear");
  client.println("</span></div>");

  client.print("<div class='row'><span class='row-label'>FF threshold</span><span class='row-value'>&lt; 0.4 g</span></div>");
  client.print("<div class='row'><span class='row-label'>Impact threshold</span><span class='row-value'>&gt; 3.0 g</span></div>");

  client.println("</div>");
  client.println("</div>"); // end grid2

  // person cards
  client.println("<div class='grid2'>");

  // person 1
  client.println("<div class='person-card'>");
  client.println("<div style='display:flex;align-items:center;gap:12px;margin-bottom:10px;'>");
  client.print("<div class='avatar' style='background:");
  client.print(p1Bg); client.print(";color:"); client.print(p1Tc);
  client.println(";'>P1</div><div>");
  client.println("<div class='person-name'>Person 1</div>");
  client.print("<div class='person-coords'>");
  client.print(p1latStr); client.print(", "); client.print(p1lonStr);
  client.println("</div></div></div>");
  client.print("<span class='badge' style='background:");
  client.print(p1Bg); client.print(";color:"); client.print(p1Tc);
  client.print(";border:1px solid "); client.print(p1Tc); client.print("44;'>");
  client.print(p1Status); client.println("</span></div>");

  // person 2
  client.println("<div class='person-card'>");
  client.println("<div style='display:flex;align-items:center;gap:12px;margin-bottom:10px;'>");
  client.print("<div class='avatar' style='background:");
  client.print(p2Bg); client.print(";color:"); client.print(p2Tc);
  client.println(";'>P2</div><div>");
  client.println("<div class='person-name'>Person 2</div>");
  client.print("<div class='person-coords'>");
  client.print(p2latStr); client.print(", "); client.print(p2lonStr);
  client.println("</div></div></div>");
  client.print("<span class='badge' style='background:");
  client.print(p2Bg); client.print(";color:"); client.print(p2Tc);
  client.print(";border:1px solid "); client.print(p2Tc); client.print("44;'>");
  client.print(p2Status); client.println("</span></div>");

  client.println("</div>"); // end person grid

  // sensor status
  client.println("<div class='card'>");
  client.println("<div class='card-title'>Sensor Status</div>");

  client.println("<div class='status-row'><div class='status-icon s-ok'>");
  client.println("<svg width='14' height='14' viewBox='0 0 14 14'><path d='M2.5 7L6 10.5L11.5 4' stroke='#51CF66' stroke-width='1.5' stroke-linecap='round' stroke-linejoin='round' fill='none'/></svg></div>");
  client.println("<div><div class='status-text'>MPU6050 accelerometer</div>");
  client.println("<div class='status-sub'>3-trigger gyro fall detection active</div></div></div>");

  client.println("<div class='status-row'><div class='status-icon s-ok'>");
  client.println("<svg width='14' height='14' viewBox='0 0 14 14'><path d='M2.5 7L6 10.5L11.5 4' stroke='#51CF66' stroke-width='1.5' stroke-linecap='round' stroke-linejoin='round' fill='none'/></svg></div>");
  client.println("<div><div class='status-text'>RFID RC522</div>");
  client.println("<div class='status-sub'>Ready · waiting for card tap</div></div></div>");

  bool gpsFallback = (lat == 12.841625 && lon == 80.155334);
  client.print("<div class='status-row'><div class='status-icon ");
  client.print(gpsFallback ? "s-warn" : "s-ok"); client.println("'>");
  if (gpsFallback)
    client.println("<svg width='14' height='14' viewBox='0 0 14 14'><path d='M7 2v6' stroke='#FFA94D' stroke-width='1.5' stroke-linecap='round' fill='none'/><circle cx='7' cy='11' r='.75' fill='#FFA94D'/></svg>");
  else
    client.println("<svg width='14' height='14' viewBox='0 0 14 14'><path d='M2.5 7L6 10.5L11.5 4' stroke='#51CF66' stroke-width='1.5' stroke-linecap='round' stroke-linejoin='round' fill='none'/></svg>");
  client.println("</div><div><div class='status-text'>GPS module</div>");
  client.print("<div class='status-sub'>");
  client.print(gpsFallback ? "Indoor fallback active" : "Live GPS signal");
  client.println("</div></div></div>");

  client.println("<div class='status-row'><div class='status-icon s-ok'>");
  client.println("<svg width='14' height='14' viewBox='0 0 14 14'><path d='M2.5 7L6 10.5L11.5 4' stroke='#51CF66' stroke-width='1.5' stroke-linecap='round' stroke-linejoin='round' fill='none'/></svg></div>");
  client.println("<div><div class='status-text'>WiFi</div>");
  client.println("<div class='status-sub'>Connected · server on port 80</div></div></div>");

  client.println("</div>"); // end sensor card

  client.println("<div class='refresh-note'>Auto-refreshes every 5 seconds</div>");
  client.println("</div></body></html>");
}


void setup()
{
  Serial.begin(115200);
  Serial1.begin(9600);

  Wire.begin();
  mpu.initialize();
  Wire.beginTransmission(0x68);
  Wire.write(0x6B);
  Wire.write(0);
  Wire.endTransmission(true);

  SPI.begin();
  rfid.PCD_Init();

  pinMode(BUZZER, OUTPUT);

  WiFi.disconnect();
  delay(1000);

  Serial.print("Connecting WiFi");
  while (WiFi.begin(ssid, password) != WL_CONNECTED)
  {
    Serial.print(".");
    delay(1500);
  }
  Serial.println("\nWiFi connected");

  IPAddress ip;
  do {
    delay(500);
    ip = WiFi.localIP();
    Serial.println("Getting IP...");
  } while (ip == IPAddress(0, 0, 0, 0));

  Serial.print("IP Address: ");
  Serial.println(ip);

  server.begin();
}


void loop()
{
  // GPS
  while (Serial1.available())
  {
    if (gps.encode(Serial1.read()))
      gps.f_get_position(&lat, &lon);
  }

  if (lat == 0 && lon == 0)
  {
    lat = 12.841625;
    lon = 80.155334;
  }

  // RFID
  if (rfid.PICC_IsNewCardPresent() && rfid.PICC_ReadCardSerial())
  {
    String uid = getUID();
    Serial.print("RFID: "); Serial.println(uid);
    beep();

    if (uid == card1)
    {
      activeUser = "PERSON 1";
      p1_lat = lat; p1_lon = lon;
      Serial.println("PERSON 1 SCANNED");
    }
    else if (uid == card2)
    {
      activeUser = "PERSON 2";
      p2_lat = lat; p2_lon = lon;
      Serial.println("PERSON 2 SCANNED");
    }
    else
    {
      Serial.println("UNKNOWN CARD");
    }
    rfid.PICC_HaltA();
  }

  // FALL DETECTION
  updateFallDetection();

  // Auto-reset fallDetected after 30 seconds
  static unsigned long fallTime = 0;
  if (fallDetected && fallTime == 0) fallTime = millis();
  if (fallDetected && (millis() - fallTime > 30000)) { fallDetected = false; fallTime = 0; }
  if (!fallDetected) fallTime = 0;

  // GEOFENCE
  double dist = haversine(lat, lon, centerLat, centerLon);
  bool outside = (dist > radius);

  if (outside)
  {
    unsigned long now = millis();
    if (!buzzerOn && (now - buzzerLastToggle >= 5000))
    {
      tone(BUZZER, 1500);
      buzzerOn = true;
      buzzerLastToggle = now;
    }
    else if (buzzerOn && (now - buzzerLastToggle >= 2000))
    {
      noTone(BUZZER);
      buzzerOn = false;
      buzzerLastToggle = now;
    }
  }
  else
  {
    noTone(BUZZER);
    buzzerOn = false;
    buzzerLastToggle = 0;
  }

  // WEB SERVER
  WiFiClient client = server.available();
  if (client)
  {
    sendWebPage(client, dist, outside);
    client.stop();
  }

  delay(100);
}
