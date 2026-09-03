#ifndef __PWM_H
#define __PWM_H

void PWM_init(void);

void PWM_Set_PSC(uint16_t Prescaler);

void PWM_Set_Compare_chn1(uint16_t Compare);

void PWM_Set_Compare_chn2(uint16_t Compare);

void PWM_Set_Compare_chn3(uint16_t Compare);


#endif
