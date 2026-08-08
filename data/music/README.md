# Music files

Put 16-bit PCM WAV files in this folder:

```text
data/music/
  my_track.wav
```

Format requirements:

- Format: WAV / PCM
- Bit depth: 16-bit
- Channels: mono or stereo
- Sample rate: 16000 / 22050 / 44100 Hz (driver switches I2S clock automatically)
- Max LittleFS size: ~7.9 MB (all files combined)
- Current tracks use about 5.6 MB, leaving about 2.4 MB.
- LittleFS 允许长文件名 (最长 255 字符), 不再有 mkspiffs 的 24 字符限制。

Upload the filesystem after adding music:

```text
pio run -t uploadfs --upload-port COM3
```

Local LittleFS playback only supports WAV, so convert local MP3 files to WAV first:

```text
ffmpeg -y -i input.mp3 -ac 1 -ar 16000 -sample_fmt s16 output.wav
```

The original MP3 can be kept in `music_source/` for desktop playback or future
MP3 decoding work.

## Cloud playlist (MP3, does not use LittleFS)

Create `data/cloud.txt` at the project root, one track per line:

```text
Track Name|https://example.com/track.mp3
```

Then run `pio run -t uploadfs --upload-port COM3`. The firmware streams the MP3
over HTTP/HTTPS and decodes it with libhelix, so the audio file itself is never
stored on the board. Use a stable direct-download URL (GitHub releases, jsDelivr,
Cloudflare R2, or your own server). Share-page links such as Baidu/Aliyun cloud
drive do not work because they return HTML instead of MP3.
