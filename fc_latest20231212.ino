#include <Wire.h>
#include <Arduino.h>
#include <PPMReader.h>

//analog read to read voltages using esp32
const int analogPin = 25; // Use the desired ADC pin
float referenceVoltage = 3.3; // Reference voltage in volts (may need calibration)
float r1Value = 77600.0; // Resistance of R1 in ohms
float r2Value = 29400.0; // Resistance of R2 in ohms
float battery_voltage=12;

//PWM signal generator pins
const int PWM_PIN_M1 = 5 ; //gpios m1
const int PWM_PIN_M2 = 18; //m2
const int PWM_PIN_M3 = 19 ;//m3
const int PWM_PIN_M4 = 15 ;//m4
//const int PWM_CHANNEL = 0;
const int PWM_FREQUENCY = 250; //hertz
const int PWM_RESOLUTION = 12;  // 12-bit resolution (0-4095)


//PPM signal receiver
//Initialize a PPMReader on digital pin 3 with 6 expected channels.
byte interruptPin = 4;
byte channelAmount = 6;
PPMReader ppm(interruptPin, channelAmount);
int ppmData[6]={0,0,0,0,0,0};

//PPM timeout
//unsigned long lastPPMUpdateTime;
//const unsigned long ppmTimeout = 500000L;


//Code for flight controller
float RateRoll, RatePitch, RateYaw;
float RateCalibrationRoll, RateCalibrationPitch, RateCalibrationYaw;
int   RateCalibrationNumber;
float AccX, AccY, AccZ;
float AngleRoll, AnglePitch;
float LoopTimer;


float DesiredRateRoll, DesiredRatePitch,DesiredRateYaw;
float ErrorRateRoll, ErrorRatePitch, ErrorRateYaw;
float InputRoll, InputThrottle, InputPitch, InputYaw;
float PrevErrorRateRoll, PrevErrorRatePitch, PrevErrorRateYaw;
float PrevItermRateRoll, PrevItermRatePitch, PrevItermRateYaw;
float PIDReturn[]={0, 0, 0};

//PID Rate
float PRateRoll=0.6; float PRatePitch=PRateRoll; float PRateYaw=2;
float IRateRoll=3.5; float IRatePitch=IRateRoll; float IRateYaw=12;
float DRateRoll=0.03; float DRatePitch=DRateRoll; float DRateYaw=0;

float MotorInput1=0, MotorInput2=0, MotorInput3=0, MotorInput4=0;


float KalmanAngleRoll=0, KalmanUncertaintyAngleRoll=2*2;
float KalmanAnglePitch=0, KalmanUncertaintyAnglePitch=2*2;
float Kalman1DOutput[]={0,0};

float DesiredAngleRoll, DesiredAnglePitch;
float ErrorAngleRoll, ErrorAnglePitch;

float PrevErrorAngleRoll, PrevErrorAnglePitch;
float PrevItermAngleRoll, PrevItermAnglePitch;


//PID Angle
float PAngleRoll=1.5; float PAnglePitch=PAngleRoll;//1
float IAngleRoll=0; float IAnglePitch=IAngleRoll;
float DAngleRoll=0.6; float DAnglePitch=DAngleRoll;//0.5

void kalman_1d(float KalmanState,float KalmanUncertainty, float KalmanInput, float KalmanMeasurement) 
{
   KalmanState=KalmanState+0.004*KalmanInput;
   KalmanUncertainty=KalmanUncertainty + 0.004* 0.004 * 4 * 4;
   float KalmanGain=KalmanUncertainty * 1/(1*KalmanUncertainty + 3 * 3);
   KalmanState=KalmanState+KalmanGain * (KalmanMeasurement-KalmanState);
   KalmanUncertainty=(1-KalmanGain) *KalmanUncertainty;
   Kalman1DOutput[0]=KalmanState;
   Kalman1DOutput[1]=KalmanUncertainty;
}


void ppmloop() 
{   
    for (byte channel = 1; channel <= channelAmount; ++channel) 
    {
        ppmData[channel-1] = ppm.latestValidChannelValue(channel, 0);
        Serial.print(ppmData[channel - 1]);
        if(channel < channelAmount) Serial.print('\t');
    }
    Serial.println();
}

void pwmsetup() 
{
   pinMode(PWM_PIN_M1, OUTPUT); 
   pinMode(PWM_PIN_M2, OUTPUT);
   pinMode(PWM_PIN_M3, OUTPUT);
   pinMode(PWM_PIN_M4, OUTPUT);

   //channels are 0,1,2,3 
   //setting the resolution to 12 bits
   ledcSetup(0, PWM_FREQUENCY, PWM_RESOLUTION);
   ledcSetup(1, PWM_FREQUENCY, PWM_RESOLUTION);
   ledcSetup(2, PWM_FREQUENCY, PWM_RESOLUTION);
   ledcSetup(3, PWM_FREQUENCY, PWM_RESOLUTION);

   //set the pin to proper channel
   ledcAttachPin(PWM_PIN_M1, 0);
   ledcAttachPin(PWM_PIN_M2, 1);
   ledcAttachPin(PWM_PIN_M3, 2);
   ledcAttachPin(PWM_PIN_M4, 3);
}

void pwmloop(int motor_input,int PWM_CHANNEL) 
{
   ledcWrite(PWM_CHANNEL, motor_input);
}


float batteryvoltage() 
{
  int rawValue = analogRead(analogPin);
  float voltage = (rawValue / 4095.0) * referenceVoltage;
  
  // Calculate actual input voltage using voltage divider formula
  float inputVoltage = voltage * ((r1Value + r2Value) / r2Value);

  //Serial.print("Raw ADC Value: ");
  //Serial.println(rawValue);
  
  //Serial.print("Input Voltage: ");
  //Serial.print(inputVoltage, 3); // Print voltage with 3 decimal places
  //Serial.println(" V");
  //delay(1); // Wait for a second before taking the next reading
}



void pid_equation(float Error, float P , float I, float D, float PrevError, float PrevIterm) 
{
   float Pterm=P*Error;
   float Iterm=PrevIterm+I*(Error+PrevError)*0.004/2;
   
   if (Iterm > 400) 
       Iterm=400;
   else if (Iterm <-400) 
       Iterm=-400;
   
   float Dterm=D*(Error-PrevError)/0.004;
   float PIDOutput= Pterm+Iterm+Dterm;
   
   if (PIDOutput>400) 
       PIDOutput=400;
   else if (PIDOutput <-400) 
       PIDOutput=-400;
   
   PIDReturn[0]=PIDOutput;
   PIDReturn[1]=Error;
   PIDReturn[2]=Iterm;
}

void reset_pid(void)
{
   PrevErrorRateRoll=0; PrevErrorRatePitch=0; PrevErrorRateYaw=0;
   PrevItermRateRoll=0; PrevItermRatePitch=0; PrevItermRateYaw=0;
   
   PrevErrorAngleRoll=0; PrevErrorAnglePitch=0; //for outer pid loop
   PrevItermAngleRoll=0; PrevItermAnglePitch=0; //-------------------
}

void gyro_signals(void) { 
    Wire.beginTransmission(0x68);
    Wire.write(0x1A);
    Wire.write(0x05);
    Wire.endTransmission();
    Wire.beginTransmission(0x68);
    Wire.write(0x1C);
    Wire.write(0x10);
    Wire.endTransmission();
    Wire.beginTransmission(0x68);
    Wire.write(0x3B);
    Wire.endTransmission();
    Wire.requestFrom(0x68,6);
    int16_t AccXLSB = Wire.read() << 8 |Wire.read();
    int16_t AccYLSB = Wire.read() << 8 |Wire.read();
    int16_t AccZLSB = Wire.read() << 8 |Wire.read();
    Wire.beginTransmission(0x68);
    Wire.write(0x1B);
    Wire.write(0x8);
    Wire.endTransmission();
    Wire.beginTransmission(0x68);
    Wire.write(0x43);
    Wire.endTransmission();
    Wire.requestFrom(0x68,6);
    int16_t GyroX=Wire.read()<<8 | Wire.read();
    int16_t GyroY=Wire.read()<<8 | Wire.read();
    int16_t GyroZ=Wire.read()<<8 | Wire.read();
    RateRoll=(float)GyroX/65.5;
    RatePitch=(float)GyroY/65.5;
    RateYaw=(float)GyroZ/65.5;
    AccX=(float)AccXLSB/4096-0.02;
    AccY=(float)AccYLSB/4096;
    AccZ=(float)AccZLSB/4096-0.08;
    AngleRoll=atan(AccY/sqrt(AccX*AccX+AccZ*AccZ))*1/(3.142/180);
    AnglePitch=-atan(AccX/sqrt(AccY*AccY+AccZ*AccZ))*1/(3.142/180);
}



void setup()  
{
    Serial.begin(115200); 
    pinMode(2, OUTPUT); //lights the led
    analogReadResolution(12); // Set ADC resolution to 12 bits (0-4095)  
    pinMode(analogPin, INPUT);
    Wire.setClock(400000);
    Wire.begin();
    delay(250);
    Wire.beginTransmission(0x68);
    Wire.write(0x6B);
    Wire.write(0x00);
    Wire.endTransmission();
    for(RateCalibrationNumber=0; RateCalibrationNumber<2000; RateCalibrationNumber++)  //gyroscope calibration
    {
        gyro_signals();
        RateCalibrationRoll+=RateRoll;
        RateCalibrationPitch+=RatePitch;
        RateCalibrationYaw+=RateYaw;
        delay(1);
    }
    RateCalibrationRoll/=2000; 
    RateCalibrationPitch/=2000;
    RateCalibrationYaw/=2000;
    pwmsetup();
    LoopTimer=micros();
}

void loop() 
{
   /* if(battery_voltage <= 11)
    {
        digitalWrite(2, HIGH); //light the led
        delay(200);
        digitalWrite(2, LOW);
    }
    else
    {*/
    digitalWrite(2, HIGH);
    gyro_signals();
    
    RateRoll-=RateCalibrationRoll;
    RatePitch-=RateCalibrationPitch;
    RateYaw-=RateCalibrationYaw;
    
    kalman_1d(KalmanAngleRoll, KalmanUncertaintyAngleRoll, RateRoll, AngleRoll);  //Roll
    KalmanAngleRoll=Kalman1DOutput[0];
    KalmanUncertaintyAngleRoll=Kalman1DOutput[1];
    
    kalman_1d(KalmanAnglePitch,KalmanUncertaintyAnglePitch, RatePitch, AnglePitch); //pitch
    KalmanAnglePitch=Kalman1DOutput[0];
    KalmanUncertaintyAnglePitch=Kalman1DOutput[1];

    ppmloop();
    DesiredAngleRoll=0.10*(ppmData[0]-1500); // desired roll
    DesiredAnglePitch=0.10*(ppmData[1]-1500); // desired pitch
   
   /*show angle*/
   
    Serial.print(" Roll Angle [°] ");
    Serial.print(KalmanAngleRoll);
    Serial.print(" Pitch Angle [°] ");
    Serial.println(KalmanAnglePitch);
   
    InputThrottle=ppmData[2];   //desired throttle
    DesiredRateYaw=0.15*(ppmData[3]-1500); //yaw

    ErrorAngleRoll=DesiredAngleRoll-KalmanAngleRoll;
    ErrorAnglePitch=DesiredAnglePitch-KalmanAnglePitch;

    pid_equation(ErrorAngleRoll, PAngleRoll, IAngleRoll, DAngleRoll, PrevErrorAngleRoll, PrevItermAngleRoll);
    DesiredRateRoll=PIDReturn[0];
    PrevErrorAngleRoll=PIDReturn[1];
    PrevItermAngleRoll=PIDReturn[2];

    pid_equation(ErrorAnglePitch, PAnglePitch, IAnglePitch, DAnglePitch, PrevErrorAnglePitch, PrevItermAnglePitch);
    DesiredRatePitch=PIDReturn[0];
    PrevErrorAnglePitch=PIDReturn[1];
    PrevItermAnglePitch=PIDReturn[2];
    
    ErrorRateRoll=DesiredRateRoll-RateRoll;
    ErrorRatePitch=DesiredRatePitch-RatePitch;
    ErrorRateYaw=DesiredRateYaw-RateYaw;

    pid_equation(ErrorRateRoll, PRateRoll, IRateRoll, DRateRoll, PrevErrorRateRoll, PrevItermRateRoll);
    InputRoll=PIDReturn[0];
    PrevErrorRateRoll=PIDReturn[1];
    PrevItermRateRoll=PIDReturn[2];

    pid_equation(ErrorRatePitch, PRatePitch, IRatePitch, DRatePitch, PrevErrorRatePitch, PrevItermRatePitch);
    InputPitch=PIDReturn[0];
    PrevErrorRatePitch=PIDReturn[1];
    PrevItermRatePitch=PIDReturn[2];

    pid_equation(ErrorRateYaw, PRateYaw, IRateYaw, DRateYaw, PrevErrorRateYaw, PrevItermRateYaw);
    InputYaw=PIDReturn[0];
    PrevErrorRateYaw=PIDReturn[1];
    PrevItermRateYaw=PIDReturn[2];

   if (InputThrottle > 1800) InputThrottle = 1800;
   
   //input throtle value multiplied with 1.024
   MotorInput1= 1.024*(InputThrottle-InputRoll-InputPitch-InputYaw); //m1
   MotorInput2= 1.024*(InputThrottle-InputRoll+InputPitch+InputYaw); //m2
   MotorInput3= 1.024*(InputThrottle+InputRoll+InputPitch-InputYaw); //m3
   MotorInput4= 1.024*(InputThrottle+InputRoll-InputPitch+InputYaw); //m4

   if (MotorInput1 > 2000) MotorInput1 = 1989;
   if (MotorInput2 > 2000) MotorInput2 = 1989;
   if (MotorInput3 > 2000) MotorInput3 = 1989;
   if (MotorInput4 > 2000) MotorInput4 = 1989;


   int ThrottleIdle=1180;
   if (MotorInput1 < ThrottleIdle) MotorInput1 = ThrottleIdle;
   if (MotorInput2 < ThrottleIdle) MotorInput2 = ThrottleIdle;
   if (MotorInput3 < ThrottleIdle) MotorInput3 = ThrottleIdle;
   if (MotorInput4 < ThrottleIdle) MotorInput4 = ThrottleIdle;

   int ThrottleCutOff=1000;
   if (ppmData[2]<1050) 
   { 
      MotorInput1=ThrottleCutOff;
      MotorInput2=ThrottleCutOff;
      MotorInput3=ThrottleCutOff;
      MotorInput4=ThrottleCutOff;
      reset_pid();
   }
   
   /*
    Serial.print("  error Roll Angle [°] ");
    Serial.print( ErrorAngleRoll);
    Serial.print("  error Pitch Angle [°] ");
    Serial.println( ErrorAnglePitch);
   */
    
    Serial.print("\n  Motor 1 out ");
    Serial.println( MotorInput1);
    Serial.print("\n  Motor 2 out ");
    Serial.println( MotorInput2);
    Serial.print("\n  Motor 3 out ");
    Serial.println( MotorInput3);
    Serial.print("\n  Motor 4 out ");
    Serial.println( MotorInput4);
   
  
    battery_voltage =batteryvoltage();
    /*These lines are used to send pwm signals to the escs connected to the microcontroller esp32*/
    pwmloop(MotorInput1,0);  //gpio5 output to esc m1 --anti clkwise
    pwmloop(MotorInput2,1);  //gpio18 m2 -- clkwise
    pwmloop(MotorInput3,2);  //gpio19 m3 -- anti clkwise
    pwmloop(MotorInput4,3);  //gpio15 m4 -- clkwise
    
    while ((micros() - LoopTimer) < 4000);
    LoopTimer=micros();
}
    
    
 
