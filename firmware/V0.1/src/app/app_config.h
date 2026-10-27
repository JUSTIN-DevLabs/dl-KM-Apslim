#ifndef APSLIM_APP_CONFIG_H_
#define APSLIM_APP_CONFIG_H_

/* 모든 시간은 ms. 애플리케이션의 기준 타이머는 원본과 같은 TIMER2 10 ms입니다. */
#define APP_TIMER_TICK_MS 10U
#define APP_BUTTON_DEBOUNCE_MS 30U
#define APP_POWER_LONG_PRESS_MS 3000U
#define APP_DEBUG_LED_1_TOGGLE_MS 500U
#define APP_ACCEL_SAMPLE_PERIOD_MS 20U
#define APP_FDC_SAMPLE_PERIOD_MS 50U
#define APP_BATTERY_SAMPLE_PERIOD_MS 500U
#define APP_USB_OUTPUT_PERIOD_MS 100U

/* [이동평균 조정 위치]
 * 1 = 필터 우회(원본), 처음에는 3, 비교 시험은 5로 변경합니다.
 * 숫자가 커지면 평활화와 반응 지연이 모두 증가합니다. 범위: 1~32.
 * 가속도는 X/Y/Z 각각 한 번만 필터링합니다. 식사 판정에서 재평균하지 않습니다.
 * 정전용량은 오류/포화값을 평균에 넣지 않으며, 데이터가 끊기면 재시작합니다.
 * 두 값을 동시에 바꾸지 말고 센서별로 비교하면 원인을 확인하기 쉽습니다.
 */
#ifndef APP_ACCEL_MA_SAMPLES
#define APP_ACCEL_MA_SAMPLES 3U
#endif
#ifndef APP_FDC_MA_SAMPLES
#define APP_FDC_MA_SAMPLES 1U
#endif

/* [RGB 표시 조정] 식사 1회 감지 시 G 점등 후 B로 복귀합니다.
 * R은 VBUS 표시로 독립 동작하므로 USB 연결 중에는 혼합색이 보일 수 있습니다.
 * USB 전원이 MCU를 계속 공급하면 논리 OFF 상태에서도 R은 켜질 수 있습니다.
 */
#define APP_MEAL_GREEN_PULSE_MS 300U

/* [식사 판정 조정] 원본 MOTION-V3의 잠정 조건을 유지합니다.
 * 60 mg는 각도가 아닙니다. 낮추면 작은 움직임에도 반응하지만 오탐이 늘 수 있습니다.
 * 필터 개수를 바꾼 뒤 이 값도 실제 식사/비식사 자료로 다시 비교하세요.
 */
#define APP_MEAL_MOTION_TRIGGER_MG 60L
#define APP_MEAL_MOTION_WINDOW_MS 200U
#define APP_MEAL_MOTION_HOLD_MS 2000U
#define APP_MEAL_MOTION_MIN_SAMPLES 3U
#define APP_MEAL_FDC_BASE_PF_X10000 69000L /* 6.9000 pF: 자동 영점 아님 */
#define APP_MEAL_CONTACT_ON_DELTA 50000L   /* 기준선 대비 +5.0000 pF */
#define APP_MEAL_CONTACT_OFF_DELTA 20000L  /* 기준선 대비 +2.0000 pF */
#define APP_MEAL_CONTACT_CONFIRM_MS 100U
#define APP_MEAL_RELEASE_CONFIRM_MS 100U
#define APP_MEAL_REARM_CONFIRM_MS 300U

#define APP_ACCEL_STALE_MS 200U
#define APP_FDC_STALE_MS 250U
#define APP_BATTERY_STALE_MS 1500U
#define APP_BATTERY_DIVIDER_HIGH_OHM 1000000U
#define APP_BATTERY_DIVIDER_LOW_OHM 499000U

/* FDC 범위 보정: 원본의 +16 pF 포화값을 정상 접촉으로 인정하지 않습니다.
 * 0..31 코드는 각 3.125 pF의 CAPDAC 오프셋입니다. 총 pF 값에는 이를 복원합니다.
 * AUTO_RANGE=1이면 잔여 입력이 +/-15 pF 범위를 벗어났을 때 코드 조정 후 재측정합니다.
 * 조정 중 값은 필터/카운트에서 제외합니다. 범위 내에서도 입/볼 구분을 보장하지 않습니다.
 */
#define APP_FDC_INITIAL_CAPDAC 0U
#define APP_FDC_AUTO_RANGE 1U
#define APP_FDC_CAPDAC_STEP 4U
#define APP_FDC_CONVERSION_TIMEOUT_MS 100U

/* 0: 원본과 같은 100 ms 화면 스냅샷. 전체 20/50 ms 취득 로그가 아닙니다.
 * 1: 새 ACC/FDC 샘플마다 기록을 시도합니다. 순번/시간/USB_DROP으로 누락을 확인하세요.
 * PC가 읽지 않거나 USB 버퍼가 차도 센서/전원 처리를 기다리게 하지 않습니다.
 */
#ifndef APP_USB_LOG_EVERY_SAMPLE
#define APP_USB_LOG_EVERY_SAMPLE 0U
#endif
#define APP_USB_TX_BUFFER_BYTES 8192U

_Static_assert(APP_ACCEL_MA_SAMPLES >= 1U && APP_ACCEL_MA_SAMPLES <= 32U,
               "ACC moving average must be 1..32");
_Static_assert(APP_FDC_MA_SAMPLES >= 1U && APP_FDC_MA_SAMPLES <= 32U,
               "FDC moving average must be 1..32");
_Static_assert(APP_MEAL_CONTACT_OFF_DELTA < APP_MEAL_CONTACT_ON_DELTA,
               "Contact OFF threshold must be below ON");
_Static_assert(APP_FDC_INITIAL_CAPDAC <= 31U && APP_FDC_CAPDAC_STEP > 0U &&
                   APP_FDC_CAPDAC_STEP <= 31U,
               "Invalid CAPDAC setting");
_Static_assert(APP_ACCEL_SAMPLE_PERIOD_MS % APP_TIMER_TICK_MS == 0U &&
                   APP_FDC_SAMPLE_PERIOD_MS % APP_TIMER_TICK_MS == 0U &&
                   APP_BATTERY_SAMPLE_PERIOD_MS % APP_TIMER_TICK_MS == 0U &&
                   APP_USB_OUTPUT_PERIOD_MS % APP_TIMER_TICK_MS == 0U,
               "Periods must be multiples of TIMER2 tick");

#endif
