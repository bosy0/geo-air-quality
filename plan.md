  1. Contexte et objectif (1 min) Personne 1 . ALI

  - Problème : mesurer la qualité de l'air en mobilité (vélo) avec géolocalisation
  - Pourquoi c'est pertinent : pollution urbaine, données hyperlocales vs stations fixes ATMO


  2. Hardware et capteurs (2 min) Personne 1 . ALI

  - Photo/schéma du montage avec les capteurs
  - Expliquer les bus : I2C (BME680, SGP30, SHT3X, QMP6988 sur Port A), UART1 (PMS5003), UART2 (GPS)
  - Mentionner les contraintes : calibration SGP30 + satellites, compensation humidité*


  3. Architecture globale (3 min) Personne 2 . MARIUS et 3 . NATHAN
  
  - Montrer le schéma du README (M5Stack → HiveMQ → HA → InfluxDB → Grafana)
  - Justifier chaque brique : pourquoi MQTT (léger, adapté embarqué), pourquoi HiveMQ Cloud (TLS gratuit, pas de serveur à maintenir), pourquoi HA comme bridge (intégrations
   prêtes), pourquoi InfluxDB (time-series optimisé)
  - Docker Compose : 2 services, provisioning automatique (datasource + dashboard)
  - Flux queries : pivot cross-measurement pour la carte, v.timeRangeStart dynamique
  - Alertes email : automations HA avec seuils DANGER, SMTP IONOS


  4. Firmware (3 min) Personne 4 . REMY

  - Organisation du code : boucle principale, lecture capteurs, publication MQTT, affichage
  - Points techniques intéressants à défendre :
    - Buffer SD : stockage hors-ligne si réseau indisponible, flush par batch de 5
    - Reconnexion WiFi/MQTT avec backoff exponentiel (pas de spam réseau)
    - Affichage
    - Credentials externalisés : credentials.h gitignored


  5. Démo live (3 min) Personne 3 . NATHAN et 2 . MARIUS fait acte de présence
  - Montrer le M5Stack avec les 2 pages (Air / Env)
  - Ouvrir Grafana : carte avec points colorés, stats temps réel, courbes
  - Si possible, souffler sur le capteur pour faire monter le CO2/VOC et montrer le changement de couleur
  - Seuils basés sur ATMO, OMS, ANSES, UBA — pas inventés
  - 4 niveaux : BON / MOYEN / MAUVAIS / DANGER


  6. Ouverture : Personne 2 . MARIUS

  - Vélo, scalabilité (ID dans les credentials)
  - Systeme plus élaboré : LoRa















*
Le SGP30 mesure le TVOC et eCO2 en analysant la résistance d'un film MOX (oxyde métallique) quand des gaz passent
   dessus. Le problème : l'humidité de l'air change aussi cette résistance. Un air humide va fausser les mesures à 
  la hausse.                                                                                                       
                                                                                                                 
  Le SGP30 a un algorithme interne de compensation, mais il a besoin qu'on lui dise le taux d'humidité absolue     
  ambiant. C'est pour ça qu'on utilise le SHT3X (capteur temp/humidité) pour corriger le SGP30.
                                                                                                                   
  Comment ça marche ?                                                                                            

  Le SHT3X donne l'humidité relative (ex: 58%) — c'est un pourcentage par rapport au max que l'air peut contenir à 
  cette température. Mais le SGP30 attend l'humidité absolue — la quantité réelle de vapeur d'eau en g/m³.
                                                                                                                   
  La formule :                                                                                                   

  ah = 216.7 * (h/100 * 6.112 * exp(17.62*t / (243.12+t))) / (273.15+t)
                                                                                                                   
  Décomposée :
                                                                                                                   
  1. 6.112 * exp(17.62*t / (243.12+t)) — c'est la formule de Magnus-Tetens, qui donne la pression de vapeur        
  saturante en hPa à la température t. C'est la pression max que la vapeur d'eau peut atteindre avant de condenser.
  2. h/100 * (...) — on multiplie par l'humidité relative pour obtenir la pression de vapeur réelle (si l'air est à
   58% de sa capacité max)                                                                                         
  3. 216.7 * (...) / (273.15+t) — conversion de pression (hPa) en densité (g/m³) via la loi des gaz parfaits. 216.7
   est une constante qui combine la masse molaire de l'eau et la constante des gaz.                                
                                                                                                                 
  Le * 256.0f à la fin ?                                                                                           
                                                                                                                 
  Le SGP30 attend la valeur en format point fixe 8.8 bits (8 bits entiers + 8 bits décimaux). Multiplier par 256 (=
   2⁸) revient à décaler de 8 bits, c'est le format imposé par la datasheet Sensirion.                           
                                                                                                                   
  Exemple concret                                                                                                

  À 22°C et 58% d'humidité relative :
  - Pression saturante ≈ 26.4 hPa
  - Pression réelle = 0.58 × 26.4 ≈ 15.3 hPa                                                                       
  - Humidité absolue ≈ 216.7 × 15.3 / 295.15 ≈ 11.2 g/m³
  - Envoyé au SGP30 : 11.2 × 256 = 2867 (en uint32_t)                                                              
                                                                                                                   
  Sans cette compensation, le SGP30 peut surestimer le TVOC de 10-20% par temps humide.