/**
  ******************************************************************************
  * @file    key.h
  * @brief   KEY0/KEY1 初始化、消抖及事件接口
  ******************************************************************************
  */

#ifndef __KEY_H
#define __KEY_H

#include "stm32f10x.h"

/* typedef enum 把三个离散事件值定义成 KeyEvent 类型。 */
typedef enum
{
    /* 当前没有新事件。 */
    KEY_EVENT_NONE = 0,
    /* KEY0 页面切换事件。 */
    KEY_EVENT_MENU,
    /* KEY1 编辑/保存事件。 */
    KEY_EVENT_EDIT,
    /* LCD 示波器控制项切换事件。 */
    KEY_EVENT_SCOPE
} KeyEvent;

/** @brief 初始化 KEY0 外部中断和 KEY1 上拉输入 */
void Key_Init(void);

/**
 * @brief  获取一次已经消抖的按键事件
 * @retval KEY_EVENT_NONE 当前没有新事件
 * @retval KEY_EVENT_MENU KEY0 菜单切换事件
 * @retval KEY_EVENT_EDIT KEY1 编辑确认事件
 */
KeyEvent Key_TakeEvent(void);

#endif
