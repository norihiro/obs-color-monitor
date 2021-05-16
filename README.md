# OBS Color Monitor

## Introduction

This plugin provides three sources to monitor color balances.

- [Vectorscope](doc/vectorscope.md)
- [Waveform](doc/waveform.md)
- [Histogram](doc/histogram.md)

## Quick Usage

1. Install the plugin and boot OBS Studio.
1. Have your source to see vectorscope, waveform, or histogram, eg. a camera.
1. Create a new scene.
1. Create vectorscope, waveform, or histogram on the scene by clicking *+* button at the bottom of *Source* list.
1. At *Source* combo box, select your source. You can select both scene and source.
   1. If the plugin increases rendering time too much, increase *scale* to scale down the image before processing.
   1. *Bypass* checkbox will show the scaled image to ensure you've select the right source.
1. You may open a windowed projector of the scene so that you can switch preview and program scene while monitoring color.
