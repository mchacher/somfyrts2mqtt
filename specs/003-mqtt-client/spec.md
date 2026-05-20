# 003 — mqtt client

## But
Connect/reconnect au broker MQTT (config depuis NVS), subscribe `somfy2mqtt/+/set`, publish état avec retained, last-will. Dispatch des commandes reçues vers un handler qui se contente de logger.

## Modules cibles
`include/mqtt.h`, `src/mqtt.cpp`

## Statut
Backlog — sera détaillé au démarrage de l'iter (3 fichiers spec/architecture/plan).
