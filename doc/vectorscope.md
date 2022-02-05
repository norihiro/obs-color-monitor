# Vectorscope

## Introduction

Vectorscope shows population of each chrominance components of a source to help color correction especially *hue* and *saturation*.

## Properties

### Source

Selects one of Program, Preview, Scene, or Source.
Default is Program.

### Scale

Scale factor before calculating vectorscope.
The width and height of the source will be scaled by this number.
Main purpose is to shorten the rendering time.
Larger value will degrade the accuracy and intensity.
For example, if you change scale from `1` to `2`, you need to increase intensity from `1` to `4` to get the same intensity.
Default is `2`, which means width and height are both scaled by half. Available range is an integer number beween `1` - `128`.

### Intensity

Intensity of each pixel.
Population for each color is multiplied by intensity and drawn on the scope.
Larger value will increase the visibility of less population colors.
Default is `25`. Available range is an integer number between `1` - `255`.

### Graticule

Choice of graticule.

| Choice | Description |
|--------|-------------|
| `None` | No graticule will be displayed. |
| `Amber` | 6 boxes for primary colors, these labels, and skin tone line will be displayed in amber. |
| `Amber + IQ` (default) | 6 boxes for primary colors, these labels, and I-Q lines will be displayed in amber. |
| `Green` | 6 boxes for primary colors, these labels, and skin tone line will be displayed in green. |
| `Green + IQ` | 6 boxes for primary colors, these labels, and I-Q lines will be displayed in green. |

### Skin tone color

The color for the skin tone line.
Default is `#CBAB99`.
If you set black or white, skin tone line will be hidden.

### Color space

Choice of color space; Auto, BT.601, or BT.709.
If Auto, the color space is retrieved from the settings of OBS Studio.
Coefficients for Cr and Cb, graticule, and skin tone line will be changed.
Default is Auto.

### Bypass

If you check this, image after the scaling will be displayed.

## Output

The output size is always `256x256` unless bypassed.
