# 002 — nvs_store

## But
Persister la config runtime (broker MQTT, AP fallback) et N "télécommandes virtuelles" Somfy (`remote_id` 24 bits + `rolling_code` 16 bits + nom) en NVS.

## Modules cibles
`include/nvs_store.h`, `src/nvs_store.cpp`

## Statut
Backlog — sera détaillé au démarrage de l'iter (3 fichiers spec/architecture/plan).
