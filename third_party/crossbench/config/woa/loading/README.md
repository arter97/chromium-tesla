This folder contains configs for the loading benchmark.

### Run the benchmark on live sites

```
./cb.py loading --page-config config/woa/loading/page_config_phone.hjson --separate --probe-config config/woa/loading/probe_config.hjson --browser <browser>
```

The browser can be `android:chrome-canary`, `android:chrome-stable` etc. See crossbench docs for the full list of options.

### Record a WPR archive

Uncomment the `wpr: {},` line in the probes config and run the same command as above. The archive will be located in `results/latest/archive.wprgo`

### Replay from a WPR archive

Make sure the `wpr: {},` line in the probes config is commented out, then run the following command:

```
./cb.py loading --page-config config/woa/loading/page_config_phone.hjson --separate --probe-config config/woa/loading/probe_config.hjson --browser <browser> --network <path to archive>
```

