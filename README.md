# OBS Color Monitor

## Introduction

This plugin provides three sources to monitor color balances.

- [Vectorscope](doc/vectorscope.md)
- [Waveform](doc/waveform.md)
- [Histogram](doc/histogram.md)
- [Zebra](doc/zebra.md)
- [False Color](doc/falsecolor.md)

In addition, a dock widget is available.
- [Dock](doc/dock.md)

## Quick Usage

### Dock
1. Install the plugin and boot OBS Studio.
1. Click `Tools` and `New Scope Dock...` in the menu of OBS Studio.
1. Input the name, optionally set the source to monitor, and clock OK. You will see a new dock containing vectorscope, waveform, and histogram for the program.

### Projector View
1. Install the plugin and boot OBS Studio.
1. Have your source to see vectorscope, waveform, or histogram, eg. a camera.
1. Create a new scene.
1. Create vectorscope, waveform, or histogram on the scene by clicking `+` button at the bottom of `Source` list.
1. At `Source` combo box, select your source. You can select both scene and source.
   1. If the plugin increases rendering time too much, increase `scale` to scale down the image before processing.
   1. `Bypass` checkbox will show the scaled image to ensure you've select the right source.
1. You may open a windowed projector of the scene so that you can switch preview and program scene while monitoring color.

## Known Issue
- Performance is much slower on macOS than on Linux.
