#include <BleKeyboard.h>
BleKeyboard bleKeyboard("HERMES", "In-NovaBio", 67);  //NOMBRE, EMPRESA Y SIX SEVEN

const int Muscular_signal = 34;                       //definicion del pin D34 analogico en esp32
const int sonsorial_output = 25;                      //feedback sensorial
const int Lo_neg = 26;                                //Lo- conectado (amarilo)
const int Lo_pos = 27;                                // Lo+ desconetdo (rojo)

const unsigned long time_of_detection = 350;          // aprox 100 lecturas de validacion
const unsigned long cooldown_time = 500;              //tiempo de bloqueo entre clicks

int val_force = 0;                                    //variable para almacenar los datos analogicos
int prev_value = 0;                                   //registro del valor previo
int promedial = 0;                                    //valor promediado detector de disparos cortos
int peaks = 0;                                        //contador de picos de la señal multiples indican un click

int trigger_min_value = 125;                          //balor bace del disparo
int out_of_value_threshold = 2000;                    //valor para medir cambios no naturales de disparo

float average_rest = 0;                               //promedio musculo relajado
float average_wink = 0;                               //promedio musculo guiño
float averageSpikes = 0;                              //promedio picos de señal


bool spasm_dt = false;    //deteccion de espasmos
bool evaluation = false;  //variable para abrir el escaneo por 500 ms donde se validara el click

unsigned long fst_detection = 0; //variable timer validation detection
unsigned long last_event_time = 0;  //timer medidor de activacion de evento previo

void setup() {
  Serial.begin(115200);
  pinMode(sonsorial_output, OUTPUT);
  pinMode(Lo_neg, INPUT);
  pinMode(Lo_pos, INPUT);
  Serial.println("Iniciando BLE Keyboard...");
  bleKeyboard.begin();  // iniciar Blek
  while (!bleKeyboard.isConnected()) {
    Serial.println("Esperando BLE...");
    delay(100);
  }
  if (!body_conection(digitalRead(Lo_neg), digitalRead(Lo_pos))) {  //buscar estar bien conectado
    if (bleKeyboard.isConnected()) {
      calibration();
    }
    else {
      Serial.println("Sin coneccion a bluetooth");
    }
  }
}

bool body_conection(bool lon, bool lop) {  //verificador de coneccion corporal correcta Lo+ y Lo-
  if (lon) {
    //Serial.println("ELECTRODO amarillo DESCONECTADO");
    //delay(500);
    return false;  //▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄CAMBIAR A TRUE SOLO PROVICIONAL
  } else if (lop) {
    //Serial.println("ELECTRODO ROJO DESCONECTADO");
    //delay(500);
    return false;  //▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄CAMBIAR A TRUE SOLO PROVICIONAL
  } else {
    return false;
  }
}

bool trigger_detector() {  //validacion separacion espasmo y movimiento
  val_force = analogRead(Muscular_signal);
  promedial = abs(val_force - prev_value);
  prev_value = val_force;

  if (promedial >= trigger_min_value) { //se busca ver si se supera el umbral minimo para accionar disparo
    if (promedial >= out_of_value_threshold) {  //se busca comparar si es demaciado grande como para ser causado por un musculo
      spasm_dt = true;
      return false;  // no se evalua nada y sigue buscando
    }
    if (!evaluation) {//empieza la evaluacion si es valido
      fst_detection = millis();//toma medida de tiempo
      evaluation = true;
      peaks = 1;
    } else {
      peaks++;  //considera los picos
    }
    return true;
  }
  return false;
}

bool trigger_validation() {
  if ((millis() - fst_detection > time_of_detection) && evaluation) {  //evalua el tiempo de analisis
    if (millis() - last_event_time < cooldown_time) {                  //evalua el tiempo de espera
      evaluation = false;
      peaks = 0;
      spasm_dt = false;
      return false;
    }

    if ((peaks >= 1 && peaks <= averageSpikes-1) && !spasm_dt) {  //evaluar los picos necesarios para enter
      Serial.println("EVENTO VALIDOOOOOOOOOOOOOOOOOOOOOOOO");
      bleKeyboard.print("hola");
      bleKeyboard.write(KEY_RETURN);
      digitalWrite(sonsorial_output, HIGH);
      delay(75);
      digitalWrite(sonsorial_output, LOW);
      last_event_time = millis();
    } else {
      Serial.println("EVENTO RECHAZADO");
    }
    evaluation = false;
    spasm_dt = false;
    peaks = 0;
    return true;
  }
  return false;
}

void calibration(){ //metodo de calibracion
  Serial.println("Calibrando..... mantenga el musculo relajado");
  digitalWrite(sonsorial_output, HIGH);

  unsigned long CALIBRATION_TIME = millis();
  int cont = 0;

  prev_value = analogRead(Muscular_signal);
  while (millis() - CALIBRATION_TIME < 5000) {
    val_force = analogRead(Muscular_signal);
    promedial = abs(val_force - prev_value);
    prev_value = val_force;
    average_rest += promedial;
    cont++;
    delay(5);
  }
  average_rest/=cont;
  Serial.print("PROMEDIO REPOSO: ");
  Serial.println(average_rest);
  delay(2000);

  for(int x = 0;x<=5;x+=1){
    digitalWrite(sonsorial_output, HIGH);
    delay(200);
    digitalWrite(sonsorial_output, LOW);

    int cicles = 0;
    int wink_val = 0;

    unsigned long CALIBRATION_WINK = millis();

    while (millis() - CALIBRATION_WINK < 400) {
      val_force = analogRead(Muscular_signal);
      promedial = abs(val_force - prev_value);
      prev_value = val_force;
      wink_val += promedial;
      cicles++;

      if (promedial > trigger_min_value && promedial < out_of_value_threshold ) {
        averageSpikes++;
      }
      delay(5);
    }
    average_wink += (float)wink_val / cicles;
    delay(1000);
  }
  average_wink /= 5;
  averageSpikes /= 5;

  Serial.print("Disparo promedio: ");
  Serial.println(average_wink);

  Serial.print("Picos promedio: ");
  Serial.println(averageSpikes);
  delay(2000);

  out_of_value_threshold = average_wink * 2; //aca era 2
  trigger_min_value = average_rest + (0.75*(average_wink - average_rest)); // aca era 0.75
}

void loop() {
  if (!body_conection(digitalRead(Lo_neg), digitalRead(Lo_pos))) {  //buscar estar bien conectado
    if (bleKeyboard.isConnected()) {                                //buscar blutu
      trigger_detector();
      trigger_validation();
    } else {
      Serial.println("Sin coneccion a bluetooth");
    }
    Serial.println(promedial);
  }
  delay(5);
}
