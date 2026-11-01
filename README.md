# Clasificador de monedas colombianas con ESP32-S3 + cámara

Sistema que **reconoce monedas colombianas** (50, 100, 200, 500 y 1000, nuevas y antiguas) con la cámara de una **ESP32-S3**, y **guía cada moneda hacia su caja** con un servo sobre una banda transportadora.

- La **ESP32-S3** transmite el video por WiFi, entrega sus datos en tiempo real y mueve el servo.
- Una **página web** (un solo archivo HTML) recibe el video, **reconoce la moneda con inteligencia artificial en el navegador**, muestra la telemetría de la ESP y le manda el resultado.

---

## 1. Cómo funciona (visión general)

```
 ┌──────────────────────────┐        WiFi (misma red)        ┌─────────────────────────────┐
 │  ESP32-S3 + cámara       │  ───── video MJPEG :81 ──────▶ │  Página web (navegador PC)  │
 │                          │  ───── /status (JSON) :80 ───▶ │                             │
 │  - Captura imágenes      │                                │  - MobileNet: convierte la  │
 │  - Servidor HTTP         │ ◀──── /cmd?c=A&d=100&t=N ───── │    imagen en números        │
 │  - Mueve el servo        │ ◀──── /servo?a=90 ──────────── │  - KNN: decide qué moneda es│
 │  - Pulsos A / R          │                                │  - Muestra datos de la ESP  │
 └──────────────────────────┘                                └─────────────────────────────┘
```

**Flujo de una moneda:**

1. La cámara de la ESP32 ve la moneda y envía el video al navegador.
2. El navegador toma un fotograma, lo convierte en un vector de números (MobileNet) y lo compara con los ejemplos que entrenaste (KNN).
3. Si la moneda se reconoce con suficiente seguridad, el navegador manda a la ESP `A` (aceptada) más la denominación y el tipo. Si no, manda `R` (rechazada).
4. La ESP activa su pin de salida, **espera un tiempo mientras la moneda avanza por la banda**, mueve el servo al ángulo de esa moneda y luego lo regresa al reposo.

> **Importante:** la ESP32 **no** ejecuta el modelo de inteligencia artificial. Solo captura y transmite. El reconocimiento se hace en el navegador del computador.

---

## 2. Estructura del repositorio

```
clasificador-monedas-esp32s3/
├── README.md
├── .gitignore
├── firmware/
│   └── esp32s3_monedas/
│       └── esp32s3_monedas.ino     ← código de la ESP32-S3 (Arduino)
└── web/
    └── index.html                  ← página: reconocimiento + telemetría
```

---

## 3. Materiales

| Elemento | Detalle |
|---|---|
| Placa | ESP32-S3 con cámara (sensor OV2640 típico) y PSRAM |
| Servo | Un servo tipo SG90 / MG90S (o similar) |
| Fuente del servo | 5 V externos, con GND común con la ESP32 |
| Condensador | 470 a 1000 µF entre 5 V y GND del servo |
| Banda transportadora | Con cajas o rampas para cada denominación |
| Iluminación | Fija y uniforme sobre la zona de lectura |
| Computador | Chrome o Edge, en la misma red WiFi que la ESP32 |

### Conexiones

| Señal | Pin ESP32-S3 | Nota |
|---|---|---|
| Servo (señal) | GPIO 14 | Cámbialo en `PIN_SERVO` si usas el modelo de pines B |
| Salida "aceptada" | GPIO 1 | Pulso de 300 ms (LED, relé, etc.) |
| Salida "rechazada" | GPIO 2 | Pulso de 300 ms |
| Servo (alimentación) | 5 V externos | **No** lo alimentes con el 3,3 V de la placa |
| GND | GND común | La fuente del servo y la ESP32 deben compartir GND |

---

## 4. Puesta en marcha paso a paso

### 4.1 Firmware (ESP32-S3)

1. Instala el **Arduino IDE** y, en el gestor de placas, el paquete **esp32 de Espressif versión 3.x**.
2. Abre `firmware/esp32s3_monedas/esp32s3_monedas.ino`.
3. En `Herramientas` selecciona:
   - **Placa:** ESP32S3 Dev Module
   - **PSRAM:** OPI PSRAM (módulos N16R8) o QSPI PSRAM (N8R2 / N4R2)
   - **USB CDC On Boot:** Enabled
   - **Partition Scheme:** 8MB with spiffs (o Huge APP si tu flash es menor)
4. Escribe tu WiFi en `WIFI_SSID` y `WIFI_PASS`.
5. Elige el mapa de pines de tu placa: `CAM_MODELO_A` (Freenove, ESP32-S3-EYE y la mayoría de genéricas) o `CAM_MODELO_B`.
6. Sube el código y abre el **Monitor Serie a 115200**. Allí aparece la **IP** de la ESP (por ejemplo `192.168.1.50`).

> **No subas tu contraseña real de WiFi a GitHub.** Deja `TU_WIFI` y `TU_CLAVE` en el repositorio.

### 4.2 Página web

1. Abre `web/index.html` con **doble clic** en Chrome o Edge (como archivo local).
2. Escribe la IP de la ESP y pulsa **Conectar ESP32**.
3. Entrena las clases (sección 7.3) y pulsa **Reconocer ahora** o activa el **modo automático**.

> Debe abrirse como archivo local o desde `http://`. Si lo publicas en una página `https://`, el navegador bloquea el video porque la ESP sirve por `http://` (contenido mixto).

---

## 5. El firmware, parte por parte (`esp32s3_monedas.ino`)

### 5.1 Librerías

| Librería | Para qué sirve |
|---|---|
| `esp_camera.h` | Maneja el sensor de la cámara y entrega fotogramas JPEG |
| `WiFi.h` | Conexión a la red WiFi |
| `ESPmDNS.h` | Permite entrar también como `monedas.local` |
| `esp_http_server.h` | Servidor HTTP integrado de la ESP32 |

### 5.2 Parámetros de configuración (arriba del archivo)

| Parámetro | Qué hace |
|---|---|
| `WIFI_SSID`, `WIFI_PASS` | Datos de tu red |
| `PIN_ACEPTAR`, `PIN_RECHAZAR`, `PULSO_MS` | Pines de salida y duración del pulso al recibir `A` o `R` |
| `FPS_MAX` | Máximo de cuadros por segundo que se envían al navegador |
| `CAM_XCLK_HZ` | Reloj del sensor. Más bajo = menos calor |
| `CPU_MHZ` | Frecuencia del procesador. Más bajo = menos calor |
| `JPEG_CALIDAD` | Compresión: 10 = mejor imagen, 63 = peor. Más alto = menos trabajo |
| `TX_WIFI` | Potencia de transmisión WiFi |
| `USAR_SERVO` | `1` activa el servo, `0` lo desactiva y todo queda como antes |
| `PIN_SERVO` | Pin de señal del servo |
| `SERVO_US_MIN` / `SERVO_US_MAX` | Pulsos (µs) que corresponden a 0° y 180° |
| `SERVO_REPOSO` | Grados de la posición de espera |
| `SERVO_RECHAZADA` | Grados cuando la moneda se rechaza |
| `SERVO_RETRASO_MS` | Tiempo entre que la cámara ve la moneda y el servo se mueve |
| `SERVO_MANTENER_MS` | Tiempo que el servo permanece en posición antes de volver al reposo |
| `DENOM[]` y `SERVO_ANG[][]` | Tabla de grados por denominación (columnas: nueva, antigua) |
| `CAM_MODELO_A/B` | Mapa de pines de la cámara |
| `VOLTEAR_VERTICAL/HORIZONTAL` | Corrigen la imagen si sale al revés o en espejo |

### 5.3 `iniciarCamara()`

Llena la estructura `camera_config_t` y llama a `esp_camera_init()`:

- **Pines `pin_d0…pin_d7`, `pin_xclk`, `pin_pclk`, `pin_vsync`, `pin_href`, `pin_sccb_*`:** conexiones físicas entre el sensor y la ESP. Cambian según la placa (por eso hay modelos A y B).
- **`xclk_freq_hz`:** reloj que la ESP le da al sensor. El sensor trabaja al ritmo de este reloj, así que bajarlo reduce el calor y el máximo de cuadros por segundo.
- **`ledc_channel` / `ledc_timer`:** la ESP genera ese reloj con un periférico llamado LEDC. Se usan el canal 7 y el timer 3 para que **no choquen con el servo**, que usa los canales bajos.
- **`pixel_format = PIXFORMAT_JPEG`:** el sensor entrega las imágenes ya comprimidas, lo que ahorra memoria y WiFi.
- **`frame_size = FRAMESIZE_VGA`:** 640×480.
- **`jpeg_quality`:** nivel de compresión.
- **`fb_location = CAMERA_FB_IN_PSRAM` y `fb_count = 2`:** los cuadros se guardan en la PSRAM, con dos buffers.
- **`grab_mode = CAMERA_GRAB_LATEST`:** al pedir un cuadro se entrega siempre el más reciente (importante para no mostrar imágenes atrasadas).
- Si la placa no tiene PSRAM, usa un solo buffer en la RAM interna.
- Al final aplica el volteo vertical y horizontal con `set_vflip` y `set_hmirror`.

### 5.4 Control del servo

El servo se controla con una **señal PWM de 50 Hz**. El ancho del pulso define el ángulo:

```
pulso (µs) = SERVO_US_MIN + (SERVO_US_MAX - SERVO_US_MIN) * ángulo / 180
duty       = pulso * 16383 / 20000        // 14 bits de resolución, periodo de 20 000 µs
```

| Función | Qué hace |
|---|---|
| `servoIniciar()` | Configura el PWM del pin del servo con `ledcAttach` y lo lleva al reposo |
| `servoMover(ang)` | Limita el ángulo a 0–180, calcula el pulso y lo escribe con `ledcWrite` |
| `servoEncolar(ang)` | Agrega un evento a una **cola de hasta 8 movimientos**, programado para `ahora + SERVO_RETRASO_MS` |
| `servoPorMoneda(denom, tipo)` | Busca la denominación en `DENOM[]` y encola el ángulo de la tabla (columna 0 = nueva, 1 = antigua) |
| `servoLoop()` | Se ejecuta en `loop()`. Si llegó la hora del siguiente evento, mueve el servo y programa su regreso. Si pasó `SERVO_MANTENER_MS`, lo devuelve al reposo |

**Por qué una cola:** en una banda pueden venir varias monedas seguidas. Cada una se programa con su propia hora, así una moneda nueva no cancela el movimiento de la anterior. No se usa `delay()`, así que el servidor y el video no se bloquean mientras el servo espera.

### 5.5 `streamHandler()`: el video

Envía el video en formato **MJPEG** (`multipart/x-mixed-replace`): una secuencia de imágenes JPEG separadas por un delimitador, que el navegador muestra dentro de una etiqueta `<img>`.

Puntos clave:

- **`Access-Control-Allow-Origin: *`:** sin esta cabecera el navegador **bloquea la lectura de los píxeles** del video, y la IA no podría analizarlo.
- **Límite de FPS sin pausas:** cada cuadro se recoge siempre con `esp_camera_fb_get()`. Si todavía no toca enviar uno, se devuelve y se descarta. Esto limita el WiFi y el calor sin dejar que se acumulen cuadros.
- **`streamActivo`:** una variable que indica si el navegador está viendo el video (se usa en `loop()`, ver 5.9).
- El ciclo termina cuando `httpd_resp_send_chunk` falla, es decir, cuando el navegador cierra la conexión.

### 5.6 `statusHandler()`: datos en tiempo real

Responde en `/status` un JSON que la página consulta cada 0,5 s:

| Campo | Significado |
|---|---|
| `ip` | Dirección IP de la ESP |
| `rssi` | Señal WiFi en dBm (más cerca de 0 = mejor) |
| `uptime` | Segundos desde que se encendió |
| `fps` | Cuadros por segundo realmente enviados |
| `w`, `h` | Resolución del video |
| `heap` | Memoria RAM libre (bytes) |
| `psram` | PSRAM libre (bytes) |
| `acept`, `rech` | Contadores de monedas aceptadas y rechazadas |
| `cmds` | Total de comandos recibidos |
| `ultimo`, `hace` | Último comando (`A`/`R`) y milisegundos desde que llegó |
| `temp` | Temperatura interna del chip en °C |
| `srv` | Ángulo actual del servo |

### 5.7 `cmdHandler()`: recibe el resultado

Atiende `/cmd` con estos parámetros:

| Parámetro | Valores | Significado |
|---|---|---|
| `c` | `A` o `R` | Aceptada o rechazada |
| `d` | 50, 100, 200, 500, 1000 | Denominación (opcional) |
| `t` | `N` o `A` | Moneda nueva o antigua (opcional) |

Ejemplo: `/cmd?c=A&d=500&t=N` → moneda de 500 nueva aceptada.

Lo que hace:

1. Cuenta la moneda y activa el pin de salida por `PULSO_MS`.
2. Si es `A` y viene `d`, encola el movimiento del servo para esa denominación.
3. Si es `R`, encola el movimiento a `SERVO_RECHAZADA`.
4. Imprime el comando por el puerto serie y responde un JSON de confirmación.

Como `d` y `t` son opcionales, un cliente antiguo que solo mande `c=A` o `c=R` sigue funcionando.

### 5.8 `servoHandler()` e `iniciarServidores()`

- **`/servo?a=90`:** mueve el servo directamente a esos grados y lo deja ahí. Sirve para **calibrar** (el deslizador "Probar servo" de la página usa este endpoint).
- **`iniciarServidores()`** arranca **dos servidores HTTP**:
  - Puerto **80:** `/status`, `/cmd` y `/servo`.
  - Puerto **81:** `/stream`.

  Están separados para que el video, que ocupa su conexión de forma continua, no bloquee los datos ni los comandos.

### 5.9 `setup()` y `loop()`

**`setup()`:** baja la CPU a `CPU_MHZ`, inicia el puerto serie, configura los pines de salida, inicia el servo y la cámara, se conecta al WiFi (con `setSleep(false)` para menor latencia y la potencia `TX_WIFI`), activa `monedas.local`, arranca los servidores e imprime la IP.

**`loop()`** repite continuamente:

1. `servoLoop()`: avanza la cola del servo.
2. **Si nadie ve el video**, recoge y devuelve un cuadro (`esp_camera_fb_get` / `fb_return`) para que los buffers de la cámara no se llenen.
3. Calcula los FPS reales una vez por segundo.
4. Apaga los pines `A`/`R` cuando termina su pulso.
5. Reconecta el WiFi si se cae.

---

## 6. Endpoints de la ESP32

| URL | Puerto | Descripción |
|---|---|---|
| `http://IP:81/stream` | 81 | Video MJPEG |
| `http://IP/status` | 80 | JSON con datos en tiempo real |
| `http://IP/cmd?c=A&d=100&t=N` | 80 | Recibe el resultado de una moneda |
| `http://IP/servo?a=90` | 80 | Mueve el servo a unos grados (calibración) |

Se pueden probar escribiéndolos en el navegador.

---

## 7. La página web, parte por parte (`web/index.html`)

### 7.1 Librerías (cargadas desde CDN)

| Librería | Papel |
|---|---|
| **TensorFlow.js** | Motor de IA que corre en el navegador |
| **MobileNet v2** | Red neuronal **ya entrenada** que convierte una imagen en un vector de números (huella) que describe lo que ve |
| **KNN Classifier** | Clasificador de "vecinos más cercanos": compara la huella actual con las de tus ejemplos |

Como se cargan desde internet, el computador necesita conexión la primera vez.

### 7.2 Cómo se "lee" una moneda (el núcleo del sistema)

**Paso 1: la imagen entra al modelo y sale una huella.**

```js
// v es la etiqueta <img> que muestra el video de la ESP32
const huella = net.infer(v, true);   // true = devolver la "embedding" (el vector), no la clasificación de MobileNet
```

`net` es MobileNet v2. Con `true` entrega un vector de números que describe la imagen. MobileNet **no se vuelve a entrenar**: solo se usa para convertir imágenes en números comparables.

**Paso 2: guardar ejemplos (entrenamiento).**

```js
tf.tidy(() => knn.addExample(net.infer(v, true), "100 nueva"));
```

Cada foto que tomas con el botón "+" guarda su huella asociada a una etiqueta. `tf.tidy` libera la memoria de los tensores temporales. **El KNN no "aprende" pesos: simplemente almacena los ejemplos.**

**Paso 3: predecir.**

```js
const e = tf.tidy(() => net.infer(v, true));
const r = await knn.predictClass(e, 5);   // k = 5 vecinos
e.dispose();
return { l: r.label, c: r.confidences[r.label] };   // etiqueta y confianza
```

El KNN busca los **5 ejemplos más parecidos** a la huella actual y vota. La **confianza** es la fracción de esos 5 vecinos que coinciden con la etiqueta ganadora (0,2 / 0,4 / 0,6 / 0,8 / 1,0).

**Paso 4: decidir si se acepta.**

```js
const UMBRAL = 0.7;
const ok = etiqueta !== OTRO && confianza >= UMBRAL;
```

Se acepta solo si la clase **no** es "Otro (rechazar)" y la confianza es de **al menos 0,7**. Con k = 5 eso equivale a que **por lo menos 4 de los 5 vecinos** coincidan en el caso de una sola lectura.

### 7.3 Clases y entrenamiento

Las clases se generan en el código:

```js
const VACIA = "Banda vacía", OTRO = "Otro (rechazar)";
const CLASES = [VACIA, OTRO, ...[50,100,200,500,1000].flatMap(v => [v+" nueva", v+" antigua"])];
```

Son **12 clases**: banda vacía, otro y las 10 monedas (5 denominaciones × nueva/antigua).

| Clase | Para qué sirve |
|---|---|
| **Banda vacía** | Le enseña al sistema cómo se ve la banda sin moneda. En modo automático indica cuándo puede llegar una moneda nueva |
| **Otro (rechazar)** | Objetos que no son monedas válidas: cuero, cartón, monedas extranjeras |
| **Cada moneda** | Fotos de la moneda, cara y sello |

**Cómo entrenar:** pon la moneda bajo la cámara y **mantén pulsado el botón "+"** de su clase. Mientras lo mantienes, toma una foto cada 200 ms. Se recomiendan **20 a 40 fotos por clase**, moviendo y girando la moneda. Cada foto aparece como miniatura.

> Entrena con **la misma cámara, altura e iluminación** que usarás en la banda. Si cambias la luz o la posición, el reconocimiento empeora.

### 7.4 "Reconocer ahora"

Hace **5 lecturas** separadas por 120 ms y decide por **voto mayoritario**. La confianza final es el promedio de las lecturas de la clase ganadora. Si gana "Banda vacía", muestra "No veo ninguna moneda". Reduce errores de una lectura aislada.

### 7.5 Modo automático

Lee el video cada 150 ms y usa una pequeña máquina de estados:

```js
function paso(r){
  if (r.l === VACIA) { armed = true; cand = null; n = 0; return; }   // banda vacía: se "arma" para la próxima moneda
  if (!armed) return;                                                // ya se leyó esta moneda, esperar a que salga
  if (r.l === cand) n++; else { cand = r.l; n = 1; }                 // cuenta lecturas consecutivas iguales
  if (n >= 3) { armed = false; decidir(r.l, r.c); }                  // 3 iguales seguidas: se decide una sola vez
}
```

- Cada moneda se lee **una sola vez**, incluso si pasa varios segundos bajo la cámara.
- Exige **3 lecturas iguales seguidas** para evitar falsos positivos.
- Se vuelve a "armar" cuando la cámara ve la banda vacía. Por eso **hay que entrenar "Banda vacía"**.

### 7.6 `decidir()` y `send()`: resultado hacia la ESP

`decidir()` aplica el umbral, muestra el resultado, ilumina el recuadro del video (verde = aceptada, rojo = rechazada), actualiza el conteo y llama a `send()`:

```js
function send(c, clase){
  if (writer) writer.write(new TextEncoder().encode(c + "\n"));      // por USB (opcional), solo "A" o "R"
  const ex = (c === "A" && clase)
    ? "&d=" + parseInt(clase) + "&t=" + (clase.includes("antigua") ? "A" : "N")
    : "";
  if (base) fetch(base + "/cmd?c=" + c + ex);                        // por WiFi, con denominación y tipo
}
```

`parseInt("100 nueva")` da `100`: así se extrae la denominación del nombre de la clase.

### 7.7 Telemetría y panel de la ESP

`telemetria()` consulta `/status` cada 500 ms y dibuja el panel con IP, señal WiFi, tiempo encendida, FPS, resolución, **temperatura del chip** (amarillo desde 65 °C, rojo desde 75 °C), memoria, aceptadas/rechazadas, ángulo del servo y último comando. Si falla 3 veces seguidas, muestra "Sin conexión con la ESP32". El **registro** va anotando los eventos con la hora.

### 7.8 Conexión del video

```js
function abrirStream(){ v.src = base + ":81/stream?t=" + Date.now(); }
v.onerror = () => { retry = setTimeout(abrirStream, 2000); };   // si se corta, reintenta cada 2 s
```

La etiqueta `<img crossorigin="anonymous">` permite leer los píxeles porque la ESP envía `Access-Control-Allow-Origin: *`. La IP escrita se recuerda en el navegador (`localStorage`).

### 7.9 Guardar y cargar el entrenamiento

- **Guardar:** convierte los ejemplos del KNN a JSON y los guarda en `localStorage` del navegador.
- **Cargar:** reconstruye los tensores y los devuelve al KNN.
- **Borrar todo:** vacía los ejemplos, con confirmación.

> `localStorage` tiene un límite cercano a 5 MB. Si tienes muchísimas fotos, "Guardar" puede fallar; en ese caso usa menos fotos por clase. Los datos quedan **en ese navegador y ese computador**.

### 7.10 Servo desde la página

El deslizador **"Probar servo"** llama a `/servo?a=GRADOS` mientras lo mueves, para encontrar los ángulos de cada caja.

### 7.11 USB opcional

El botón de USB usa Web Serial (solo Chrome/Edge en PC). Permite mandar `A`/`R` por cable a otra placa y mostrar en el registro lo que imprime la ESP por el puerto serie.

---

## 8. Calibrar el servo

1. Conecta la página y mueve el deslizador **Probar servo**.
2. Anota los grados en los que una moneda que cae llega a **cada caja**.
3. Escribe esos valores en `SERVO_ANG[][]` y en `SERVO_RECHAZADA`.
4. Ajusta `SERVO_RETRASO_MS` con una moneda real: es el tiempo que tarda en ir **del lente al servo**. Si el servo se mueve antes de tiempo, auméntalo; si llega tarde, redúcelo.
5. Ajusta `SERVO_MANTENER_MS` para que dé tiempo a que la moneda pase antes de volver al reposo.

Si necesitas separar monedas nuevas y antiguas, usa valores distintos en las dos columnas de `SERVO_ANG[][]`.

---

## 9. Problemas frecuentes

| Problema | Causa probable | Solución |
|---|---|---|
| La cámara se calienta y la imagen se ve **verde** | Calor del sensor, mala alimentación o cinta plana floja | Bajar `FPS_MAX`, `CAM_XCLK_HZ` y `CPU_MHZ`; usar un buen cable y fuente de 5 V / 1 A o más; revisar la cinta plana; poner un disipador pequeño. Evita hielo o agua por la condensación |
| `cam_hal: FB-OVF` en el Monitor Serie | La cámara produce cuadros más rápido de lo que se recogen | Ya corregido: el firmware recoge siempre cada cuadro (descarta los sobrantes) y vacía los buffers cuando no hay nadie viendo. Si aparece de forma ocasional, sube `JPEG_CALIDAD` (por ejemplo a 20) para reducir el tamaño de cada cuadro |
| La ESP se reinicia al mover el servo | Caída de voltaje por el consumo del servo | Fuente propia de 5 V, GND común y condensador de 470–1000 µF |
| "Camara fallo" en el Monitor Serie | Mapa de pines incorrecto, PSRAM mal configurada o cable de cámara | Probar `CAM_MODELO_B`, revisar la opción de PSRAM y la cinta de la cámara |
| No se ve el video | IP incorrecta, otra red o página en `https://` | Misma red WiFi; abrir el HTML como archivo local; probar `http://IP:81/stream` en el navegador |
| "No se pueden leer los píxeles del video" | El navegador bloquea el video por CORS | Confirmar que usas el firmware de este repositorio (envía `Access-Control-Allow-Origin`) |
| El reconocimiento se equivoca | Poca variedad de fotos o cambios de luz | Más fotos por clase, cara y sello, en distintas posiciones; entrenar con la luz definitiva; entrenar "Banda vacía" y "Otro" |
| El servo apunta mal | Ángulos sin calibrar | Sección 8 |

---

## 10. Ideas para mejorar

- Agregar un sensor infrarrojo en la banda para disparar la lectura en el momento exacto en que llega la moneda.
- Guardar el entrenamiento en un archivo `.json` descargable.
- Usar un segundo servo o un servo por caja si la mecánica lo necesita.
- Registrar el conteo de monedas en una hoja de cálculo.

---

## 11. Licencia

Elige la licencia que prefieras (por ejemplo MIT) y agrega un archivo `LICENSE` al repositorio.
