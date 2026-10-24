# APSLIM Sensor USB Test

v0.1 본 펌웨어를 만들기 전, Rev.A PCB의 입력과 센서 값을 함께 확인하는 테스트 앱입니다.
애플리케이션 로직은 `src/main.c` 한 파일에 모았습니다.

## 적용 기준

- MCU: MDBT50Q-1MV2 / nRF52840 커스텀 PCB.
- SDK 대상: nRF Connect SDK v3.4.1 / Zephyr, 기존 테스트와 같은 legacy USB CDC.
- 빌드 타겟: `raytac_mdbt50q_db_40/nrf52840` + 동봉 `app.overlay`.
- 2026-10-17 성공 화면: 가속도 I2C 주소 `0x19`, WHO_AM_I `0x44`; FDC1004 `0x50`, ID `0x1004`.
- 코딩 기준: Engineering Playbook 07-01. C / Embedded C Coding Convention v1.1.
- 시험 파일이므로 단일 main.c를 사용하는 것은 사용자가 지정한 모듈 분리 규칙의 예외입니다.

회로도·기존 명세의 LIS2DH12(WHO=0x33)와 현재 성공 화면의 센서 ID가 다릅니다.
이 코드는 실제 확인된 **0x44 계열(LIS2DW12/LIS2DTW12)** 레지스터를 사용합니다.
0x44만으로 두 부품을 구분하지는 않습니다. 0x19를 먼저 찾고 0x18을 추가 확인합니다.
0x33 센서에는 초기화 실패(-ENOTSUP)를 표시하며 잘못된 레지스터 설정을 적용하지 않습니다.

## 파일

```text
APSLIM_Sensor_USB_Test/
    CMakeLists.txt
    prj.conf
    app.overlay
    README.md
    VALIDATION.md
    src/main.c
```

## 동작 순서

1. 기동 직후 P0.05를 LOW, 두 디버그 LED를 OFF로 설정합니다.
2. TIMER2와 USB CDC를 시작합니다. USB 터미널 연결을 기다리며 정지하지 않습니다.
3. P0.26 버튼을 10 ms마다 확인하고 30 ms 디바운싱을 적용합니다.
4. 버튼이 3초간 계속 눌렸을 때 P0.05를 HIGH로 만들어 전원을 유지합니다.
5. 배터리 ADC·가속도·FDC1004 초기화를 시작합니다.
6. 가속도 소프트 리셋 완료와 80 ms 안정화는 타이머의 경과 시간으로 확인합니다.
7. 초기화 결과를 출력한 뒤 센서 값을 지정 순서로 계속 출력합니다.

3초는 펌웨어가 버튼을 처음 확인한 시점부터 계산합니다. 전원이 완전히 꺼진 상태에서
누르는 경우 MCU 부팅 시간만큼 실제 누름 시간이 조금 더 필요합니다.
3초 전에 손을 떼면 배터리 단독 운전에서는 하드웨어 전원이 꺼질 수 있습니다.
USB로 전원이 공급되는 상태에서도 3초 누르기 전에는 P0.05를 올리지 않습니다.
이 테스트의 길게 누르기는 ON 동작만 수행합니다. ON 후 다시 눌러도 전원을 끄지 않습니다.

## 핀

| 기능 | MCU 핀 | 설정 |
| --- | --- | --- |
| 버튼 입력 | P0.26 | Active HIGH, 외부 풀다운 |
| 전원 유지 | P0.05 | HIGH 유지 |
| DEBUG LED1 | P0.31 | Active LOW |
| DEBUG LED2 | P0.30 | Active LOW |
| 배터리 ADC | P0.02 / AIN0 | 12 bit, 내부 0.6 V 기준, gain 1/6, 취득 40 us |
| 가속도 SDA / SCL | P0.06 / P0.08 | I2C0, 100 kHz |
| FDC1004 SDA / SCL | P1.00 / P0.25 | I2C1, 100 kHz |

DK의 UART/SPI/PWM/QSPI와 버튼 노드를 비활성화하여 핀 충돌을 줄였습니다.
예전 main.c의 `led0`, `sw0`에 의존하지 않고 용도가 드러나는 별칭을 사용합니다.
`default-state`와 같은 gpio-leds 바인딩에 없는 속성은 사용하지 않습니다.

## 타이머 구성

TIMER2 한 개를 하드웨어 compare/clear 방식으로 반복 사용합니다.
prescaler 4에서 타이머 카운터는 1 MHz이고, top=10,000 ticks로 10 ms마다 ISR을 실행합니다.
Zephyr Counter 드라이버의 top callback을 사용하므로 nrfx IRQ를 중복 등록하지 않습니다.
PWM 출력은 타이머의 시간 기준을 만들기 위해 필요하지 않습니다.

| 작업 | 주기 | 실행 위치 |
| --- | --- | --- |
| 타이머 카운터 / 이벤트 / main 깨우기 | 10 ms | TIMER2 ISR |
| 버튼 읽기 / 3초 판정 | 10 ms | main |
| LED1 토글 | 500 ms | main, ISR 이벤트로 실행 |
| LED2 토글 | 100 ms | TIMER2 ISR에서 직접 GPIO 토글 |
| 가속도 읽기 | 20 ms / 50 Hz | main |
| FDC1004 측정 시작 | 50 ms / 20 Hz | main |
| FDC1004 완료 확인 | 측정 대기 중 10 ms | main |
| 배터리 ADC | 500 ms / 2 Hz | main |
| USB 데이터 행 | 20 ms / 50 Hz | main에서 전송 큐에 저장 |

LED1의 ON/OFF 한 주기는 1초, LED2는 200 ms입니다. 토글 간격과 전체 점멸 주기를 구분합니다.
LED2가 계속 점멸하는데 LED1이 멈추면 main의 I/O 지연 여부를 확인할 수 있습니다.
LED1과 센서 작업은 main에서 실행되므로 드라이버 처리 시간만큼 실행 시점이 늦어질 수 있습니다.

`k_sleep`, `k_msleep`, `k_busy_wait`, 지연용 반복문을 사용하지 않습니다.
`k_sem_take(K_FOREVER)`는 타이머 이벤트가 발생할 때까지 main을 대기시키는 이벤트 대기입니다.
Zephyr 커널·USB 드라이버가 내부적으로 사용하는 시간 자원은 애플리케이션 TIMER2와 별개입니다.
I2C/ADC는 main에서 동기 드라이버 API를 호출하므로 이 코드가 모든 I/O까지 비동기인 것은 아닙니다.
처리가 늦어진 과거 센서 읽기를 몰아서 재생하지 않고 `missed_ticks`로 누락된 main 처리 주기를 기록합니다.

## 센서 값

### 배터리

현재 실제 PCB에서 C5는 직렬 연결이 아니라는 사용자 확인을 반영했습니다.
R8=1 MOhm, R10=499 kOhm 기준으로 다음과 같이 환산합니다.

```text
battery_mv = adc_pin_mv * (1000000 + 499000) / 499000
           = adc_pin_mv * 약 3.004008
```

분압 저항값은 main.c의 `BATTERY_DIVIDER_HIGH_OHM`, `BATTERY_DIVIDER_LOW_OHM`에서 변경합니다.
배터리는 전압(mV)을 출력합니다. 잔량(%)·충전 상태·저전압 종료는 이번 테스트에 넣지 않았습니다.
첫 샘플에서 SAADC 보정을 수행합니다. 첫 출력 전압은 멀티미터 VBAT 값과 대조해 주세요.

### 가속도

- WHO=0x44 계열, HP 모드, ODR 50 Hz, +/-2 g.
- BDU와 주소 자동 증가를 켜고 FIFO는 bypass로 둡니다.
- 6 byte를 한 번에 읽어 X/Y/Z를 mg로 변환합니다. `1000 mg = 1 g`입니다.
- 출력은 센서의 X/Y/Z 좌표 그대로입니다. 스푼 방향 매핑·높이 환산·자세 판정은 적용하지 않았습니다.
- 이동평균/IIR/Kalman 등 소프트웨어 필터는 적용하지 않았습니다.
- DATA_READY가 아직 없으면 마지막 완전한 샘플을 유지하며 나이로 구분합니다.

센서의 50 Hz ODR과 MCU의 20 ms 읽기는 서로 다른 클록을 사용합니다.
따라서 모든 USB 행이 반드시 서로 다른 가속도 샘플을 포함하지는 않습니다.
`acc_age_ms`를 확인하세요. FIFO 또는 DRDY 인터럽트 기반의 무손실 수집은 후속 단계입니다.

### FDC1004

- CIN1만 사용하며 CAPDAC 기본값은 0입니다.
- 100 S/s 설정의 단발 변환을 50 ms마다 시작합니다. 완료를 다음 타이머 이벤트에서 확인합니다.
- MSB 레지스터를 먼저, LSB 레지스터를 다음으로 읽고 24 bit 부호 확장을 적용합니다.
- `pF = signed_raw / 524288 + CAPDAC * 3.125`로 변환합니다.
- 출력은 소수점 4자리 pF입니다. 이동평균·영점 보정·접촉 판정은 적용하지 않았습니다.
- 잔여 측정 범위가 +/-15 pF에 도달하면 숫자와 함께 `fdc_ret=-ERANGE`를 출력합니다.
- 포화되면 `FDC_CAPDAC_CODE`를 0..31 범위에서 조절하고 다시 빌드합니다.

## USB 출력

터미널이 DTR을 올리면 초기화 결과와 컬럼 제목을 보냅니다.
전원 유지·센서 초기화 단계가 바뀌거나 터미널을 다시 연결하면 결과 헤더를 다시 보냅니다.
초기화 중에는 `INIT=-EINPROGRESS`가 숫자로 출력되며 완료 후 0으로 바뀝니다.
센서별 초기화 실패는 독립적으로 표시하고 다른 센서의 동작은 계속합니다.

앞의 여섯 컬럼은 요청한 순서입니다.

```text
btn,battery_mv,accel_x_mg,accel_y_mg,accel_z_mg,fdc_pf
```

이후 컬럼은 다음과 같습니다.

| 컬럼 | 의미 |
| --- | --- |
| time_ms | TIMER2 시작 이후 경과 시간 |
| pwr_hold | P0.05 유지 출력 적용 성공: 1 |
| btn_raw | P0.26 실제 GPIO 입력 0/1 |
| btn_hold_ms | 디바운싱된 연속 누름 시간, 놓으면 0 |
| bat_adc_raw / bat_adc_mv | ADC 원시값 / MCU 핀 전압 |
| acc_ret / fdc_ret / bat_ret | 0 정상, 음수 오류. 준비 전·오류·오래된 값은 NA |
| acc_age_ms / fdc_age_ms / bat_age_ms | 마지막 성공 샘플의 나이, 샘플 없음은 -1 |
| missed_ticks | main이 한 번에 처리하지 못한 10 ms tick 누적 수 |
| usb_drop | 호스트가 느리거나 큐가 차서 버린 데이터 행 수 |
| btn_ret / led1_ret / led2_ret | 버튼 읽기 / LED1 / LED2 처리 오류 |

USB 50 Hz 행에 배터리 2 Hz와 FDC 20 Hz 값이 반복되는 것은 정상입니다.
단위만 변환한 값을 확인하기 위한 테스트이며, 모든 센서가 같은 순간에 샘플링되는 것은 아닙니다.
USB가 연결되지 않아도 버튼·LED·센서는 동작합니다. USB 큐가 차도 main을 기다리게 하지 않습니다.

## 빌드 / 실행

1. 압축을 풀고, 기존 앱을 덮어쓰지 않도록 새로운 테스트 폴더에 둡니다.
2. VS Code nRF Connect에서 `CMakeLists.txt`가 있는 폴더를 애플리케이션으로 추가합니다.
3. SDK/Toolchain은 v3.4.1, Board target은 `raytac_mdbt50q_db_40/nrf52840`를 선택합니다.
4. 첫 빌드는 Pristine Build로 진행합니다. 동봉 app.overlay와 prj.conf를 한 세트로 사용합니다.
5. 플래시 후 USB 데이터 케이블로 PCB를 연결하고 생성된 COM 포트를 엽니다.
6. 터미널은 115200, 8N1, 흐름 제어 없음, DTR ON으로 설정합니다.
   USB CDC의 115200은 UART 전선의 실제 전송 속도가 아닌 호스트 설정값입니다.
7. 버튼을 부팅 시간 포함 3초 이상 누른 뒤 `PWR_HOLD=1`과 각 `INIT=0`을 확인합니다.

NCS v3.4.1 터미널에서 프로젝트 루트 기준 명령 예시:

```sh
west build -p always -b raytac_mdbt50q_db_40/nrf52840 -d build .
west flash -d build
```

이 패키지에는 빌드 산출물(hex/elf)이 없습니다.
작성 환경에는 NCS v3.4.1 ARM 툴체인과 실제 PCB가 없어 타겟 빌드·플래시·실측을 완료하지 못했습니다.
동봉 VALIDATION.md에 수행한 호스트 검증과 남은 PCB 확인 항목을 구분했습니다.

## 참고 기준

- Coding Convention: https://app.notion.com/p/3f66b0d0b59981dca4b4c244e022c4a8
- 핀맵: https://app.notion.com/p/3ee6b0d0b5998026a677c41d28dc1f07
- 기능 명세: https://app.notion.com/p/3f96b0d0b5998025b350c3217138a436
- Zephyr Counter API: https://docs.zephyrproject.org/latest/hardware/peripherals/counter.html
- ST LIS2DTW12 register map: https://www.st.com/resource/en/datasheet/lis2dtw12.pdf
- TI FDC1004: https://www.ti.com/lit/ds/symlink/fdc1004.pdf

FDC1004의 공식 권장 전원은 3.0~3.6 V입니다. 회로도에 남은 1V8 네트 표기와 별도로,
현재 동작 PCB에서 센서 VDD가 이 범위인지 확인하는 것이 측정 유효성 확인에 필요합니다.
