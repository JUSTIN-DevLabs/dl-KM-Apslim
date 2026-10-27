#ifndef APSLIM_BATTERY_ADC_H_
#define APSLIM_BATTERY_ADC_H_
#include <stdint.h>
int battery_adc_init(void);
int battery_adc_read_raw(int16_t *p_raw);
int battery_adc_raw_to_mv(int16_t raw, int32_t *p_mv);
#endif
