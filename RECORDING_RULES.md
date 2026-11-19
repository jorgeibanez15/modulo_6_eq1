# Reglas de grabación

Seguir **siempre** el mismo procedimiento para que todas las sesiones sean comparables.

## 1. Sitio de electrodos

- Músculo: `<<sitio>>` (p. ej. flexor de muñeca, antebrazo derecho).
- Electrodos de medición sobre el vientre del músculo, alineados con las fibras. Electrodo de referencia sobre hueso (p. ej. codo).
- Marca la posición con plumón la primera vez, para repetirla en sesiones nuevas.

## 2. Preparación de la piel

1. Limpia con alcohol y deja secar (≈ 30 s).
2. Si hay vello, recórtalo.
3. Electrodos nuevos en cada sesión.

## 3. Protocolo

- Sujeto sentado, brazo apoyado y relajado.
- Orden fijo: **reposo primero, luego movimiento**, alternando: 5 s reposo → 5 s contracción → …
- La LCD indica `RELAJA` / `CONTRAE!!` y la cuenta regresiva.
- Contracción: `<<movimiento, p. ej. cerrar el puño con fuerza moderada constante>>`.
- Mínimo **10 de reposo + 10 de movimiento** por sesión de grabación cruda.

## 4. Sesión nueva (prueba)

- La sesión de prueba se graba **otro día u otra hora, con los electrodos quitados y puestos de nuevo**.
- Sus datos **nunca** se usan para entrenar ni para elegir umbrales o hiperparámetros.
- En el Excel, sus filas llevan `conjunto = prueba`.

## 5. Nombres de archivo (sin nombres de personas)

```
<grupo>_<fecha>_s<n>_<sitio>_<rest|move>_<NN>.csv
p. ej.  G?_2026-11-19_s1_flexor_move_01.csv
        G?_2026-11-19_s1_flexor_rest_01.csv
```

- Una fila por archivo en `data/metadata.csv`. Los campos personales van como `not collected`.
- Los crudos se guardan en `data/raw/` (ignorado por git) y se suben a Azure.

## 6. Privacidad

- Nada de caras en fotos o videos, ni edad, género o medidas corporales.
- Las grabaciones **no se suben a ningún servicio de IA**.
