# bela_qtm_track_playback
This is a simple Bela render.cpp that plays an audio file and sends markers to QTM


# Config

Add a flac or wav file to `res`
Specify that path in `gFilename`

Markers for the start and end of playback can be configred by specifying `gPlaybackStartMarker` and `gPlaybackEndMarker`.

The tick marker can be configured to mark at a specified interval in `ms` with `gTickInterval` and the label it will use is specified by `gTickMarker`.
