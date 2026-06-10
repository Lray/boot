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
#include "hash.h"
#include "iwdg.h"
#include "pka.h"
#include "rng.h"
#include "usart.h"
#include "gpio.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "bootutil/boot_active_slot_flash.h"
#include "bootutil/boot_public.h"
#include "bootutil/bootutil.h"
#include "bootutil/bootutil_log.h"
#include "sysflash.h"
#include "ymodem.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define BOOT_TRIGGER_PIN GPIO_PIN_13
#define BOOT_TRIGGER_PORT GPIOC
#define BOOT_TRIGGER_ACTIVE GPIO_PIN_SET
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */
uint8_t FileName[FILE_NAME_LENGTH];
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
/* USER CODE BEGIN PFP */
extern void do_boot(struct boot_rsp *rsp);
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
typedef enum
{
  BOOT_MODE_NORMAL = 0,
  BOOT_MODE_DOWNLOAD = 1,
} boot_mode_t;

static boot_mode_t check_boot_mode(void)
{
  if (HAL_GPIO_ReadPin(BOOT_TRIGGER_PORT, BOOT_TRIGGER_PIN) == BOOT_TRIGGER_ACTIVE)
  {
    return BOOT_MODE_DOWNLOAD;
  }
  return BOOT_MODE_NORMAL;
}

static void enter_bootloader_download(uint8_t active_slot)
{
  uint32_t primary = CY_FLASH_BASE + CY_BOOT_BOOTLOADER_SIZE;
  uint32_t secondary = primary + CY_BOOT_PRIMARY_1_SIZE;
  const char *active_str = "unknown";
  const char *target_str = "unknown";
  uint32_t target_addr = 0;
  uint8_t target_slot = 0xFF;
  static uint8_t ymodem_buf[PACKET_1K_SIZE + PACKET_OVERHEAD];

  if (active_slot == BOOT_SLOT_PRIMARY)
  {
    active_str = "primary";
    target_str = "secondary";
    target_addr = secondary;
    target_slot = BOOT_SLOT_SECONDARY;
  }
  else if (active_slot == BOOT_SLOT_SECONDARY)
  {
    active_str = "secondary";
    target_str = "primary";
    target_addr = primary;
    target_slot = BOOT_SLOT_PRIMARY;
  }

  BOOT_LOG_INF("Enter bootloader download mode");
  if (target_addr != 0)
  {
    BOOT_LOG_INF("Active slot: %s, download target: %s (0x%08lX)",
                 active_str, target_str, (unsigned long)target_addr);
  }
  else
  {
    BOOT_LOG_ERR("Download target: unknown (state invalid)");
    BOOT_LOG_ERR("Cannot select target slot, abort.");
    while (1)
    {
      IWDG_Feed();
      HAL_Delay(200);
    }
  }

  BOOT_LOG_INF("Ready for YMODEM download.");

  {
    int32_t rc = Ymodem_Receive(ymodem_buf, target_slot);
    if (rc > 0)
    {
      BOOT_LOG_INF("YMODEM receive OK, size=%ld bytes", (long)rc);
    }
    else if (rc == 0)
    {
      BOOT_LOG_WRN("YMODEM aborted or no file received.");
    }
    else
    {
      BOOT_LOG_ERR("YMODEM receive failed, rc=%ld", (long)rc);
    }
  }

  BOOT_LOG_INF("Download finished. Reset to boot the new image.");
  IWDG_Feed();
  HAL_Delay(50);
  NVIC_SystemReset();
}
/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */
  struct boot_rsp rsp;
  int boot_rc;
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
  MX_USART1_UART_Init();
  MX_USART2_UART_Init();
  MX_IWDG_Init();
  MX_HASH_Init();
  MX_RNG_Init();
  MX_PKA_Init();
  /* USER CODE BEGIN 2 */
  boot_mode_t mode = check_boot_mode();

  if (mode == BOOT_MODE_DOWNLOAD)
  {
    BOOT_LOG_INF("[Bootloader] GPIO trigger detected, entering download mode...");
    uint8_t active_slot;

    if (!boot_active_slot_read(&active_slot))
    {
      BOOT_LOG_ERR("Active slot record invalid, abort download.");
      while (1)
      {
        IWDG_Feed();
        HAL_Delay(200);
      }
    }

    enter_bootloader_download(active_slot);
  }

  boot_rc = boot_go(&rsp);

  if (boot_rc != 0)
  {
    BOOT_LOG_ERR("No valid image, stay in bootloader. rc=%d", boot_rc);
    while (1)
    {
      IWDG_Feed();
      HAL_Delay(200);
    }
  }

  {
    const uint32_t secondary = CY_FLASH_BASE + CY_BOOT_BOOTLOADER_SIZE +
                               CY_BOOT_PRIMARY_1_SIZE;
    uint8_t active_slot = BOOT_SLOT_PRIMARY;

    if (rsp.br_image_off == secondary)
    {
      active_slot = BOOT_SLOT_SECONDARY;
    }

    boot_active_slot_write(active_slot);
  }

  do_boot(&rsp);
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
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

  /** Configure the main internal regulator output voltage
  */
  if (HAL_PWREx_ControlVoltageScaling(PWR_REGULATOR_VOLTAGE_SCALE1) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI48|RCC_OSCILLATORTYPE_LSI
                              |RCC_OSCILLATORTYPE_MSI;
  RCC_OscInitStruct.HSI48State = RCC_HSI48_ON;
  RCC_OscInitStruct.LSIState = RCC_LSI_ON;
  RCC_OscInitStruct.MSIState = RCC_MSI_ON;
  RCC_OscInitStruct.MSICalibrationValue = RCC_MSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.MSIClockRange = RCC_MSIRANGE_0;
  RCC_OscInitStruct.LSIDiv = RCC_LSI_DIV1;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_MSI;
  RCC_OscInitStruct.PLL.PLLMBOOST = RCC_PLLMBOOST_DIV4;
  RCC_OscInitStruct.PLL.PLLM = 3;
  RCC_OscInitStruct.PLL.PLLN = 10;
  RCC_OscInitStruct.PLL.PLLP = 2;
  RCC_OscInitStruct.PLL.PLLQ = 2;
  RCC_OscInitStruct.PLL.PLLR = 1;
  RCC_OscInitStruct.PLL.PLLRGE = RCC_PLLVCIRANGE_1;
  RCC_OscInitStruct.PLL.PLLFRACN = 0;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2
                              |RCC_CLOCKTYPE_PCLK3;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;
  RCC_ClkInitStruct.APB3CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_4) != HAL_OK)
  {
    Error_Handler();
  }
}

/* USER CODE BEGIN 4 */
void __aeabi_assert(const char *expr, const char *file, int line)
{
  (void)expr;
  (void)file;
  (void)line;
  Error_Handler();
  while (1)
  {
  }
}
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
  /* User can add an implementation here to report the file name and line number. */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
