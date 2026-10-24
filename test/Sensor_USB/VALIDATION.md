# Validation

검증일: 2026-10-17

## 수행한 검증

현재 `src/main.c` 자체를 테스트 번역 단위에 포함하고 main의 이름만 바꾸어,
동일한 static 함수들을 Linux 호스트에서 직접 호출했습니다.
Zephyr 장치·GPIO·Counter·I2C·ADC·UART API는 테스트용 mock으로 대체했습니다.
이는 실제 SDK 헤더 및 드라이버를 이용한 타겟 빌드가 아닙니다.

GCC 13, C11, `-Wall -Wextra -Werror -Wshadow -Wformat=2`,
AddressSanitizer와 UndefinedBehaviorSanitizer를 사용한 실행을 통과했습니다.

| 검증 | 결과 |
| --- | --- |
| TIMER2 설정 top 10,000 / 1초 동안 이벤트 발생 비율 | PASS |
| LED1 2회, LED2 10회, ACC/USB 50회, FDC 20회, BAT 2회 / 초 | PASS |
| 3초 이전 PWR LOW, 3초 연속 누름 이후 HIGH, 놓은 뒤 유지 | PASS |
| 3초 직전 10 ms 놓고 다시 누른 경우 누름 시간 재시작 | PASS |
| 30 ms 미만 버튼 채터링·버튼 읽기 실패·전원 GPIO 실패 | PASS |
| 32 bit 경과 시간의 wraparound 중 누름 시간 계산 | PASS |
| 배터리 ADC 전압 / 1 MOhm·499 kOhm 분압 환산 및 샘플 오류 | PASS |
| 가속도 양수·음수 mg 환산, 0x44 설정, 0x33 거부 | PASS |
| 가속도 리셋 / 안정화 상태 전이와 리셋 타임아웃 | PASS |
| FDC1004 24 bit 부호·pF 변환과 CAPDAC 0 설정 | PASS |
| FDC 단발 측정 시작 / 완료 대기 / 결과 읽기 / 타임아웃 / 포화 표시 | PASS |
| 초기화 헤더 버퍼 크기·요청한 선두 6개 컬럼·CSV 컬럼 수 | PASS |
| USB 부분 쓰기, 전송 큐 가득 참, 재연결 헤더 재전송 | PASS |
| 오래된 샘플 무효화 | PASS |

자체 C 소스의 Tab 없음, 100자 줄 길이, LF, 마지막 줄바꿈을 확인했습니다.
핀이 현재 Notion 핀맵과 일치하고 앱 타이머가 TIMER2 하나인지 확인했습니다.
위 검증은 mock이 모델링한 동작에 대한 결과이며 실제 IRQ 선점·USB 드라이버 동시성은
실기에서 별도로 확인해야 합니다.

## 실제 PCB에서 남은 확인

- NCS v3.4.1 Pristine Build: Kconfig, devicetree 바인딩, ARM 링크 완료.
- P0.05가 버튼 3초 미만에서는 LOW, 3초 이후 HIGH인지 측정.
- 배터리 단독 전원에서 버튼을 놓아도 전원이 유지되는지 확인.
- LED1 토글 500 ms, LED2 토글 100 ms 확인.
- 센서별 INIT=OK(0), 가속도 ADDR=0x19 / WHO=0x44, FDC ID=0x1004 확인.
- 배터리 출력 전압과 멀티미터 비교.
- 정지 상태에서 중력 방향 축이 약 +/-1000 mg인지 확인.
- FDC 패드 접촉에 따른 pF 변화와 포화 코드 확인.
- USB 제거·연결·터미널 재연결 중 LED/버튼 동작 유지 확인.
- 1분 이상 로깅하며 time_ms·샘플 age·missed_ticks·usb_drop 확인.

이 패키지에는 hex/elf 또는 실제 측정 완료를 뜻하는 결과가 포함되지 않습니다.
