# Zebra

## Introduction

Zebra indicates bright areas to help adjust exposure.
This plugin provides lower threshold and upper threshold.

## Properties

### Source
(Not available for Zebra Filter.)

Selects one of Program, Preview, Scene, or Source.
The default is Program.

### Threshold (lower, higher)

Specify the level of the intensity where the zebra should be displayed.
The default is `75%` and `100%`.
The values are inclusive; the zebra will be also shown even if the intensity is same as the specified threshold.
RGB color `#000000` corresponds to 0% `#FFFFFF` corresponds to 100%.

### Color space

Choice of color space; Auto, BT.601, or BT.709.
If Auto, the color space is retrieved from the settings of OBS Studio.
Coefficients for Luminance, Cr and Cb components will be changed.
Default is Auto. This property is only available if the component property is Luma, Chroma, or YUV.
