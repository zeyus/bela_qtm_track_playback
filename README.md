# bela_qtm_track_playback
This is a simple Bela render.cpp that plays an audio file and sends markers to QTM


# Config

Add a flac or wav file to `res`
Specify that path in `gFilename`

Markers for the start and end of playback can be configred by specifying `gPlaybackStartMarker` and `gPlaybackEndMarker`.

Markers for the start and end of a song (often not the same as the file start and end) can be configred by specifying `gSongStartMarker` and `gSongEndMarker`.
These require a corresponding sample number to be specified in `gSongStartSample` and `gSongEndSample`.

The tick marker can be configured to mark at a specified interval in `ms` with `gTickInterval` and the label it will use is specified by `gTickMarker`.
