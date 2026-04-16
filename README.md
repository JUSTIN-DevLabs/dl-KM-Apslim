# dl-KM-Apslim

## Overview
Apslim project repository for KM.

## Structure
- `docs/` : project documents
- `firmware/ble/` : BLE firmware modules
- `firmware/uart/` : UART firmware modules
- `firmware/adc/` : ADC firmware modules
- `firmware/common/` : common firmware code
- `app/` : app source code
- `test/` : test code and logs
- `shared/` : shared protocol, config, and common definitions
- `tools/` : PC tools and scripts

## Notes
Start with one repository for the whole project. If the project grows significantly, split it into `-fw`, `-app`, and `-tool` repositories later.
