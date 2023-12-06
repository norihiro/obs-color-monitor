# Waveform

## Introduction

Waveform shows population of red (R), green (G), blue (B) components for each row of the specified source
for color correction especially *brightness* or *luminance* and *gain* of each components.

X-axis corresponds to the X-axis of the source, ie row.
Y-axis corresponds to the value of color component for each row. Bright color stays upper, dark color stays lower.
The intensity shows the population of each color.

## Properties

### Source

Selects one of Program, Main view, Preview, Scene, or Source.
Default is Program.

### Scale

Scale factor before calculating waveform.
The width and height of the source will be scaled by this number.
Main purpose is to shorten the rendering time.
Larger value will degrade the accuracy and intensity.
For example, if you change scale from `1` to `2`, you need to increase intensity from `1` to `2` to get the same intensity.
Default is `2`, which means width and height are both scaled by half. Available range is an integer number beween `1` - `128`.

### Display

Choice of displaying mode; Overlay, Stack, or Parade.
| Display | Description |
|---------|-------------|
| Overlay (default) | Each color components will be displayed on the same place by each color. |
| Stack | R, G, B waveforms are displayed from the top to the bottom. |
| Parade | R, G, B waveforms are displayed from the left to the right. |

If setting components to YUV, CrYCb are displayed instead of RGB, respectively.
If setting components to Luma only, this property is not effective.

### Components
Choice of components to be displayed.
| Components | Description |
|------------|-------------|
| RGB | R, G, B components are displayed. |
| Luma | Luminance component (*Y*) is displayed. |
| Chroma | Chrominance components (*Cr* and *Cb*) are displayed. |
| YUV | Both luminance an chrominance components are displayed. |

### Color space

Choice of color space; Auto, BT.601, or BT.709.
If Auto, the color space is retrieved from the settings of OBS Studio.
Coefficients for Luminance, Cr and Cb components will be changed.
Default is Auto. This property is only available if the component property is Luma, Chroma, or YUV.

### Intensity

Intensity of each pixel.
Population for each color is multiplied by intensity and drawn on the scope.
Larger value will increase the visibility of less population colors.
Default is `51`. Available range is an integer number between `1` - `255`.

### Graticule

Choice of graticule.

| Choice | Description |
|--------|-------------|
| `None` | No graticule will be displayed. |
| `0%, 100%` | 2 lines will be displayed at 0% and 100%. |
| `0%, 50%, 100%` | 3 lines will be displayed. |
| `each 25%` | 5 lines will be displayed. |
| `each 20%` | 6 lines will be displayed. |
| `each 10%` | 11 lines will be displayed. |

### Bypass

If you check this, image after the scaling will be displayed.

## Output

Width is scaled width of the source for Overlay and Stack display, 3-times of that for Parade, scaled height for bypass.
Height is fixed 256 pixels for Overlay and Parade display, 768 pixels for Stack, scaled height for bypass.
