/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body (Framed UART parser)
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2025 STMicroelectronics.
  * All rights reserved.
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "pins.h"   // <-- include board-specific pins and handles here
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdbool.h>

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */
/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
#define FRAME_START 0xAA
#define MAX_PAYLOAD 128
#define RX_QUEUE_SIZE 256

/* Uncomment to enable debug prints on the same UART (will mix with framed traffic) */
/* #define ENABLE_DEBUG */

/* USER CODE BEGIN PD */
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */
/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
/* Note: CubeMX will generate huart2 (or other huart) in usart.c; keep that handle name */
UART_HandleTypeDef huart2;

/* RX queue */
static volatile uint8_t rx_queue[RX_QUEUE_SIZE];
static volatile uint16_t rx_q_head = 0;
static volatile uint16_t rx_q_tail = 0;

/* Single-byte receiver for HAL_UART_Receive_IT */
static uint8_t uart_rx_byte;

/* Parser state */
static uint8_t parser_state = 0;
static uint8_t payload_len = 0;
static uint8_t payload_idx = 0;
static uint8_t payload_buf[MAX_PAYLOAD + 1];
static uint8_t checksum_accum = 0;

/* USER CODE BEGIN PV */
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_USART2_UART_Init(void);

/* Parser helpers */
void enqueue_rx_byte(uint8_t b);
bool dequeue_rx_byte(uint8_t *b);
void process_byte(uint8_t b);
void handle_payload(uint8_t *data, uint8_t len);
void send_uart_response(const char *s);
void debug_print(const char *fmt, ...);

/* USER CODE BEGIN PFP */
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{
  /* MCU Configuration--------------------------------------------------------*/
  HAL_Init();
  SystemClock_Config();

  MX_GPIO_Init();
  MX_USART2_UART_Init();

  /* Start interrupt-driven UART reception for 1 byte */
  if (HAL_UART_Receive_IT(&UART_HANDLE, &uart_rx_byte, 1) != HAL_OK) {
    // Optionally handle error
  }

  /* Optional startup notification (non-framed debug) -- disabled unless ENABLE_DEBUG
     debug_print("STM: UART Parser Ready\r\n");
  */

  /* Infinite loop: process bytes from ISR queue */
  while (1)
  {
    uint8_t b;
    while (dequeue_rx_byte(&b)) {
      process_byte(b);
    }
    /* background tasks */
    HAL_Delay(1);
  }
}

/* ======= ISR callback (called when a byte received via HAL UART IT) ======= */
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
  if (huart == &UART_HANDLE) {
    enqueue_rx_byte(uart_rx_byte);
    /* Re-arm reception */
    HAL_UART_Receive_IT(&UART_HANDLE, &uart_rx_byte, 1);
  }
}

/* ======= RX queue helpers (interrupt-safe) ======= */
void enqueue_rx_byte(uint8_t b)
{
  uint16_t head = rx_q_head;
  uint16_t next = (head + 1) % RX_QUEUE_SIZE;
  if (next != rx_q_tail) { // not full
    rx_queue[head] = b;
    rx_q_head = next;
  } else {
    // queue full; drop byte (could increment drop counter)
#ifdef ENABLE_DEBUG
    debug_print("RXQ FULL\r\n");
#endif
  }
}

bool dequeue_rx_byte(uint8_t *b)
{
  if (rx_q_tail == rx_q_head) return false; // empty
  *b = rx_queue[rx_q_tail];
  rx_q_tail = (rx_q_tail + 1) % RX_QUEUE_SIZE;
  return true;
}

/* ======= Parser state machine ======= */
void process_byte(uint8_t b)
{
  switch (parser_state) {
    case 0: // waiting for start
      if (b == FRAME_START) {
        parser_state = 1;
      }
      break;
    case 1: // length byte
      payload_len = b;
      if (payload_len > 0 && payload_len < MAX_PAYLOAD) {
        payload_idx = 0;
        checksum_accum = 0;
        parser_state = 2;
      } else {
        // invalid length -> resync
        parser_state = 0;
#ifdef ENABLE_DEBUG
        debug_print("BAD_LEN %d\r\n", payload_len);
#endif
      }
      break;
    case 2: // payload bytes
      payload_buf[payload_idx++] = b;
      checksum_accum = (uint8_t)(checksum_accum + b);
      if (payload_idx >= payload_len) {
        parser_state = 3;
      }
      break;
    case 3: // checksum byte
      if (b == checksum_accum) {
        payload_buf[payload_len] = '\0'; // null-terminate
        handle_payload(payload_buf, payload_len);
      } else {
#ifdef ENABLE_DEBUG
        debug_print("CHK_ERR exp=0x%02X got=0x%02X\r\n", checksum_accum, b);
#endif
        // checksum failed; drop frame
      }
      parser_state = 0; // always reset
      break;
    default:
      parser_state = 0;
      break;
  }
}

/* ======= Handle received payloads ======= */
/* Helper: trim trailing/leading whitespace and CR/LF in-place */
static void trim_inplace(char *s)
{
  /* trim leading */
  char *start = s;
  while (*start && (*start == ' ' || *start == '\t' || *start == '\r' || *start == '\n')) start++;
  if (start != s) memmove(s, start, strlen(start) + 1);

  /* trim trailing */
  size_t len = strlen(s);
  while (len > 0) {
    char c = s[len - 1];
    if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
      s[len - 1] = '\0';
      len--;
    } else break;
  }
}

/* Updated payload handler with motor parsing (no motor actuation) */
void handle_payload(uint8_t *data, uint8_t len)
{
  char *s = (char *)data;
  trim_inplace(s); /* remove stray whitespace/newlines */

#ifdef ENABLE_DEBUG
  debug_print("RCV_TRIM: '%s'\r\n", s);
#endif

  if (strcmp(s, "ping") == 0) {
    send_uart_response("ACK:pong");
    return;
  }

  if (strncmp(s, "led:", 4) == 0) {
    char *arg = s + 4;
    trim_inplace(arg);
    if (strcmp(arg, "on") == 0) {
      HAL_GPIO_WritePin(LED_PORT, LED_PIN, GPIO_PIN_SET);
      send_uart_response("LED:OK");
      return;
    } else if (strcmp(arg, "off") == 0) {
      HAL_GPIO_WritePin(LED_PORT, LED_PIN, GPIO_PIN_RESET);
      send_uart_response("LED:OK");
      return;
    } else {
      send_uart_response("LED:ERR");
      return;
    }
  }

  /* New: motor:<steps>:<dir>  where steps is unsigned integer, dir is 0 or 1 */
  if (strncmp(s, "motor:", 6) == 0) {
    char *p = s + 6;
    trim_inplace(p);

    /* find first ':' */
    char *sep = strchr(p, ':');
    if (!sep) {
      send_uart_response("MOTOR:ERR:FORMAT"); /* missing second ':' */
      return;
    }

    *sep = '\0';
    char *steps_str = p;
    char *dir_str = sep + 1;
    trim_inplace(steps_str);
    trim_inplace(dir_str);

    /* Basic validation: steps numeric and non-negative */
    bool steps_ok = true;
    if (strlen(steps_str) == 0) steps_ok = false;
    for (size_t i = 0; i < strlen(steps_str); ++i) {
      if (steps_str[i] < '0' || steps_str[i] > '9') { steps_ok = false; break; }
    }

    /* dir must be "0" or "1" */
    bool dir_ok = (strcmp(dir_str, "0") == 0) || (strcmp(dir_str, "1") == 0);

    if (!steps_ok || !dir_ok) {
      send_uart_response("MOTOR:ERR:BAD_ARGS");
      return;
    }

    /* parse values (safe) */
    unsigned long steps = strtoul(steps_str, NULL, 10);
    int dir = (dir_str[0] == '1') ? 1 : 0;

#ifdef ENABLE_DEBUG
    debug_print("MOTOR parsed: steps=%lu dir=%d\r\n", steps, dir);
#endif

    /* Successful parse -> send OK */
    send_uart_response("MOTOR:OK");
    return;
  }

  /* Fallback */
  send_uart_response("UNKNOWN");
}

/* ======= Send framed response using same protocol ======= */
void send_uart_response(const char *s)
{
  uint8_t len = (uint8_t)strlen(s);
  if (len == 0 || len >= MAX_PAYLOAD) return;

  uint8_t cs = 0;
  for (uint8_t i = 0; i < len; i++) cs = (uint8_t)(cs + (uint8_t)s[i]);

  uint8_t header = FRAME_START;
  HAL_UART_Transmit(&UART_HANDLE, &header, 1, 50);
  HAL_UART_Transmit(&UART_HANDLE, &len, 1, 50);
  HAL_UART_Transmit(&UART_HANDLE, (uint8_t *)s, len, 200);
  HAL_UART_Transmit(&UART_HANDLE, &cs, 1, 50);

#ifdef ENABLE_DEBUG
  debug_print("TX: %s\r\n", s);
#endif
}

/* ======= debug_print (optional) ======= */
void debug_print(const char *fmt, ...)
{
#ifdef ENABLE_DEBUG
  char buf[160];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  uint16_t l = (uint16_t)strlen(buf);
  HAL_UART_Transmit(&UART_HANDLE, (uint8_t *)buf, l, 200);
#else
  (void)fmt;
#endif
}

/* ======= System / Peripheral init code (CubeMX-generated originally) ======= */

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_NONE;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_HSI;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_0) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief USART2 Initialization Function
  * @param None
  * @retval None
  */
static void MX_USART2_UART_Init(void)
{
  /* Use 115200 to match ESP32 */
  huart2.Instance = USART2;
  huart2.Init.BaudRate = 115200;
  huart2.Init.WordLength = UART_WORDLENGTH_8B;
  huart2.Init.StopBits = UART_STOPBITS_1;
  huart2.Init.Parity = UART_PARITY_NONE;
  huart2.Init.Mode = UART_MODE_TX_RX;
  huart2.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart2.Init.OverSampling = UART_OVERSAMPLING_16;
  huart2.Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
  huart2.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;
  if (HAL_UART_Init(&UART_HANDLE) != HAL_OK)
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

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOF_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(LED_PORT, LED_PIN, GPIO_PIN_RESET);

  /*Configure GPIO pin : LED_PIN */
  GPIO_InitStruct.Pin = LED_PIN;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(LED_PORT, &GPIO_InitStruct);
}

/* USER CODE BEGIN 4 */
/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  __disable_irq();
  while (1)
  {
    /* blink LED quickly to indicate error */
    HAL_GPIO_TogglePin(LED_PORT, LED_PIN);
    HAL_Delay(200);
  }
}

#ifdef  USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* User can add reporting here */
}
#endif /* USE_FULL_ASSERT */
