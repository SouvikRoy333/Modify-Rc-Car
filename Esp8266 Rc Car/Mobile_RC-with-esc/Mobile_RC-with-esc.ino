#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <Servo.h>

// --- Hardware Pin Definitions (Same as original setup) ---
#define ESC_PIN D5    // 10A Brushed ESC signal line
#define SERVO_PIN D6  // Steering Servo signal line

// --- Wi-Fi Credentials ---
const char* ssid = "RCCar_NodeMCU";
const char* password = "password123";

ESP8266WebServer server(80);

Servo motorESC;
Servo steeringServo;

// --- Calibration Variables ---
int servoCenter = 90;     // Neutral angle (degrees)
int servoMin = 10;        // Left turn limit
int servoMax = 170;       // Right turn limit
bool reverseServo = true; // Flip steering direction if needed

// --- Mobile Touch Web Interface ---
const char HTML_PAGE[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <meta name="viewport" content="width=device-width, initial-scale=1.0, maximum-scale=1.0, user-scalable=no">
  <title>ESC & Servo Controller</title>
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
      width: 220px;
      height: 220px;
      background: #1a1a1a;
      border: 4px solid #00adb5;
      border-radius: 50%;
      margin: 0 4vw;
    }
    .handle {
      position: absolute;
      width: 70px;
      height: 70px;
      background: #00adb5;
      border-radius: 50%;
      top: 75px;
      left: 75px;
      box-shadow: 0 0 15px rgba(0, 173, 181, 0.6);
      pointer-events: none;
    }
    .label {
      position: absolute;
      top: -35px;
      width: 100%;
      text-align: center;
      font-weight: bold;
      font-size: 18px;
      color: #00adb5;
    }
  </style>
</head>
<body>

  <!-- Left Joystick: Throttle (ESC) -->
  <div class="joystick-zone" id="leftZone">
    <div class="label">THROTTLE (ESC)</div>
    <div class="handle" id="leftHandle"></div>
  </div>

  <!-- Right Joystick: Steering (Servo) -->
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

    function sendCommand() {
      if (sendInFlight) return;
      if (currentSpeed === lastSentSpeed && currentSteer === lastSentSteer) return;

      sendInFlight = true;
      lastSentSpeed = currentSpeed;
      lastSentSteer = currentSteer;

      fetch(`/control?speed=${currentSpeed}&steer=${currentSteer}`, { keepalive: true })
        .then(() => { sendInFlight = false; })
        .catch(() => { sendInFlight = false; });
    }

    setInterval(sendCommand, 20);

    function processTouches(e) {
      e.preventDefault();

      let leftActive = false;
      let rightActive = false;

      const leftRect = leftZone.getBoundingClientRect();
      const rightRect = rightZone.getBoundingClientRect();

      const leftCenterX = leftRect.left + 110;
      const leftCenterY = leftRect.top + 110;
      const rightCenterX = rightRect.left + 110;
      const rightCenterY = rightRect.top + 110;

      const activeRadius = 140; 

      for (let i = 0; i < e.touches.length; i++) {
        const touch = e.touches[i];

        // Left Joystick (Throttle)
        const distLeft = Math.hypot(touch.clientX - leftCenterX, touch.clientY - leftCenterY);
        if (distLeft <= activeRadius) {
          leftActive = true;
          let y = touch.clientY - leftCenterY;
          y = Math.max(-80, Math.min(80, y));
          leftHandle.style.transform = `translate(0px, ${y}px)`;
          currentSpeed = Math.round((-y / 80) * 100);
        }

        // Right Joystick (Steering)
        const distRight = Math.hypot(touch.clientX - rightCenterX, touch.clientY - rightCenterY);
        if (distRight <= activeRadius) {
          rightActive = true;
          let x = touch.clientX - rightCenterX;
          x = Math.max(-80, Math.min(80, x));
          rightHandle.style.transform = `translate(${x}px, 0px)`;
          currentSteer = Math.round((x / 80) * 100);
        }
      }

      if (!leftActive) {
        leftHandle.style.transform = 'translate(0px, 0px)';
        currentSpeed = 0;
      }

      if (!rightActive) {
        rightHandle.style.transform = 'translate(0px, 0px)';
        currentSteer = 0;
      }
    }

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
    int speedPct = server.arg("speed").toInt(); // -100 to +100
    int steerPct = server.arg("steer").toInt(); // -100 to +100

    // ESC Control: 1500us (neutral), 2000us (full forward), 1000us (full reverse)
    int escPulse = map(speedPct, -100, 100, 1000, 2000);
    motorESC.writeMicroseconds(escPulse);

    // Servo Control
    int steerOffset = map(steerPct, -100, 100, -80, 80);
    int targetAngle;

    if (reverseServo) {
      targetAngle = servoCenter - steerOffset;
    } else {
      targetAngle = servoCenter + steerOffset;
    }

    targetAngle = constrain(targetAngle, servoMin, servoMax);
    steeringServo.write(targetAngle);

    server.send(200, "text/plain", "OK");
  } else {
    server.send(400, "text/plain", "Bad Request");
  }
}

void setup() {
  // Attach ESC to D1 (Pulse width range: 1000us - 2000us)
  motorESC.attach(ESC_PIN, 1000, 2000);
  motorESC.writeMicroseconds(1500); // Arms the ESC at startup

  // Attach Steering Servo to D5
  steeringServo.attach(SERVO_PIN);
  steeringServo.write(servoCenter);

  // Configure Wi-Fi Access Point
  WiFi.mode(WIFI_AP);
  WiFi.softAP(ssid, password);
  WiFi.setOutputPower(20.5);

  // Setup Web Server Routes
  server.on("/", []() {
    server.send(200, "text/html", HTML_PAGE);
  });
  server.on("/control", handleControl);

  server.begin();
}

void loop() {
  server.handleClient();
}