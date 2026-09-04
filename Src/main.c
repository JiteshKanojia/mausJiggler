/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
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
#include "main.h"
#include "usb_device.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <stdlib.h>
#include <math.h>
#include "usbd_def.h"
#include "usbd_hid.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define STARTUP_DELAY_MS   3000
#define LED_BLINK_WAIT_MS  1000   // full period while waiting (USB OK)
#define LED_BLINK_WORK_MS  500    // full period while moving
#define LED_BLINK_NO_HOST_MS 150  // continuous fast blink: no host setup packets
#define LED_DBG_CYCLE_MS     2500 // pause between burst groups
#define LED_DBG_SLOT_MS      400  // one blink slot within a burst
#define LED_DBG_ON_MS        200  // on-time within each slot

/*
 * USB on STM32F103 needs 72 MHz PLL -> 48 MHz USB clock.
 *   USE_HSI_CLOCK 0  - HSE crystal (recommended; use BOARD_HSE_MHZ)
 *   USE_HSI_CLOCK 1  - HSI only (64 MHz max; USB clock out of spec, test only)
 *   BOARD_HSE_MHZ  8  - crystal marked 8.000 (PLL x9)
 *   BOARD_HSE_MHZ 12  - crystal marked 12.000 (PLL x6)
 */
#ifndef USE_HSI_CLOCK
#define USE_HSI_CLOCK 0
#endif
#ifndef BOARD_HSE_MHZ
#define BOARD_HSE_MHZ 8
#endif

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */
/* ---- App state machine ---- */
typedef enum { APP_STARTUP, APP_WAITING, APP_MOVING } AppState;
static AppState app_state = APP_STARTUP;

static uint32_t startup_tick = 0;
static uint32_t led_last_toggle = 0;
static uint8_t  led_state = 0;
static uint8_t  startup_started = 0;

/* ---- Jiggler state ---- */
typedef enum { JIGGLE_WAITING, JIGGLE_MOVING } JiggleState;
static JiggleState jiggle_state = JIGGLE_WAITING;

static uint32_t next_action_tick = 0;
static uint32_t last_step_tick = 0;

static float ellipse_a;
static float ellipse_b;
static float rotation;
static float start_angle;
static float sweep_angle;
static int   total_steps;
static int   current_step;
static float prev_x, prev_y;
static uint32_t step_interval_ms;

static uint8_t hid_report[4] = {0};
extern USBD_HandleTypeDef hUsbDeviceFS;


/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/* ---------- LED helpers ---------- */
/* Blue Pill onboard LED is active-LOW: pin LOW = LED on.
   If your clone is inverted, swap SET/RESET in both functions below. */
static void led_toggle(void)
{
    led_state = !led_state;
    HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, led_state ? GPIO_PIN_RESET : GPIO_PIN_SET);
}

static void led_off(void)
{
    HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_SET);
    led_state = 0;
}

/* Not configured: fast blink = no setup packets; N blinks/cycle = dev_state (1 or 2). */
static void led_usb_not_configured(uint32_t now)
{
    static uint32_t cycle_start = 0U;
    static uint8_t cycle_started = 0U;

    if (USB_GetSetupCount() == 0U)
    {
        cycle_started = 0U;
        if (now - led_last_toggle >= LED_BLINK_NO_HOST_MS / 2U)
        {
            led_toggle();
            led_last_toggle = now;
        }
        return;
    }

    if (!cycle_started)
    {
        cycle_start = now;
        cycle_started = 1U;
        led_off();
    }

    uint8_t blinks = USB_GetDevState();
    if (USB_GetSetConfigCount() > 0U && blinks < 3U)
    {
        blinks = 3U;
    }
    if (blinks < 1U)
    {
        blinks = 1U;
    }
    if (blinks > 3U)
    {
        blinks = 3U;
    }

    uint32_t elapsed = now - cycle_start;
    if (elapsed >= LED_DBG_CYCLE_MS)
    {
        cycle_start = now;
        elapsed = 0U;
        led_off();
    }

    uint32_t slot = elapsed / LED_DBG_SLOT_MS;
    uint32_t in_slot = elapsed % LED_DBG_SLOT_MS;

    if (slot < blinks)
    {
        uint8_t on = (in_slot < LED_DBG_ON_MS) ? 1U : 0U;
        if (on != led_state)
        {
            if (on)
            {
                HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_RESET);
            }
            else
            {
                HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_SET);
            }
            led_state = on;
        }
    }
    else
    {
        led_off();
    }
}

/* ---------- HID send ---------- */
static uint8_t hid_endpoint_ready(void)
{
    if (hUsbDeviceFS.dev_state != USBD_STATE_CONFIGURED)
        return 0;
    if (hUsbDeviceFS.pClassData == NULL)
        return 0;

    USBD_HID_HandleTypeDef *hhid = (USBD_HID_HandleTypeDef *)hUsbDeviceFS.pClassData;
    return (hhid->state == HID_IDLE);
}

/* Returns 1 if the report was accepted by the USB stack. */
static uint8_t send_hid_move(int8_t dx, int8_t dy)
{
    USBD_HID_HandleTypeDef *hhid;

    if (!hid_endpoint_ready())
        return 0;

    hid_report[0] = 0;
    hid_report[1] = (uint8_t)dx;
    hid_report[2] = (uint8_t)dy;
    hid_report[3] = 0;
    USBD_HID_SendReport(&hUsbDeviceFS, hid_report, 4);

    hhid = (USBD_HID_HandleTypeDef *)hUsbDeviceFS.pClassData;
    return (hhid != NULL && hhid->state == HID_BUSY);
}

/* ---------- random helper ---------- */
static float randf(float min, float max)
{
    return min + ((float)rand() / (float)RAND_MAX) * (max - min);
}

/* ---------- ellipse math ---------- */
static void ellipse_point(float t, float *x, float *y)
{
    float ex = ellipse_a * cosf(t);
    float ey = ellipse_b * sinf(t);
    *x = ex * cosf(rotation) - ey * sinf(rotation);
    *y = ex * sinf(rotation) + ey * cosf(rotation);
}

/* ---------- start a new curved move ---------- */
static void start_new_move(void)
{
    ellipse_a   = randf(15.0f, 40.0f);
    ellipse_b   = randf(10.0f, 30.0f);
    rotation    = randf(0.0f, 2.0f * M_PI);
    start_angle = randf(0.0f, 2.0f * M_PI);
    sweep_angle = randf(M_PI * 0.6f, M_PI * 1.6f);
    if (rand() % 2) sweep_angle = -sweep_angle;

    total_steps  = 6 + (rand() % 7);    // 6-12 steps
    current_step = 0;
    prev_x = 0.0f;
    prev_y = 0.0f;

    step_interval_ms = 15 + (rand() % 11); // 15-25 ms

    jiggle_state = JIGGLE_MOVING;
    last_step_tick = HAL_GetTick();
}

/* ---------- advance one step of the current move ---------- */
static void jiggler_step(void)
{
    uint32_t now = HAL_GetTick();

    if (now - last_step_tick < step_interval_ms) return;

    if (current_step >= total_steps)
    {
        jiggle_state = JIGGLE_WAITING;
        next_action_tick = now + (3000 + (rand() % 5000)); // 3-8s
        return;
    }

    {
        int next_step = current_step + 1;
        float t = (float)next_step / (float)total_steps;
        float eased = 0.5f - 0.5f * cosf(t * M_PI);
        float angle = start_angle + sweep_angle * eased;
        float x, y;
        float dx, dy;
        int8_t idx, idy;

        ellipse_point(angle, &x, &y);

        dx = x - prev_x;
        dy = y - prev_y;

        idx = (int8_t)roundf(dx);
        idy = (int8_t)roundf(dy);

        if (idx != 0 || idy != 0)
        {
            if (!send_hid_move(idx, idy))
                return;
        }

        current_step = next_step;
        prev_x += idx;
        prev_y += idy;
    }

    last_step_tick = now;
}

/* ---------- top-level poll, call every loop iteration ---------- */
static void app_poll(void)
{
    uint32_t now = HAL_GetTick();

    if (!USB_IsConfigured())
    {
        led_usb_not_configured(now);
        return;
    }

    switch (app_state)
    {
        case APP_STARTUP:
            if (!startup_started)
            {
                startup_tick = now;
                led_last_toggle = now;
                startup_started = 1;
            }

            if (now - led_last_toggle >= LED_BLINK_WAIT_MS / 2)
            {
                led_toggle();
                led_last_toggle = now;
            }

            if (now - startup_tick >= STARTUP_DELAY_MS)
            {
                app_state = APP_WAITING;
                next_action_tick = now + (3000 + (rand() % 5000));
                led_last_toggle = now;
            }
            break;

        case APP_WAITING:
            if (now - led_last_toggle >= LED_BLINK_WAIT_MS / 2)
            {
                led_toggle();
                led_last_toggle = now;
            }

            if (now >= next_action_tick)
            {
                start_new_move();
                app_state = APP_MOVING;
                led_last_toggle = now;
            }
            break;

        case APP_MOVING:
            if (now - led_last_toggle >= LED_BLINK_WORK_MS / 2)
            {
                led_toggle();
                led_last_toggle = now;
            }

            jiggler_step();

            if (jiggle_state == JIGGLE_WAITING)
            {
                app_state = APP_WAITING;
                led_last_toggle = now;
            }
            break;
    }
}

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_USB_DEVICE_Init();
  /* USER CODE BEGIN 2 */
  srand(HAL_GetTick() ^ (uint32_t)&hid_report);
  led_off();
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
	  app_poll();

    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};
  RCC_PeriphCLKInitTypeDef PeriphClkInit = {0};

#if USE_HSI_CLOCK
  /* HSI/2 x16 = 64 MHz (F103 PLL max). USB = 42.7 MHz, not spec-compliant. */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSI_DIV2;
  RCC_OscInitStruct.PLL.PLLMUL = RCC_PLL_MUL16;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }
#else
  /** HSE -> PLL = 72 MHz SYSCLK, USB = 48 MHz; fall back to HSI if HSE fails */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.HSEPredivValue = RCC_HSE_PREDIV_DIV1;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
#if BOARD_HSE_MHZ == 12
  RCC_OscInitStruct.PLL.PLLMUL = RCC_PLL_MUL6;
#elif BOARD_HSE_MHZ == 8
  RCC_OscInitStruct.PLL.PLLMUL = RCC_PLL_MUL9;
#else
#error "BOARD_HSE_MHZ must be 8 or 12"
#endif
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
    RCC_OscInitStruct.HSIState = RCC_HSI_ON;
    RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSI_DIV2;
    RCC_OscInitStruct.PLL.PLLMUL = RCC_PLL_MUL16;
    if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
    {
      Error_Handler();
    }
  }
#endif

  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK)
  {
    Error_Handler();
  }

  PeriphClkInit.PeriphClockSelection = RCC_PERIPHCLK_USB;
  PeriphClkInit.UsbClockSelection = RCC_USBCLKSOURCE_PLL_DIV1_5;
  if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInit) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief GPIO Initialization Function
  * @param None
  * @retval None
  */
static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};
  /* USER CODE BEGIN MX_GPIO_Init_1 */

  /* USER CODE END MX_GPIO_Init_1 */

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOD_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_RESET);

  /*Configure GPIO pin : PC13 */
  GPIO_InitStruct.Pin = GPIO_PIN_13;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

  /* USER CODE BEGIN MX_GPIO_Init_2 */

  /* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */

/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
