#include "battery_adc.h"

#include <errno.h>
#include <stdbool.h>

#include <zephyr/drivers/adc.h>

static const struct adc_dt_spec s_adc = ADC_DT_SPEC_GET_BY_NAME(DT_PATH(zephyr_user), battery);
static bool s_calibrate;

int battery_adc_init(void)
{
    if (!adc_is_ready_dt(&s_adc)) {
        return -ENODEV;
    }
    s_calibrate = true;
    return adc_channel_setup_dt(&s_adc);
}

int battery_adc_read_raw(int16_t *p_raw)
{
    if (p_raw == NULL) {
        return -EINVAL;
    }
    struct adc_sequence sequence = {
        .buffer = p_raw,
        .buffer_size = sizeof(*p_raw),
        .calibrate = s_calibrate,
    };
    int ret = adc_sequence_init_dt(&s_adc, &sequence);
    if (ret == 0) {
        ret = adc_read_dt(&s_adc, &sequence);
    }
    if (ret == 0) {
        s_calibrate = false;
    }
    return ret;
}

int battery_adc_raw_to_mv(int16_t raw, int32_t *p_mv)
{
    if (p_mv == NULL) {
        return -EINVAL;
    }
    *p_mv = raw;
    return adc_raw_to_millivolts_dt(&s_adc, p_mv);
}
