#include <Arduino.h>
#include <math.h>
#include <string.h>

#define M_PI 3.14159265358979323846

// ================== ENTRADAS ESP32 ==================
const int senal_muscular = 34;                       //definicion del pin D34 analogico en esp32
const int salida_sensorial = 25;                     //feedback sensorial
const int Lo_neg = 26;                               //Lo- conectado (amarilo)
const int Lo_pos = 27;                               // Lo+ desconetdo (rojo)

/

// Parametros de filtrado
static constexpr float NOTCH_FREQ = 60.0f;      // Hz - frecuencia de la red electrica
static constexpr float NOTCH_Q = 4.0f;          // Factor de calidad del notch
static constexpr float BAND_LOW = 20.0f;        // Hz - limite inferior EMG
static constexpr float BAND_HIGH = 450.0f;      // Hz - limite superior EMG
static constexpr int BUTTER_ORDER = 4;          // Orden del filtro Butterworth
/ DECLARACION DE PARAMETROS DEL SISTEMA
// Parametro de muestreo
static float FS = 1000.0f;
// Parametro de la ventana de segmentacion
static constexpr float RMS_WINDOW_SIZE = 0.2f;  // 200 ms

// Parametros de deteccion binaria (debouncing)
// Se pueden ajustar para mejorar la deteccion de contracciones musculares y la calibracion propuesta
static constexpr float HOLD_TIME_SEC = 0.15f;   // 150 ms de hold time
static constexpr float REST_CALIB_SEC = 2.0f;   // 2 segundos para calibrar 
static constexpr float THRESHOLD_SCALE = 0.20f; // 20% del rango MVC-reposo

// ESTRUCTURAS DE FILTROS IIR DE SEGUNDO ORDEN (BIQUAD)
struct Biquad{
    // Coeficientes del filtro
    float b0, b1, b2; // Coeficientes de la parte de alimentacion (feedforward)
    float a1, a2;     // Coeficientes de la parte de retroalimentacion (feedback)

    // Estados del filtro (memoria de las muestras anteriores)
    float w1 = 0.0f, w2 = 0.0f; 

    // Procesa una muestra de entrada y devuelve la salida filtrada
    // OPTIMIZACION: inline para reducir la sobrecarga de la llamada a la función
    inline float procesar(float x) {
        // Calculo de la salida del filtro usando la estructura de biquad
        float y = b0 * x + w1;      // Salida actual basada en la entrada y el estado anterior
        w1 = b1 * x - a1 * y + w2;  // Actualiza w1 para la proxima muestra
        w2 = b2 * x - a2 * y;       // Actualiza w2 para la proxima muestra
        return y;
    }

    // Resetea los estados del filtro 
    void resetear() { w1 = 0.0f; w2 = 0.0f; }
};

// CASCADA DE BIQUADS PARA FILTROS DE ORDEN SUPERIOR
struct CascadaBiquad {
    static constexpr int MAX_ETAPAS = 2;    // Orden 4 -> 2 biquads
    Biquad etapas[MAX_ETAPAS];              // Array de biquads para la cascada
    int numEtapas = 0;                      // Numero de etapas en la cascada

    float procesar(float x) {
        float y = x;
        for (int i = 0; i < numEtapas; ++i) 
            y = etapas[i].procesar(y);
        return y;   
    }

    void resetear() {
        for (int i = 0; i < numEtapas; i++)
            etapas[i].resetear();
    }
};

// DISENO DE FILTRO NOTCH
Biquad disenarNotch(float fs, float freq, float Q) {
    float w0 = 2.0f * M_PI * freq / fs;     
    float alpha = sin(w0) / (2.0f * Q);     
    float cos_w0 = cos(w0);                 
    float a0 = 1.0f + alpha;                

    Biquad bq;
    bq.b0 = 1.0f / a0;                      
    bq.b1 = -2.0f * cos_w0 / a0;            
    bq.b2 = 1.0f / a0;                     
    bq.a1 = -2.0f * cos_w0 / a0;            
    bq.a2 = (1.0f - alpha) / a0;            
    return bq;
}

// DISENO DE FILTRO BUTTERWORTH BANDPASS DE ORDEN 4 (2 SECCIONES BIQUAD)
void disenarBandpass(float lowHz, float highHz, float fs, CascadaBiquad& cascada) {
    cascada.numEtapas = 2;

    {
        float wc = 2.0f * M_PI * lowHz / fs;
        float k = tan(wc / 2.0f);
        float k2 = k * k;
        float sqrt2 = sqrt(2.0f);
        float a0 = 1.0f + sqrt2 * k + k2;

        Biquad& bq = cascada.etapas[0];
        bq.b0 = 1.0f / a0;      
        bq.b1 = -2.0f / a0;     
        bq.b2 = 1.0f / a0;      
        bq.a1 = (2.0f * (k2 - 1.0f)) / a0;   
        bq.a2 = (1.0f - sqrt2 * k + k2) / a0;
    }

    {
        float wc = 2.0f * M_PI * highHz / fs;
        float k = tan(wc / 2.0f);
        float k2 = k * k;
        float sqrt2 = sqrt(2.0f);
        float a0 = 1.0f + sqrt2 * k + k2;

        Biquad& bq = cascada.etapas[1];
        bq.b0 = k2 / a0;
        bq.b1 = 2.0f * k2 / a0;
        bq.b2 = k2 / a0;
        bq.a1 = (2.0f * (k2 - 1.0f)) / a0;
        bq.a2 = (1.0f - sqrt2 * k + k2) / a0;
    }
}

// BUFFER CIRCULAR PARA LA ENVOLVENTE RMS
struct BufferRMS {
    static constexpr int MAX_SIZE = 2048;
    float buf[MAX_SIZE] = {};
    int cabeza = 0;
    int tamVentana = 0;
    double sumaCuadrados = 0.0;

    void init(int muestrasVentana) {
        tamVentana = muestrasVentana;
        cabeza = 0;
        sumaCuadrados = 0.0;
        memset(buf, 0, sizeof(float) * tamVentana);
    }

    float actualizar(float muestra) {
        float nuevo = muestra * muestra;
        sumaCuadrados -= buf[cabeza];
        sumaCuadrados += nuevo;
        buf[cabeza] = nuevo;
        cabeza = (cabeza + 1) % tamVentana;
        return sqrt(sumaCuadrados / tamVentana);
    }
};

// ESTADO GLOBAL DEL PIPELINE
struct PipelineEMG {
    Biquad filtroNotch;
    CascadaBiquad filtroBandpass;
    BufferRMS bufferRMS;

    bool calibrado = false;
    float umbral = 0.0f;
    float nivelReposo = 0.0f;
    float maxMVC = 0.0f;

    int contadorHold = 0;
    int muestrasHold = 0;

    double sumaCalib = 0.0;
    int cuentaCalib = 0;
    int muestrasCalib = 0;

    void init(float fs) {
        filtroNotch = disenarNotch(fs, NOTCH_FREQ, NOTCH_Q);
        disenarBandpass(BAND_LOW, BAND_HIGH, fs, filtroBandpass);

        int muestrasVentana = (int)(RMS_WINDOW_SIZE * fs);
        bufferRMS.init(muestrasVentana);

        muestrasHold = (int)(HOLD_TIME_SEC * fs);
        muestrasCalib = (int)(REST_CALIB_SEC * fs);

        calibrado = false;
        sumaCalib = 0.0;
        cuentaCalib = 0;
        contadorHold = 0;
        maxMVC = 0.0f;
    }
};

PipelineEMG pipeline;

// PROCESAMIENTO DE UNA MUESTRA (PIPELINE COMPLETO)
int procesarMuestra(float muestraCruda, float offsetDC, float& salidaFiltrada, float& envolvente) {

    float x = muestraCruda - offsetDC;

    float xNotch = pipeline.filtroNotch.procesar(x);
    float xBand = pipeline.filtroBandpass.procesar(xNotch);

    salidaFiltrada = xBand;

    float env = pipeline.bufferRMS.actualizar(xBand);
    envolvente = env;

    if (!pipeline.calibrado) {
        pipeline.sumaCalib += env;
        pipeline.cuentaCalib++;

        if (pipeline.cuentaCalib >= pipeline.muestrasCalib) {
            pipeline.nivelReposo = pipeline.sumaCalib / pipeline.cuentaCalib;
            pipeline.umbral = pipeline.nivelReposo * (1.0f + THRESHOLD_SCALE);
            pipeline.calibrado = true;
        }
        return -1;
    }

    int deteccion = (env > pipeline.umbral) ? 1 : 0;
    int salida;

    if(deteccion == 1) {
        salida = 1;
        pipeline.contadorHold = pipeline.muestrasHold;
        if(env > pipeline.maxMVC) pipeline.maxMVC = env;
    } else if(pipeline.contadorHold > 0) {
        salida = 1;
        pipeline.contadorHold--;
    } else {
        salida = 0;
    }

    return salida;
}

// SETUP 

float offsetDC = 0;
int contadorDC = 0;

void setup() {
    Serial.begin(115200);

    pinMode(salida_sensorial, OUTPUT);
    pinMode(Lo_neg, INPUT);
    pinMode(Lo_pos, INPUT);

    pipeline.init(FS);

    Serial.println("[INICIO] Sistema EMG ESP32");
}

// LOOP 

void loop() {

    if (/*digitalRead(Lo_neg) || digitalRead(Lo_pos)*/false) {
        Serial.println("Electrodos desconectados!");
        return;
    }

    float cruda = analogRead(senal_muscular);

    if (contadorDC < 1000) {
        offsetDC += cruda;
        contadorDC++;
        if (contadorDC == 1000) {
            offsetDC /= 1000.0;
            Serial.println("[INFO] Offset DC calibrado");
        }
        return;
    }

    float filtrada = 0, envolvente = 0;
    int resultado = procesarMuestra(cruda, offsetDC, filtrada, envolvente);

    if (resultado >= 0) {
        Serial.print(cruda); Serial.print(",");
        Serial.print(filtrada); Serial.print(",");
        Serial.print(envolvente); Serial.print(",");
        Serial.println(resultado);

        digitalWrite(salida_sensorial, resultado);
    }

    delayMicroseconds(1000000 / FS);
}