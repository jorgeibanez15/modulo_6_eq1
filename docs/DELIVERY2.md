# Delivery 2: movimiento vs. reposo con ML

## Las 2 features

`mav_mV` y `rms_mV`, ambas de tiempo, calculadas por el firmware y promediadas por fase de 5 s.
Su correlación es ≈ 0.999, así que RMS casi no le agrega información a MAV. Cuando haya datos crudos se probará MAV + MNF (una de tiempo y una de frecuencia).

## Cómo probamos (dos frases)

Entrenamos solo con las sesiones S1 y S2 y probamos con S3, una sesión nueva con los electrodos quitados y puestos de nuevo.
El umbral y el valor de k se eligieron sin mirar S3.

## Resultados en la sesión nueva (S3)

| | Accuracy |
|---|---|
| Piso de azar (clase mayoritaria) | 50 % |
| Piso de umbral (MAV > 45.2 mV, elegido con entrenamiento) | 100 % |
| kNN (StandardScaler + k = 3) | 100 % |

Matriz de confusión: `figures/confusion_matrix.png` _(se genera con `src/train.py`)_.

kNN empata con el piso de umbral porque en MAV el reposo y la contracción no se traslapan: una sola regla ya separa todo.

## Demo en vivo

_pendiente_: aciertos / intentos.

## Nota de equidad

Un 60 % en una prueba justa (sesión nueva, sin fuga de datos) vale más que un 99 % con datos filtrados.
