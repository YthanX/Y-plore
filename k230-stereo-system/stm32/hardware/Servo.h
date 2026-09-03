#ifndef __SERVO_H__
#define __SERVO_H__

void Servo_init(void);

void Servo_SetAngle_Servo1(float angle);

void Servo_SetAngle_Servo2(float angle);

void Servo_SetPower_Laser(float Power);

void Servo_process(Serial_k230_Data *data);

#endif
// __SERVO_H__
