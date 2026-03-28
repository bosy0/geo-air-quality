# Capteurs Géolocalisés de Qualité de l'Air

Système de surveillance de la qualité de l'air basé sur des capteurs embarqués sur des vélos. Les données sont collectées en temps réel, remontées via MQTT et visualisées sur une carte colorée avec tracé historique 24h.

## Architecture

```
M5Stack (capteurs)
    │
    │ MQTT TLS (port 8883)
    ▼
HiveMQ Cloud (broker MQTT distant)
    │
    │ MQTT
    ▼
Mosquitto (broker local Docker)
    │
    │ MQTT
    ▼
Home Assistant (domotique)
    │
    │ InfluxDB integration
    ▼
InfluxDB (base de données time-series, Docker)
    │
    │ Flux queries
    ▼
Grafana (visualisation carte, Docker)
```

## Matériel

| Composant | Rôle | Connexion |
|---|---|---|
| M5Stack Basic (ESP32) | Microcontrôleur principal + écran | — |
| PMS5003 | Particules PM1.0, PM2.5, PM10 | Port B (UART1 RX=36, TX=26) |
| BME680 | VOC / qualité air (résistance gaz) | Port A (I2C SDA=21, SCL=22) |
| SHT3X (ENV III) | Température, Humidité | Port A (I2C addr 0x44) |
| QMP6988 (ENV III) | Pression, Altitude barométrique | Port A (I2C addr 0x70) |
| GPS | Géolocalisation (lat, lon, alt, sats) | Port C (UART2 RX=16, TX=17) |

## Interface M5Stack

L'écran est organisé en 3 pages navigables :

- **Bouton A** : éteindre / allumer l'écran
- **Bouton B** : page précédente
- **Bouton C** : page suivante

| Page | Contenu |
|---|---|
| Air | PM1.0, PM2.5, PM10 (couleur qualité), barre VOC |
| Env | Température, Humidité, Pression, Altitude |
| GPS | Latitude, Longitude, Satellites, Altitude GPS |

La barre de statut en haut affiche en permanence : connexion MQTT (vert/jaune/rouge) et niveau de batterie.

## Données publiées (MQTT)

Topic : `sensors/m5stack_1/data`
Fréquence : toutes les 10 secondes
Format JSON :

```json
{
  "pm1": 2, "pm25": 5, "pm10": 8,
  "voc": 245.3,
  "temp": 22.4, "humi": 58.1,
  "pres": 1013.2, "alt": 120.0,
  "lat": 48.11344, "lng": -1.64723,
  "gps_alt": 125.0, "sats": 8,
  "battery": 85, "charging": false
}
```

## Stack logicielle

| Service | Port | Rôle |
|---|---|---|
| HiveMQ Cloud | 8883 (TLS) | Broker MQTT distant |
| Mosquitto | 1883 | Broker MQTT local (Docker) |
| Home Assistant | 8123 | Domotique, entités, alertes |
| InfluxDB | 8086 | Stockage time-series |
| Grafana | 3000 | Carte qualité de l'air |

## Installation

### Prérequis

- [PlatformIO](https://platformio.org/install) pour compiler et flasher le M5Stack
- Docker pour les services (Mosquitto, InfluxDB, Grafana)
- Home Assistant (Core)

### 1. Flasher le M5Stack

```bash
pio run --target upload
```

### 2. Lancer les services Docker

```bash
# Mosquitto
docker run -d --name mosquitto --network host eclipse-mosquitto

# InfluxDB
docker run -d --name influxdb --network host -v influxdb-data:/var/lib/influxdb2 influxdb:2

# Grafana
docker run -d --name grafana --network host \
  -e GF_SECURITY_ALLOW_EMBEDDING=true \
  -v grafana-data:/var/lib/grafana grafana/grafana
```

### 3. Configurer InfluxDB

Ouvrir `http://localhost:8086` et créer :
- Organisation : `homeassistant`
- Bucket : `homeassistant`
- Générer un token **All Access**

### 4. Configurer Home Assistant

Ajouter l'intégration InfluxDB via **Settings → Integrations → Add → InfluxDB** avec :
- URL : `http://localhost:8086`
- Organisation : `homeassistant`
- Bucket : `homeassistant`
- Token : celui généré à l'étape 3

Copier le fichier `configuration.yaml` du projet dans le dossier HA pour déclarer toutes les entités MQTT.

### 5. Configurer Grafana

- Ouvrir `http://localhost:3000`
- Ajouter InfluxDB comme datasource (Flux, `http://localhost:8086`)
- Créer un panel **Geomap** avec la requête Flux du fichier `influxdb_config.yaml`

## Ajouter un 2ème vélo

Dans le code M5Stack, changer simplement :
```cpp
#define DEVICE_ID "m5stack_2"
```

Le topic MQTT devient `sensors/m5stack_2/data`. Ajouter les entités correspondantes dans `configuration.yaml` et un nouveau layer dans Grafana.

## Équipe

- [bosy0](https://github.com/bosy0)
- [RemRem-28](https://github.com/RemRem-28)
- [mariusduperrier](https://github.com/mariusduperrier)
- [alielazzouzi2005-del](https://github.com/alielazzouzi2005-del)
