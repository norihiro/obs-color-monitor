# False Color

## Introduction

False Color shows intensity in different colors to help adjust exposure.

The false color is hardcoded in an effect file `data/falsecolor.effect`.

RGB color `#000000` corresponds to 0&nbsp;IRE `#FFFFFF` corresponds to 100&nbsp;IRE.

| IRE level | Color |
| --------- | ----- |
| level < 2 | Bright purple |
| 2 <= level < 10 | Blue |
| 10 <= level < 20 | Light blue |
| 20 <= level < 42 | Dark grey |
| 42 <= level < 48 | Green |
| 48 <= level < 52 | Medium grey |
| 52 <= level < 58 | Pink |
| 58 <= level < 78 | Light grey |
| 78 <= level < 84 | Dark yellow |
| 84 <= level < 94 | Yellow |
| 94 <= level < 100 | Orange |
| level = 100 | Red |

## Properties

### Source
(Not available for False Color Filter.)

Selects one of Program, Preview, Scene, or Source.
The default is Program.

### Color space

Choice of color space; Auto, BT.601, or BT.709.
If Auto, the color space is retrieved from the settings of OBS Studio.
Coefficients for Luminance, Cr and Cb components will be changed.
Default is Auto. This property is only available if the component property is Luma, Chroma, or YUV.

