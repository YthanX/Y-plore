#ifndef __MOTOR_H
#define __MOTOR_H

#define L_Move   0
#define R_Move   1
#define L_U_Move 2
#define R_U_Move 3
#define L_D_Move 4
#define R_D_Move 5
#define OFF      6
#define ON       7

void Motor_Init(void);

void forward(u16 speed);

void backward(u16 speed);

void Left_Turn(u16 speed);

void Right_Turn(u16 speed);

void Move(u16 Dir,u16 speed);

void Motion_State(u16 mode);

void Motor_Bluetooth_Mode(void);

void APP_Joy_Mode(void);

void APP_Gravity_Mode(void);


#endif

