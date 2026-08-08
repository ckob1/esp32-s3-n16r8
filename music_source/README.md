# Original music sources

This folder keeps original MP3 files for desktop use and for hosting through
cloud storage. The ESP32 firmware plays converted 16-bit PCM WAV files from
`data/music/`, and can also stream MP3 files listed in `data/cloud.txt`.

Convert a new track with:

```text
ffmpeg -y -i "source.mp3" -ac 1 -ar 16000 -sample_fmt s16 "..\data\music\short-name.wav"
```

Keep the WAV filename including extension at or under 24 characters, otherwise
`mkspiffs` fails with `SPIFFS_write error(-10010)` and creates an empty image.
