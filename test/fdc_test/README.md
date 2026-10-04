# FDC1004 CIN1 USB test — Apslim

Target SDK: **nRF Connect SDK v3.4**. 소스 및 설정 파일 상단에 대상 SDK를 표기합니다. 이 표기는 빌드 대상이며 실제 빌드 검증 완료를 뜻하지 않습니다.

기존 nRF52840 / Zephyr 프로젝트에서 사용한 legacy USB CDC 방식(usb_enable)을 기준으로 작성한 독립 테스트 프로젝트입니다. 기존 Bring-up 기능을 모두 합친 코드는 아닙니다. 현재 환경에는 NCS 도구와 보드가 없어 실제 빌드·플래시·측정은 수행하지 않았습니다.

## 구성과 핀

| 항목 | 설정 |
|---|---|
| MCU | nRF52840 / MDBT50Q |
| FDC1004 | I2C1, 7-bit address 0x50 |
| SDA / SCL | P1.00 / P0.25 |
| 측정 입력 | CIN1만, single-ended, measurement 1 |
| 디버그 LED1 | P0.31, Active Low |
| 전원 유지 | P0.05, 부팅 시 High |
| LED | 즉시 ON, 이후 500ms마다 토글 |
| 측정 시작 주기 | 100ms, 정상 동작 시 약 10회/초 |
| FDC 변환 속도 설정 | 100S/s, 단발 측정 |
| 측정 완료 확인 | 2ms 타이머 틱, DONE1 확인 |
| 변환 제한 시간 | 50ms, 오류 시 1초 후 초기화 재시도 |
| USB 서비스 | 20ms 타이머, DTR 연결 확인 |

LED는 프로그램의 heartbeat이며 센서 정상 판정 표시가 아닙니다. USB 데이터의 status도 확인하세요. P0.31 LED에 관한 현장 핀 변경이 있다면 app.overlay의 fdc_debug_led만 수정합니다.

## 사용 순서

1. 압축을 풀어 fdc test 폴더를 기존 test 폴더 아래에 둡니다.
2. VS Code nRF Connect에서 이 폴더를 Application으로 추가합니다.
3. SDK는 **nRF Connect SDK v3.4**를 선택하고, Board target은 기존 I2C/USB 테스트에 성공한 값을 그대로 사용합니다. 설치 환경에서의 실제 빌드 검증은 필요합니다.
4. Pristine Build 후 Flash합니다. 이 프로젝트의 app.overlay와 prj.conf를 사용합니다. 기존 프로젝트 overlay와 통째로 중복 병합하지 마세요.
5. LED1이 0.5초 간격으로 토글되는지 확인합니다.
6. Tera Term에서 장치의 USB COM 포트를 엽니다. 115200 / 8N1 / flow control none으로 설정합니다. USB CDC 가상 포트이므로 115200이 센서 샘플링 속도를 결정하지는 않습니다.
7. CIN1 센서에 접촉하거나 가까이 갔을 때 raw와 cap_pf의 변화를 확인합니다.

USB 노드는 cdc_acm_uart0입니다. 기존에 해당 노드를 이미 선언한 보드라면 하나만 유지해야 합니다. USB stack next 기반 프로젝트로 합칠 경우 legacy USB 설정과 초기화 코드를 그대로 혼합하지 마세요.

## 출력 형식

```text
Target SDK: nRF Connect SDK v3.4
FDC1004 CIN1 | I2C1 0x50 | period=100ms
time_ms,raw,cap_pf,status
112,2621440,5.000000,OK
212,2673869,5.100000,OK
```

위 숫자는 형식 설명용 예시이며 실제 측정 결과가 아닙니다.

- time_ms: 측정값을 읽은 시점의 부팅 후 시간(ms, 32-bit).
- raw: 부호 있는 24-bit 원본값.
- cap_pf: raw / 524288 + CAPDAC_STEPS × 3.125(pF).
- OK: 통신·변환 완료 정상. 센서 교정이나 접촉 여부를 보증하지는 않습니다.
- RANGE_LIMIT: 원본값이 ±15pF에 도달한 경우의 소프트웨어 경고. 정확한 포화 판정 비트가 아닙니다.
- ERROR:음수: I2C 오류, ID 불일치, 변환 시간 초과 등. 다음에 재초기화합니다.

소수 여섯 자리 표시는 출력 형식이며 그 자리까지 측정 정확도를 보장하지 않습니다. 필터와 접촉 판정은 적용하지 않았습니다.

## 타이머 구조

- LED thread: 500ms k_timer를 기다린 후 GPIO 토글.
- main thread: 2ms k_timer마다 NEED_INIT → IDLE → WAIT_DONE 상태 처리.
- USB thread: 20ms k_timer마다 큐를 확인하고 연결된 터미널로 출력.
- k_sleep / k_msleep / k_busy_wait를 사용하지 않습니다. k_timer_status_sync는 타이머 만료까지 해당 스레드를 대기시키고 CPU는 다른 작업을 할 수 있습니다.
- Zephyr 시스템 타이머를 이용하는 소프트웨어 타이머입니다. nrfx TIMER 주변장치를 직접 점유하는 방식은 아닙니다.
- I2C/USB 호출은 인터럽트 내부에서 실행하지 않습니다. I2C 전송 자체는 동기 API이며 완전 비동기 DMA 드라이버는 아닙니다.
- USB 송신은 별도 스레드에서 수행하므로 터미널 지연이 LED와 센서 스레드를 직접 막지 않습니다. 큐가 차면 오래된 샘플을 버립니다. 이 버전은 실시간 확인용이며 무손실 기록용이 아닙니다.
- 터미널을 닫아도 센서 측정과 LED는 계속 동작합니다.

## CAPDAC 조정

초기값은 src/main.c의 CAPDAC_STEPS=0입니다. 값이 상한에 고정되거나 RANGE_LIMIT가 계속 나오면 CAPDAC_STEPS를 조정해 입력 범위를 이동할 수 있습니다(0~31, 1단계=3.125pF). 자동 범위 조정은 포함하지 않았습니다. CAPDAC를 켜면 SHLD2 동작도 달라지므로 실제 실드 연결을 확인한 뒤 변경하세요.

## 근거 및 검증 범위

- TI FDC1004 datasheet: https://www.ti.com/lit/ds/symlink/fdc1004.pdf
- Zephyr timers: https://docs.zephyrproject.org/latest/kernel/services/timing/timers.html
- Zephyr legacy USB: https://docs.zephyrproject.org/latest/connectivity/usb/device/usb_device.html

레지스터 주소, DONE1, MSB→LSB 읽기 순서, signed 24-bit 변환과 pF 환산을 데이터시트에 대조했습니다. 단발 측정은 RATE=01, REPEAT=0, MEAS1=1인 0x0480입니다. 데이터시트 본문의 0x0540 단발 예시는 REPEAT 비트 설명과 불일치하므로 비트 정의를 따릅니다. SDK 빌드와 실제 하드웨어 검증은 미수행입니다.
