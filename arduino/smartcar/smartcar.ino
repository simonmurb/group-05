#include <vector>

#include <MQTT.h>
#include <WiFi.h>
#ifdef __SMCE__
#include <OV767X.h>
#endif

#include <Smartcar.h>

MQTTClient mqtt;
WiFiClient net;

bool safetyFeatures = true;
bool canDrive = true;

bool activeAvoidance = false;
//bool inMotion = false;

const char ssid[] = "***";
const char pass[] = "****";

ArduinoRuntime arduinoRuntime;
BrushedMotor leftMotor(arduinoRuntime, smartcarlib::pins::v2::leftMotorPins);
BrushedMotor rightMotor(arduinoRuntime, smartcarlib::pins::v2::rightMotorPins);
DifferentialControl control(leftMotor, rightMotor);

SimpleCar car(control);

//infrared sensor//
const int frontIRPin = 0;
const int leftIRPin = 1;
const int rightIRPin = 2;
const int backIRPin = 3;
GP2Y0A02 frontIR(arduinoRuntime, frontIRPin); 
GP2Y0A02 leftIR(arduinoRuntime, leftIRPin); 
GP2Y0A02 rightIR(arduinoRuntime, rightIRPin); 
GP2Y0A21 backIR(arduinoRuntime, backIRPin); 
    //measure infrared between 0 and 40


const auto oneSecond = 1UL;

//ultrasounds sensor//
#ifdef __SMCE__ //Four simulator
const auto triggerPin = 6;
const auto echoPin = 7;
const auto mqttBrokerUrl = "127.0.0.1";
#else           //for car
const auto triggerPin = 33;
const auto echoPin = 32;
const auto mqttBrokerUrl = "192.168.0.40";
#endif
const auto maxfrontUltDis = 100;
SR04 frontUlt(arduinoRuntime, triggerPin, echoPin, maxfrontUltDis);




std::vector<char> frameBuffer;

void setup()
{
    Serial.begin(9600);
#ifdef __SMCE__
    Camera.begin(QVGA, RGB888, 15);
    frameBuffer.resize(Camera.width() * Camera.height() * Camera.bytesPerPixel());
#endif

    WiFi.begin(ssid, pass);
    mqtt.begin(mqttBrokerUrl, 1883, net);

    Serial.println("Connecting to WiFi...");
    auto wifiStatus = WiFi.status();
    while (wifiStatus != WL_CONNECTED && wifiStatus != WL_NO_SHIELD)
    {
        Serial.println(wifiStatus);
        Serial.print(".");
        delay(1000);
        wifiStatus = WiFi.status();
    }

    Serial.println("Connecting to MQTT broker");
    while (!mqtt.connect("arduino", "public", "public"))
    {
        Serial.print(".");
        delay(1000);
    }

    mqtt.subscribe("/smartcar/control/#", 1);
    mqtt.onMessage([](String topic, String message)
                   {
    if (topic == "/smartcar/control/throttle") {
      car.setSpeed(message.toInt());
    } else if (topic == "/smartcar/control/steering") {
      car.setAngle(message.toInt());
    } else {
      Serial.println(topic + " " + message);
    } });
}

void loop()
{
    if (mqtt.connected())
    {
        mqtt.loop();
        const auto currentTime = millis();
#ifdef __SMCE__
        static auto previousFrame = 0UL;
        if (currentTime - previousFrame >= 65)
        {
            previousFrame = currentTime;
            Camera.readFrame(frameBuffer.data());
            mqtt.publish("/smartcar/camera", frameBuffer.data(), frameBuffer.size(),
                         false, 0);
        }
#endif
        static auto previousTransmission = 0UL;
        if (currentTime - previousTransmission >= oneSecond)
        {
            previousTransmission = currentTime;
            const auto frontUltDis = frontUlt.getDistance();
            const auto frontIRDis = frontIR.getDistance();
            const auto leftIRDis = leftIR.getDistance();
            const auto rightIRDis = rightIR.getDistance();
            const auto backIRDis = backIR.getDistance();
            
            if (safetyFeatures)
            {
                if (!activeAvoidance)
                {
                stopZoneAutoBreak(frontUltDis, frontIRDis, backIRDis);
                }
                incomingAvoidancethreshold(frontUltDis, frontIRDis, backIRDis);
            }
            
            Serial.println(frontUltDis);
            mqtt.publish("/smartcar/ultrasound/front", String(frontUltDis));
        }
#ifdef __SMCE__
        // Avoid over-using the CPU if we are running in the emulator
        delay(1);
#endif
    }
}

void stopZoneAutoBreak(long frontUltDis, long frontIRDis, long backIRDis)
{
     if (frontUltDis <= 40 && frontUltDis != 0 || frontIRDis <= 40 && frontIRDis != 0 || backIRDis <= 40 && backIRDis != 0)//stop zone
         {
            if (canDrive)//check whether you're in the stop zone
            {
                car.setSpeed(0);
                Serial.println("Emergency stop1");
            }
            canDrive = false;//so the car can move in the stop soon
        } else {
            canDrive = true;//so the car will stop again if it hits the stop zone
        }
}

void incomingAvoidancethreshold(long frontUltDis, long frontIRDis, long backIRDis)
{
    if (frontUltDis <= 20 && frontUltDis != 0 || frontIRDis <= 20 && frontIRDis != 0)//Ford threshold
    {
        car.setSpeed(0);
        car.setSpeed(-10);
        Serial.println("backing up");
        activeAvoidance = true;
    } else if (backIRDis <= 20 && backIRDis != 0)
    {
        car.setSpeed(0);
        car.setSpeed(10);
        Serial.println("moving forward");
        activeAvoidance = true;
    } else if (backIRDis == 0 && frontIRDis == 0 && activeAvoidance)
    {
        car.setSpeed(0);
        activeAvoidance = false;
    }
    
    
      
}