/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : usb_device.c
  * @version        : v2.0_Cube
  * @brief          : This file implements the USB Device
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/

#include "usb_device.h"
#include "usbd_core.h"
#include "usbd_desc.h"
#include "usbd_hid.h"

/* USER CODE BEGIN Includes */
#include "stm32f1xx_hal_pcd_ex.h"
/* USER CODE END Includes */

/* USER CODE BEGIN PV */
/* Private variables ---------------------------------------------------------*/

/* USER CODE END PV */

/* USER CODE BEGIN PFP */
/* Private function prototypes -----------------------------------------------*/

/* USER CODE END PFP */

/* USB Device Core handle declaration. */
USBD_HandleTypeDef hUsbDeviceFS;

/*
 * -- Insert your variables declaration here --
 */
/* USER CODE BEGIN 0 */
extern PCD_HandleTypeDef hpcd_USB_FS;
extern volatile uint32_t g_usb_reset_count;
extern volatile uint32_t g_usb_setup_count;
extern volatile uint32_t g_usb_set_config_count;

uint8_t USB_IsConfigured(void)
{
  return ((hUsbDeviceFS.dev_state == USBD_STATE_CONFIGURED) ||
          (hUsbDeviceFS.dev_state == USBD_STATE_SUSPENDED)) ? 1U : 0U;
}

uint32_t USB_GetResetCount(void)
{
  return g_usb_reset_count;
}

uint32_t USB_GetSetupCount(void)
{
  return g_usb_setup_count;
}

uint32_t USB_GetSetConfigCount(void)
{
  return g_usb_set_config_count;
}

uint8_t USB_GetDevState(void)
{
  return hUsbDeviceFS.dev_state;
}

void USB_ForceReconnect(void)
{
  (void)USBD_Stop(&hUsbDeviceFS);
  HAL_PCDEx_SetConnectionState(&hpcd_USB_FS, 0);
  HAL_Delay(50);
  HAL_PCDEx_SetConnectionState(&hpcd_USB_FS, 1);
  (void)USBD_Start(&hUsbDeviceFS);
}
/* USER CODE END 0 */

/*
 * -- Insert your external function declaration here --
 */
/* USER CODE BEGIN 1 */

/* USER CODE END 1 */

/**
  * Init USB device Library, add supported class and start the library
  * @retval None
  */
void MX_USB_DEVICE_Init(void)
{
  /* USER CODE BEGIN USB_DEVICE_Init_PreTreatment */

  /* USER CODE END USB_DEVICE_Init_PreTreatment */

  /* Init Device Library, add supported class and start the library. */
  if (USBD_Init(&hUsbDeviceFS, &FS_Desc, DEVICE_FS) != USBD_OK)
  {
    Error_Handler();
  }
  if (USBD_RegisterClass(&hUsbDeviceFS, &USBD_HID) != USBD_OK)
  {
    Error_Handler();
  }
  if (USBD_Start(&hUsbDeviceFS) != USBD_OK)
  {
    Error_Handler();
  }

  /* USER CODE BEGIN USB_DEVICE_Init_PostTreatment */
  HAL_Delay(50);
  /* USER CODE END USB_DEVICE_Init_PostTreatment */
}

/**
  * @}
  */

/**
  * @}
  */

