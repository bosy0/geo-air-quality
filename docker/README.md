# Stack Docker — InfluxDB + Grafana

Stack autonome qui demarre **InfluxDB 2.7** (stockage) et **Grafana 11** (carte interactive)
pour visualiser les donnees envoyees par les capteurs M5Stack.

Home Assistant (deja installe en dehors de cette stack) joue le role de pont
**MQTT HiveMQ Cloud -> InfluxDB**.

## Demarrage rapide

```bash
cd docker
cp .env.example .env
# Editer .env et remplacer toutes les valeurs "changeme"

docker compose up -d
docker compose ps     # les 2 services doivent etre "healthy" / "running"
```

Au premier demarrage, InfluxDB se configure automatiquement avec :
- Organisation : `homeassistant`
- Bucket : `homeassistant`
- Retention : 90 jours
- Token admin : la valeur de `INFLUXDB_TOKEN`

## Acces aux services

| Service  | URL                       | Identifiants                                |
|----------|---------------------------|---------------------------------------------|
| InfluxDB | http://localhost:8086     | `admin` / `INFLUXDB_PASSWORD`               |
| Grafana  | http://localhost:3000     | `admin` / `GRAFANA_ADMIN_PASSWORD`          |

Le dashboard **Air Quality Map** est provisionne automatiquement (visible dans
*Dashboards* des le premier login). La datasource InfluxDB est aussi
pre-configuree — rien a faire dans l'UI.

## Configurer Home Assistant

### 1. Integration MQTT (connexion a HiveMQ Cloud)

Dans HA -> Settings -> Devices & Services -> Add integration -> MQTT :

| Champ    | Valeur                                 |
|----------|----------------------------------------|
| Host     | le host HiveMQ (ex: `xxxxx.s1.eu.hivemq.cloud`) |
| Port     | `8883`                                 |
| TLS      | oui                                    |
| Username | le meme que `MQTT_USER` dans le firmware |
| Password | le meme que `MQTT_PASS` dans le firmware |

### 2. Entites MQTT

Copier le contenu de `homeassistant/m5stack_sensors.yaml` dans le
`configuration.yaml` de HA, puis recharger :
Settings -> System -> YAML -> Reload MQTT entities.

### 3. Integration InfluxDB

Dans HA -> Settings -> Devices & Services -> Add integration -> InfluxDB :

| Champ        | Valeur                                |
|--------------|---------------------------------------|
| Host         | IP de la machine Docker (ou `127.0.0.1` si meme machine) |
| Port         | `8086`                                |
| SSL          | non                                   |
| API version  | `2`                                   |
| Organization | `homeassistant`                       |
| Bucket       | `homeassistant`                       |
| Token        | la valeur de `INFLUXDB_TOKEN`         |

## Entites HA attendues par le dashboard

Le dashboard Grafana cherche ces entites :

```
sensor.m5stack_1_lat          sensor.m5stack_1_lng
sensor.m5stack_1_pm1          sensor.m5stack_1_pm25
sensor.m5stack_1_pm10         sensor.m5stack_1_voc
sensor.m5stack_1_tvoc         sensor.m5stack_1_eco2
sensor.m5stack_1_temp         sensor.m5stack_1_humi
sensor.m5stack_1_battery
```

Si tes entity_ids sont differents, ajuste la regex `^m5stack_1_` dans les
requetes Flux de `grafana/dashboards/air-quality-map.json`.

## Arreter / reinitialiser

```bash
docker compose down              # arrete les services
docker compose down -v           # arrete ET supprime les volumes (perte de donnees)
```
