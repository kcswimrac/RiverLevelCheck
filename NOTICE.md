# Third-party notices

## Montserrat font

`src/fonts/*.h` are bitmap renderings of **Montserrat Medium** (Copyright 2011 The Montserrat
Project Authors, https://github.com/JulietaUla/Montserrat), licensed under the
**SIL Open Font License 1.1**: https://openfontlicense.org/open-font-license-official-text/

The `.ttf` file is not in this repository. `tools/make_vlw.py` regenerates the fonts from a copy
from Google Fonts or from LVGL's `scripts/built_in_font/Montserrat-Medium.ttf`.

## Root certificates

`noaa_roots.h` contains the public Amazon Root CA 1 and Starfield Services Root Certificate
Authority G2 certificates, as distributed in standard CA bundles.

## Data

Forecast data comes from the NOAA National Water Prediction Service API
(https://api.water.noaa.gov/nwps/v1/docs/). This device is a display aid, not a flood warning or
life-safety system. Official forecasts and warnings are at https://water.noaa.gov/gauges/VNSC1.
