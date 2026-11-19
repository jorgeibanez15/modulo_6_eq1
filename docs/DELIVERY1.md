# Delivery 1: grabaciones crudas + checks

> Estado: **pendiente** (falta grabar con el modo crudo del firmware).

## Qué se entrega

- [ ] 10 archivos de movimiento + 10 de reposo en Azure (`data/raw/` → contenedor)
- [ ] Una fila de `data/metadata.csv` por archivo
- [ ] Tabla con los 3 checks por archivo (abajo)
- [ ] Figura con todas las repeticiones: `figures/all_repetitions.png`

## Los 3 checks

| Check | Regla | Tolerancia / justificación |
|---|---|---|
| Nivel cero | media de un reposo ≈ offset del sensor en cuentas | _por definir en `src/checks.py`_ |
| Sin clipping | ninguna muestra en 16383; no demasiadas exactamente en 0 | _por definir_ |
| Longitud | \|N − 5 s × 1000 Hz\| / 5000 ≤ 2 % | criterio del curso |

## Resultados por archivo

| Archivo | Nivel cero | Clipping | Longitud |
|---|---|---|---|
| _pendiente_ | | | |
