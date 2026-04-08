#include <BleKeyboard.h>
BleKeyboard bleKeyboard("HERMES", "In-NovaBio", 67);  //NOMBRE, EMPRESA Y SIX SEVEN

const int Muscular_signal = 34;                       //definicion del pin D34 analogico en esp32
const int sonsorial_output = 25;                      //feedback sensorial
const int Lo_neg = 26;                                //Lo- conectado (amarilo)
const int Lo_pos = 27;                                // Lo+ desconetdo (rojo)

const unsigned long time_of_detection = 400;          // aprox 100 lecturas de validacion
const unsigned long cooldown_time = 1000;              //tiempo de bloqueo entre clicks

int val_force = 0;                                    //variable para almacenar los datos analogicos
int prev_value = 0;                                   //registro del valor previo
int promedial = 0;                                    //valor promediado detector de disparos cortos
int peaks = 0;                                        //contador de picos de la señal multiples indican un click
int meditions_count = 0;                              //conteo de muestras

int trigger_min_value = 125;                          //balor bace del disparo
int out_of_value_threshold = 2000;                    //valor para medir cambios no naturales de disparo

float average_rest = 0;                               //promedio musculo relajado
float average_wink = 0;                               //promedio musculo guiño
float averageSpikes = 0;                              //promedio picos de señal
float average_force = 0;                              //promedio de fuerza
float promedial_medition = 0;                         //promedio por lectura
float average_rest_rms = 0;                           //Rms bace
float average_force_rms = 0;                          //RMS activo
float rms_accumulator = 0;                            //RMS PARA EVALUAR
float average_wink_rms = 0;                           //RMS PARA CONTRACCIONES

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
  rms_accumulator = promedial * promedial;

  if (promedial >= trigger_min_value) { //se busca ver si se supera el umbral minimo para accionar disparo
    if (promedial >= out_of_value_threshold) {  //se busca comparar si es demaciado grande como para ser causado por un musculo
      spasm_dt = true;
      return false;  // no se evalua nada y sigue buscando
    }
    if (!evaluation) {//empieza la evaluacion si es valido
      fst_detection = millis();//toma medida de tiempo
      evaluation = true;
      peaks = 1;
      meditions_count = 1;
      promedial_medition = promedial;
    } else {
      peaks++;  //considera los picos
      promedial_medition += promedial;
      rms_accumulator += promedial * promedial;
      meditions_count++;
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
      promedial_medition = 0;
      meditions_count = 0;
      return false;
    }

    float rms = sqrt(rms_accumulator / meditions_count);
    float normalized = (rms - average_rest_rms) / (average_wink_rms - average_rest_rms);
    /*Serial.println(normalized);*/

    if ((peaks >= 1 && peaks <= averageSpikes) && !spasm_dt) {  //evaluar los picos necesarios para enter
      if((promedial_medition/meditions_count) >= average_wink){
        if(/*normalized > 0.4*/true){
          Serial.println("EVENTO VALIDOOOOOOOOOOOOOOOOOOOOOOOO");
          bleKeyboard.print("a");
          bleKeyboard.write(KEY_RETURN);
          digitalWrite(sonsorial_output, HIGH);
          delay(75);
          digitalWrite(sonsorial_output, LOW);
          last_event_time = millis();
        }
      }
      else {
        Serial.println("EVENTO RECHAZADO promedio");
      }
    }
    else {
      Serial.println("EVENTO RECHAZADO PICOS");
    }
    evaluation = false;
    spasm_dt = false;
    peaks = 0;
    promedial_medition = 0;
    meditions_count = 0;
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
    average_rest_rms += promedial * promedial;
    cont++;
    delay(5);
  }
  average_rest_rms = sqrt(average_rest_rms / cont);
  average_rest/=cont;
  Serial.print("PROMEDIO REPOSO: ");
  Serial.println(average_rest);
  Serial.print("RMS REPOSO: ");
  Serial.println(average_rest_rms);
  digitalWrite(sonsorial_output, LOW);
  delay(2000);

  Serial.println("Calibrando..... mantenga el musculo haciendo fuerza");
  digitalWrite(sonsorial_output, HIGH);

  unsigned long CALIBRATION_TIME2 = millis();
  int cont2 = 0;

  prev_value = analogRead(Muscular_signal);
  while (millis() - CALIBRATION_TIME2 < 5000) {
    val_force = analogRead(Muscular_signal);
    promedial = abs(val_force - prev_value);
    prev_value = val_force;
    average_force += promedial;
    average_force_rms += promedial * promedial;
    cont2++;
    delay(5);
  }
  average_force_rms = sqrt(average_force_rms / cont2);
  average_force/=cont2;
  Serial.print("PROMEDIO FUERZA: ");
  Serial.println(average_force);
  Serial.print("RMS FUERZA: ");
  Serial.println(average_force_rms);
  digitalWrite(sonsorial_output, LOW);
  delay(2000);

  for(int x = 0;x<=5;x+=1){
    digitalWrite(sonsorial_output, HIGH);
    delay(75);
    int cicles = 0;
    int wink_val = 0;
    float rms_temp = 0;

    unsigned long CALIBRATION_WINK = millis();

    while (millis() - CALIBRATION_WINK < 400) {
      val_force = analogRead(Muscular_signal);
      promedial = abs(val_force - prev_value);
      prev_value = val_force;
      wink_val += promedial;
      rms_temp += promedial * promedial;
      cicles++;

      if (promedial > trigger_min_value && promedial < out_of_value_threshold ) {
        averageSpikes++;
      }
      delay(5);
    }
    digitalWrite(sonsorial_output, LOW);
    average_wink += (float)wink_val / cicles;
    average_wink_rms += sqrt((float)rms_temp / cicles);;
    delay(1000);
  }
  average_wink /= 5;
  averageSpikes /= 5;
  average_wink_rms /= 5;

  Serial.print("Disparo promedio: ");
  Serial.println(average_wink);

  Serial.print("Picos promedio: ");
  Serial.println(averageSpikes);

  Serial.print("RMS PROM: ");
  Serial.println(average_wink_rms);
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
      Serial.print("Disparo promedio: ");
      Serial.println(average_wink);
      Serial.print("Picos promedio: ");
      Serial.println(averageSpikes);
      Serial.print("PROMEDIO FUERZA: ");
      Serial.println(average_force);
      Serial.print("PROMEDIO REPOSO: ");
      Serial.println(average_rest);
      Serial.print("RMS FUERZA: ");
      Serial.println(average_force_rms);
      Serial.print("RMS REPOSO: ");
      Serial.println(average_rest_rms);
      Serial.print("RMS PROM: ");
      Serial.println(average_wink_rms);

      delay(8000);
    }
    Serial.print(promedial);
    Serial.print(",");
    Serial.print(promedial_medition/meditions_count);
    Serial.print(",");
    Serial.print(sqrt(rms_accumulator / meditions_count));
    Serial.print(",");
    Serial.print(average_wink);
    Serial.print(",");
    Serial.print(average_wink_rms);
    Serial.print(",");
    Serial.print(average_rest);
    Serial.print(",");
    Serial.print(average_rest_rms);
    Serial.print(",");
    Serial.print(average_force);
    Serial.print(",");
    Serial.println(average_force_rms);
  }
  delay(5);
}