#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <Servo.h>

// --- Pin Definitions ---
#define IN1 D1   // L293D Direction Pin 1
#define IN2 D2   // L293D Direction Pin 2
#define ENA D5   // L293D Speed Pin (PWM)
#define SERVO_PIN D6 // Front Servo Pin

// --- Wi-Fi Credentials ---
const char* ssid = "RCCar_NodeMCU";
const char* password = "password123";

ESP8266WebServer server(80);
Servo steeringServo;

// --- Webpage HTML + Low-Latency Async JavaScript ---
const char HTML_PAGE[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <meta name="viewport" content="width=device-width, initial-scale=1.0, maximum-scale=1.0, user-scalable=no">
  <title>Low-Latency RC Controller</title>
  <style>
    * { box-sizing: border-box; touch-action: none; -webkit-touch-callout: none; -webkit-user-select: none; }
    body {
      background-color: #0d0d0d;
      color: #ffffff;
      font-family: sans-serif;
      margin: 0;
      padding: 0;
      width: 100vw;
      height: 100vh;
      overflow: hidden;
      display: flex;
      justify-content: space-between;
      align-items: center;
    }
    .joystick-zone {
      position: relative;
      width: 160px;
      height: 160px;
      background: #1a1a1a;
      border: 4px solid #00adb5;
      border-radius: 50%;
      margin: 0 5vw;
    }
    .handle {
      position: absolute;
      width: 60px;
      height: 60px;
      background: #00adb5;
      border-radius: 50%;
      top: 50px;
      left: 50px;
      box-shadow: 0 0 15px rgba(0, 173, 181, 0.6);
      pointer-events: none;
    }
    .label {
      position: absolute;
      top: -30px;
      width: 100%;
      text-align: center;
      font-weight: bold;
      color: #00adb5;
    }
  </style>
</head>
<body>

  <!-- Left Joystick: Speed / Rear Motor -->
  <div class="joystick-zone" id="leftZone">
    <div class="label">SPEED</div>
    <div class="handle" id="leftHandle"></div>
  </div>

  <!-- Right Joystick: Steering / Front Servo -->
  <div class="joystick-zone" id="rightZone">
    <div class="label">STEERING</div>
    <div class="handle" id="rightHandle"></div>
  </div>

  <script>
    let currentSpeed = 0;
    let currentSteer = 0;
    let lastSentSpeed = null;
    let lastSentSteer = null;
    let sendInFlight = false;

    const leftZone = document.getElementById('leftZone');
    const leftHandle = document.getElementById('leftHandle');
    const rightZone = document.getElementById('rightZone');
    const rightHandle = document.getElementById('rightHandle');

    // 1. Ultra-fast non-blocking HTTP fetch pipeline
    function sendCommand() {
      if (sendInFlight) return; // Prevent network queue flooding
      if (currentSpeed === lastSentSpeed && currentSteer === lastSentSteer) return;

      sendInFlight = true;
      lastSentSpeed = currentSpeed;
      lastSentSteer = currentSteer;

      fetch(`/control?speed=${currentSpeed}&steer=${currentSteer}`, { keepalive: true })
        .then(() => { sendInFlight = false; })
        .catch(() => { sendInFlight = false; });
    }

    // 2. High-speed 50Hz control loop (20ms)
    setInterval(sendCommand, 20);

    function processTouches(e) {
      e.preventDefault();

      let leftActive = false;
      let rightActive = false;

      const leftRect = leftZone.getBoundingClientRect();
      const rightRect = rightZone.getBoundingClientRect();

      const leftCenterX = leftRect.left + 80;
      const leftCenterY = leftRect.top + 80;
      const rightCenterX = rightRect.left + 80;
      const rightCenterY = rightRect.top + 80;

      const radius = 80; // Boundary limit of 160px diameter circle

      for (let i = 0; i < e.touches.length; i++) {
        const touch = e.touches[i];

        // --- Left Joystick Processing (Speed) ---
        const distLeft = Math.hypot(touch.clientX - leftCenterX, touch.clientY - leftCenterY);
        if (distLeft <= radius) {
          leftActive = true;
          let y = touch.clientY - leftCenterY;
          y = Math.max(-60, Math.min(60, y));
          leftHandle.style.transform = `translate(0px, ${y}px)`;
          currentSpeed = Math.round((-y / 60) * 255);
        }

        // --- Right Joystick Processing (Steering) ---
        const distRight = Math.hypot(touch.clientX - rightCenterX, touch.clientY - rightCenterY);
        if (distRight <= radius) {
          rightActive = true;
          let x = touch.clientX - rightCenterX;
          x = Math.max(-60, Math.min(60, x));
          rightHandle.style.transform = `translate(${x}px, 0px)`;
          currentSteer = Math.round((x / 60) * 70); 
        }
      }

      // OUTSIDE CIRCLE / RELEASE RESET LOGIC:
      if (!leftActive) {
        leftHandle.style.transform = 'translate(0px, 0px)';
        currentSpeed = 0;
      }

      if (!rightActive) {
        rightHandle.style.transform = 'translate(0px, 0px)';
        currentSteer = 0;
      }
    }

    // Global touch listeners across the display
    window.addEventListener('touchstart', processTouches, {passive: false});
    window.addEventListener('touchmove', processTouches, {passive: false});
    window.addEventListener('touchend', processTouches, {passive: false});
    window.addEventListener('touchcancel', processTouches, {passive: false});
  </script>
</body>
</html>
)rawliteral";

void handleControl() {
  if (server.hasArg("speed") && server.hasArg("steer")) {
    int speedVal = server.arg("speed").toInt();
    int steerVal = server.arg("steer").toInt();

    // Motor Logic (Rear Wheels)
    if (speedVal > 15) {
      digitalWrite(IN1, HIGH);
      digitalWrite(IN2, LOW);
      analogWrite(ENA, speedVal);
    } else if (speedVal < -15) {
      digitalWrite(IN1, LOW);
      digitalWrite(IN2, HIGH);
      analogWrite(ENA, abs(speedVal));
    } else {
      digitalWrite(IN1, LOW);
      digitalWrite(IN2, LOW);
      analogWrite(ENA, 0);
    }

    // Servo Logic (Front Steering)
    int servoAngle = 90 + steerVal;
    servoAngle = constrain(servoAngle, 20, 160);
    steeringServo.write(servoAngle);

    server.send(200, "text/plain", "OK");
  } else {
    server.send(400, "text/plain", "Bad Request");
  }
}

void setup() {
  pinMode(IN1, OUTPUT);
  pinMode(IN2, OUTPUT);
  pinMode(ENA, OUTPUT);

  digitalWrite(IN1, LOW);
  digitalWrite(IN2, LOW);
  analogWrite(ENA, 0);

  steeringServo.attach(SERVO_PIN);
  steeringServo.write(90);

  WiFi.mode(WIFI_AP);
  WiFi.softAP(ssid, password);

  // Optimizations to reduce latency on ESP8266 WiFi driver
  WiFi.setOutputPower(20.5); 

  server.on("/", []() {
    server.send(200, "text/html", HTML_PAGE);
  });
  server.on("/control", handleControl);

  server.begin();
}

void loop() {
  server.handleClient();
}