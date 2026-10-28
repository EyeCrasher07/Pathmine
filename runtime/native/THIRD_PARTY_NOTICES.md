# Native dependency notices

The optional NVIDIA GPU architecture query uses the NVML declarations from
the pinned NVIDIA go-nvml package. The installed Linux driver supplies
`libnvidia-ml.so.1` at runtime. Its header carries this notice:

```text
/*** NVML VERSION: 12.9.40 ***/
```
