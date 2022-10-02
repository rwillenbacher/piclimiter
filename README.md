# piclimiter
piclimiter can be used to throttle pictures of a YUV or HEVC stream to a picture rate

## usage
```
piclimiter options ( \* are mandatory ):
-in <file>            | \* input file, '-' for stdin
-out <file>           | \* output file, '-' for stdout
-mode <mode>          | \* operation mode, 'yuv' or 'hevc'
-timescale <int>      | timescale
-picduration <int>    | picture duration
-buffer_pics <int>    | picture buffer size for yuv mode
-pic_buffer_size      | size of a yuv picture in bytes for yuv mode.
                      | yuv420p would be ( ( width * height * 3 ) / 2 )
-buffer_kbit <int>    | video buffer size for hevc mode
-verbose <int>        | (0-2)
```

## examples

### To limit a raw YUV file or pipe stream to a framerate ( to slow down an encoder the pipe goes to ) try this:

- YUV is 352x288 420P 8 bit, so a single YUV picture is 152064 bytes ( (width\*height\*3)/2 )
- Target picture rate is 25, so duration is 1 and timescale is 25

```
piclimiter_app.exe -mode yuv -in - -out - -timescale 25 -picduration 1 -pic_buffer_size 152064 -verbose 0 | ./x265.exe --input - --input-res 352x288 --fps 25 --output test.265 --vbv-maxrate 200 --vbv-bufsize 400
```

### To limit a HEVC file or pipe to a framerate ( to slow down sending downstream or blocking a source the pipe comes from ) try this:

- HEVC streams needs Access Unit Delimiter, --aud with x265
- Framerate is set to ~23.9760, so duration is 1001 and timscale is 24000

```
./x265.exe --input foreman_cif.yuv --input-res 352x288 --fps 25 --output - --vbv-maxrate 200 --vbv-bufsize 400 --aud | piclimiter_app.exe -mode hevc -in - -out test.265 -timescale 24000 -picduration 1001 -buffer_kbit 400 -verbose 0
```

## Notes

For short sequences or long runs of really small pictures HEVC limiting works not so well.
