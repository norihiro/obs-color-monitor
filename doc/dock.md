# Color Scope Dock

## Introduction

Color Scope Dock shows ROI (region of interest), and all other scopes including vectorscope, waveform, histogram.

## Opening the Color Scope Dock

To open the dock,
Click `Tools` and `New Scope Dock...` in the menu of OBS Studio.
Input the name and clock OK.

## ROI Mouse Interaction

ROI is a quick tool to select a rectangle to pick for the scope calculation.

### Setting the region

Dragging with left-button will select a region.

### Moving the region

After setting the region first,
drag the center of the region to move the region.
When you hover the region, green outline will appear to indicate you can drag the region.

### Resizing the region

After setting the region first,
drag the edge of the region to resize the edge of the region.
When you move your mouse close to the edge, a square will appear in addition to the region outline. The square indicates you can resize the edge.

## Selecting Scopes
The dock provides ROI and some types of scopes.
All scopes are on by default.
Each scope can be turned off from a right-click menu.

## ROI Properties

### Source

Selects one of Program, Preview, Scene, or Source.
Default is Program.

### Scale

Scale factor before calculating vectorscope.
The width and height of the source will be scaled by this number.
Main purpose is to shorten the rendering time.
Larger value will degrade the accuracy and intensity.
For example, if you change scale from `1` to `2`, you need to increase intensity of Vectorscope from `1` to `4`, intensity of Waveform from `1` to `2` to get the same intensity. The intensity of Histogram won't be affected.
Default is `2`, which means width and height are both scaled by half. Available range is an integer number beween `1` - `128`.

### Interleave

This property controls whether the calculation will be interleaved or not.
Main purpose is to hide the rendering time.
If set to `0`, interleave won't happen. Every frame will be processed.
If set to `1`, a frame for each 2 frames will be processed.
In this setting, UV channel calculation and staging (sending data from GPU to CPU) will happen at odd frames, counting each color for each scope will happen at even frames.
Default is `1`.
