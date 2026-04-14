# Capteurs Geolocalises de Qualite de l'Air

Systeme de surveillance de la qualite de l'air embarque sur velo. Les donnees sont collectees en temps reel, remontees via MQTT et visualisees sur une carte interactive.

## Architecture

```
M5Stack (capteurs)
    |
    | MQTT TLS (port 8883)
    v
HiveMQ Cloud (broker MQTT distant)
    |
    | MQTT
    v
Home Assistant (hors Docker)
    |                      \
    | InfluxDB              \ alertes email (SMTP)
    |                        \__________________
    |                                          | 
    v                                          v
InfluxDB 2.7  <--Flux-->  Grafana 11       Boite mail
  (Docker)                 (Docker)
                             |
                             v
                    Carte interactive
                    (http://localhost:3000)
```

## Materiel

| Composant | Role | Connexion |
|---|---|---|
| M5Stack Basic (ESP32) | Microcontroleur + ecran | -- |
| PMS5003 | Particules PM1.0, PM2.5, PM10 | Port B (UART1 RX=36, TX=26) |
| BME680 | Resistance gaz (VOC) | Port A (I2C 0x76/0x77) |
| SGP30 | TVOC (ppb), eCO2 (ppm) | Port A (I2C 0x58) |
| SHT3X (ENV III) | Temperature, Humidite | Port A (I2C 0x44) |
| QMP6988 (ENV III) | Pression, Altitude | Port A (I2C 0x70) |
| GPS | Lat, Lon, Alt, Satellites | Port C (UART2 RX=16, TX=17) |
| Carte SD | Buffer hors-ligne | Slot integre |

## Interface M5Stack

| Bouton | Action |
|---|---|
| **A** (gauche) | Page precedente |
| **B** (milieu) | Ecran on/off |
| **C** (droite) | Page suivante |

| Page | Contenu |
|---|---|
| **Air** | PM1.0, PM2.5, PM10 + TVOC, eCO2 (4 niveaux : BON / MOYEN / MAUVAIS / DANGER) |
| **Env** | Temperature, Humidite, Pression, GPS |

La barre de statut affiche la connexion (vert/jaune/rouge) et le niveau de batterie.

## Seuils de qualite de l'air

| Polluant | BON | MOYEN | MAUVAIS | DANGER | Source |
|---|---|---|---|---|---|
| PM2.5 (ug/m3) | 0-10 | 10-25 | 25-50 | >50 | Indice ATMO, OMS 2021 |
| PM10 (ug/m3) | 0-20 | 20-50 | 50-100 | >100 | Indice ATMO, OMS 2021 |
| PM1.0 (ug/m3) | 0-10 | 10-20 | 20-35 | >35 | Extrapole depuis PM2.5 |
| TVOC (ppb) | 0-220 | 220-660 | 660-2200 | >2200 | ANSES, UBA (Allemagne) |
| eCO2 (ppm) | 400-800 | 800-1200 | 1200-2000 | >2000 | ANSES, NF EN 16798-1 |

Sources :
- **Indice ATMO** : atmo-france.org (indice national de qualite de l'air)
- **OMS 2021** : WHO Global Air Quality Guidelines
- **ANSES** : Agence nationale de securite sanitaire (qualite air interieur)
- **UBA** : Umweltbundesamt (agence federale allemande, reference TVOC)
- **NF EN 16798-1** : norme europeenne ventilation et qualite air interieur

## Donnees MQTT

Topic : `sensors/m5stack_1/data` (toutes les 10 s)

```json
{
  "pm1": 2, "pm25": 5, "pm10": 8,
  "voc": 245.3, "tvoc": 120, "eco2": 412,
  "temp": 22.4, "humi": 58.1,
  "pres": 1013.2, "alt": 120.0,
  "lat": 48.11344, "lng": -1.64723,
  "gps_alt": 125.0, "sats": 8,
  "battery": 85, "charging": false
}
```

Si le MQTT est indisponible, les messages sont bufferises sur carte SD (max 500 KB) et renvoyes a la reconnexion.

## Installation

### Prerequis

- [PlatformIO](https://platformio.org/install)
- Docker + Docker Compose
- Home Assistant deja installe
- Compte [HiveMQ Cloud](https://console.hivemq.cloud) (gratuit)

### 1. Identifiants firmware

```bash
cp src/credentials.h.example src/credentials.h
```

Remplir `WIFI_SSID`, `WIFI_PASS`, `MQTT_HOST`, `MQTT_USER`, `MQTT_PASS`, `DEVICE_ID`.
Ce fichier est ignore par git.

### 2. Flasher le M5Stack

```bash
pio run --target upload
```

### 3. Lancer InfluxDB + Grafana

```bash
cd docker
cp .env.example .env
```

Remplir `INFLUXDB_PASSWORD`, `INFLUXDB_TOKEN`, `GRAFANA_ADMIN_PASSWORD`.

```bash
docker compose up -d
```

### 4. Configurer Home Assistant

**a) MQTT** : Settings > Add integration > MQTT. Meme host/user/pass que dans `credentials.h`, port 8883 avec TLS.

**b) Entites** : copier `homeassistant/m5stack_sensors.yaml` dans `configuration.yaml`, puis Settings > YAML > Reload MQTT.

**c) InfluxDB** : Settings > Add integration > InfluxDB. Host = IP machine Docker, port 8086, API v2, org `homeassistant`, bucket `homeassistant`, token = `INFLUXDB_TOKEN`.

### 5. Alertes email (optionnel)

Des alertes par email sont envoyees automatiquement quand un seuil DANGER est depasse (ex : eCO2 > 2000 ppm).

**a)** Ajouter le mot de passe SMTP dans `secrets.yaml` de HA :

```yaml
smtp_password: "mot_de_passe_email"
```

**b)** Copier le contenu de `homeassistant/alerts.yaml` dans `configuration.yaml`, puis redemarrer HA.

**c)** Verifier dans Settings > Automations que les 3 alertes apparaissent.

### 6. Ouvrir la carte

`http://localhost:3000` > Dashboards > **Air Quality Map**

## Stack

| Service | Port | Role |
|---|---|---|
| HiveMQ Cloud | 8883 | Broker MQTT (TLS) |
| Home Assistant | 8123 | Bridge MQTT > InfluxDB |
| InfluxDB | 8086 | Stockage time-series (Docker) |
| Grafana | 3000 | Dashboard carte (Docker) |

## Ajouter un 2eme velo

1. Changer `DEVICE_ID` en `"m5stack_2"` dans `credentials.h` et flasher.
2. Dupliquer les entites MQTT dans HA en remplacant `m5stack_1` par `m5stack_2`.
3. Dans Grafana, elargir la regex : `^m5stack_1_` > `^m5stack_(1|2)_`.

## Equipe

- [bosy0](https://github.com/bosy0)
- [RemRem-28](https://github.com/RemRem-28)
- [mariusduperrier](https://github.com/mariusduperrier)
- [alielazzouzi2005-del](https://github.com/alielazzouzi2005-del)
