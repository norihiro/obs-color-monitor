# Waveform

## Introduction

Histogram shows population of red (R), green (G), blue (B) components.

X-axis corresponds to the value of color component. Bright color stays right, dark color stays left.
Y-axis shows the population. The Y-axis is automatically scaled to the maximum population.

## Properties

### Source

Selects one of Program, Scene, or Source.
Default is Program.

### Scale

Scale factor before calculating vectorscope.
The width and height of the source will be scaled by this number.
Main purpose is to shorten the rendering time.
Larger value will degrade the accuracy and intensity.
Default is `1`, which means not to scale. Available range is an integer number beween `1` - `128`.

### Display

Choice of displaying mode; Overlay, Stack, or Parade.
| Display | Description |
|---------|-------------|
| Overlay (default) | Each color components will be displayed on the same place by each color. |
| Stack | R, G, B histograms are displayed from the top to the bottom. |
| Parade | R, G, B histograms are displayed from the left to the right. |

### Height

Height of the output.
Default is `200`. Available range is an integer number between `50` - `2048`.

### Log scale

Check this to plot in log scale.

### Bypass

If you check this, image after the scaling will be displayed.

## Output

Width is scaled width of the source for Overlay and Stack display, 3-times of that for Parade, scaled height for bypass.
Height is controlled by the Height property for Overlay and Parade display, 3-times of that for Stack, scaled height for bypass.
