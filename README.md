# EMG → Machine Learning → IoT · BI2005B grupo 604, módulo M6

> Por privacidad, en este repositorio solo aparecen códigos (grupo y sujeto). No hay nombres, caras, edad, género ni medidas corporales.

## Equipo

| Campo | Valor |
|---|---|
| Código de grupo | `G_1?`|
| Track | `<<B>>` |
| Sujeto(s) | `S01` (y `S02` si aplica) |
| Músculo / sitio | `<<flexor de muñeca>>` ← **llenar** (p. ej. flexor de muñeca, antebrazo derecho) |
| Sensor EMG | `<<sensor>>` ← **llenar**. **La salida ya viene rectificada/envolvente** |
| Placa | Arduino UNO R4 WiFi (principal) · ESP32 (alternativa) |
| ADC | 14 bits, Vref 5.0 V, ganancia `not stated` |

## Cadena del proyecto

```
señal (sensor, ya rectificada)
  → grabación (firmware UNO R4 → USB → src/logger.py → data/raw/*.csv + data/metadata.csv)
  → checks (src/checks.py: nivel cero, sin clipping, longitud)
  → features (src/features.py: MAV, RMS, MNF, MDF → data/features/EMG_dataset_ML.xlsx)
  → modelo (src/train.py: pisos de azar y umbral + kNN k=3, prueba en sesión nueva)
  → score (figures/confusion_matrix.png, docs/DELIVERY2.md)
  → nube (src/azure_upload.py → Azure Blob; dashboard en notebooks/)
```

## Estructura

| Carpeta / archivo | Qué contiene |
|---|---|
| `firmware/uno_r4_usb/` | Firmware principal (UNO R4 WiFi, A0, 2 kHz, LCD 1602, protocolo 30 × (5 s reposo + 5 s contracción)) |
| `firmware/esp32_sheets/` | Alternativa: ESP32 que manda los resúmenes a Google Sheets |
| `firmware/secrets.example.h` | Plantilla de secretos (ver abajo) |
| `src/` | Scripts de Python (logger, checks, features, train, demo, Azure) |
| `data/features/EMG_dataset_ML.xlsx` | Dataset organizado: hoja `datos` (una fila = una fase de 5 s), `resumen`, `diccionario` |
| `data/metadata.csv` | Una fila por archivo crudo |
| `data/raw/` | Señales crudas. **No van en GitHub**: se suben a Azure |
| `notebooks/` | Colab: Delivery 1 (checks), Delivery 2 (ML), dashboard |
| `figures/`, `models/`, `docs/`, `ai-log/` | Figuras, modelo entrenado, entregas, bitácora de IA (Track B) |

## Cómo correr

```bash
pip install -r requirements.txt
```

Firmware: copia `firmware/secrets.example.h` dentro de la carpeta del sketch como `secrets.h` y pon ahí tus valores. `secrets.h` está en `.gitignore` y nunca se sube.

La llave de Azure va en la variable de entorno `AZURE_STORAGE_CONNECTION_STRING` (o en un `.env`, que también está ignorado).

## Avisos honestos sobre la señal

- **El sensor ya rectifica y suaviza la señal.** Lo que leemos en A0 no es el EMG crudo bipolar, sino su envolvente. Por eso el `offset_v` de la metadata es el nivel del sensor en reposo (≈ 0 V) y no la mitad de Vref.
- **MNF y MDF** (features de frecuencia) se calculan sobre esa envolvente, así que **no representan el espectro real del EMG** (20–450 Hz). Solo las usamos si de verdad separan las clases, y lo reportamos tal cual.
- Las ventanas deslizantes del firmware (RMS de 200 ms, MAV de 400 muestras y filtro de 6 Hz) arrastran valores de la fase anterior al inicio de cada fase. Ver `docs/DELIVERY2.md`.

## Resultados

Ver [`docs/DELIVERY1.md`](docs/DELIVERY1.md) y [`docs/DELIVERY2.md`](docs/DELIVERY2.md).

| | Accuracy en sesión nueva (S3) |
|---|---|
| Piso de azar | 50 % |
| Piso de umbral (MAV > 45.2 mV) | 100 % |
| kNN (k = 3, MAV + RMS) | 100 % |
| Demo en vivo | _pendiente_ (aciertos / intentos) |
