#include <M5Stack.h>

void setup() {
    M5.begin();
    M5.Power.begin();
    M5.Lcd.setTextSize(2);
    M5.Lcd.setCursor(10, 10);
    M5.Lcd.println("Air Quality Monitor");
    M5.Lcd.println("Initialisation...");

    Serial.begin(115200);
    Serial.println("Demarrage du capteur de qualite de l'air");
}

void loop() {
    M5.update();

    // TODO: Lecture des capteurs (CO2, humidite, PM)
    // TODO: Geolocalisation
    // TODO: Envoi MQTT vers Home Assistant
    // TODO: Affichage sur ecran M5Stack

    delay(1000);
}
