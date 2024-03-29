# False Color

## Introduction

False Color shows intensity in different colors to help adjust exposure.

The false color is hardcoded in an effect file `data/falsecolor.effect`.

RGB color `#000000` corresponds to 0&nbsp;IRE `#FFFFFF` corresponds to 100&nbsp;IRE.

<img src="falsecolor.svg" alt="color table"/>

## Properties

### Source
(Not available for False Color Filter.)

Selects one of Program, Main view, Preview, Scene, or Source.
The default is Program.

### Color space

Choice of color space; Auto, BT.601, or BT.709.
If Auto, the color space is retrieved from the settings of OBS Studio.
Coefficients to convert from RGB to Luminance will be changed.
Default is Auto.

