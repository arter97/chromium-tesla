# Crossbench

Crossbench is a cross-browser/cross-benchmark runner to extract performance
numbers.

Mailing list: <crossbench@chromium.org>

Issues/Bugs: [Tests > CrossBench](https://bugs.chromium.org/p/chromium/issues/list?q=component%3ATest%3ECrossBench)

Supported Browsers: Chrome/Chromium, Firefox, Safari and Edge.

Supported OS: macOS, Android, linux and windows.

## Basic usage:
### Chromium Devs (with a full chromium checkout)
Use the `./cb.py` script directly to run benchmarks (requires chrome's
[vpython3](https://chromium.googlesource.com/infra/infra/+/main/doc/users/vpython.md))

### Standalone installation
- Use `pip install crossbench`,
- or use the "poetry" package manager, see the [development section](#development).

Run the latest [speedometer benchmark](https://browserbench.org/Speedometer/)
20 times with the system default browser (chrome-stable):
```bash
# Run chrome-stable by default:
./cb.py speedometer --repeat=20

# Compare chrome browser versions and a local chrome build on jetstream:
./cb.py jetstream --browser=chrome-stable --browser=chrome-m90 --browser=$PATH
```

Profile individual line items (with pprof on linux):
```bash
./cb.py speedometer --probe='profiling' --separate
```

Use a custom chrome build and only run a subset of the stories:
```bash
./cb.py speedometer --browser=$PATH --probe='profiling' --story='jQuery.*'
```

Profile a website for 17 seconds on Chrome M100 (auto-downloading on macOS and linux):
```bash
./cb.py loading --browser=chrome-m100 --probe='profiling' --url=www.cnn.com,17s
```

Collect perfetto data from loading separate websites on multiple attached
android devices using the device ID or unique device names
(see `adb devices -l`):

```bash
./cb.py loading --probe-config=./config/probe/perfetto.probe.config.example.hjson \
    --browser='Pixel_4:chrome-stable' --browser='AA00BB11:chrome-stable' \
    --parallel=platform \
    --url=https://theverge.com,15s,https://cnn.com,15s  --separate
```


## Main Components

### Browsers
Crossbench supports running benchmarks on one or multiple browser configurations.
The main implementation uses selenium for maximum system independence.

You can specify a browser with `--browser=<name>`. You can repeat the
`--browser` argument to run multiple browser. If you need custom flags for
multiple browsers use `--browser-config` (or pass simple flags after `--` to
the browser).

```bash
./cb.py speedometer --browser=$BROWSER -- --enable-field-trial-config
```

#### Browser Config File
For more complex scenarios you can use a
[browser.config.hjson](config/browser.config.example.hjson) file.
It allows you to specify multiple browser and multiple flag configurations in
a single file and produce performance numbers with a single invocation.

```bash
./cb.py speedometer --browser-config=config.hjson
```

The [example file](config/browser.config.example.hjson) lists and explains all
configuration details.

#### Remote WebDriver Interface
Crossbench also supports benchmarking browsers on remote machines
running Linux or ChromeOS, via SSH.
The remote machine is expected to have at least two ports open to the host:
(a) the SSH port (typically `22`), and
(b) the WebDriver port (typically `9515`).
The [remote browser example](config/remote.browser.config.example.hjson)
describes the configuration details for both Linux and ChromeOS.

On ChromeOS, Crossbench requires
[ChromeDriver](https://developer.chrome.com/docs/chromedriver/get-started/chromeos/)
to interact with Chrome,
and [Autotest](https://chromium.googlesource.com/chromiumos/third_party/autotest/+/HEAD/docs/user-doc.md)
for creating ephemeral sessions for testing.
Both ChromeDriver and Autotest are pre-installed on ChromeOS test images.
Detailed instructions for flashing Chromebooks with test images are provided at:
go/arc-setup-dev-mode-dut#usb-cros-test-image.

### Probes
Probes define a way to extract arbitrary (performance) numbers from a
host or running browser. This can reach from running simple JS-snippets to
extract page-specific numbers to system-wide profiling.

Multiple probes can be added with repeated `--probe='XXX'` options.
You can use the `describe probes` subcommand to list all probes:

```bash
# List all probes:
./cb.py describe probes

# List help for an individual probe:
./cb.py describe probe v8.log
```

#### Inline Probe Config
Some probes can be configured, either with inline json when using `--probe` or
in a separate `--probe-config` hjson file. Use the `describe` command to list
all options.

```bash
# Get probe config details:
./cb.py describe probe v8.log

# Use inline hjson to configure a probe:
./cb.py speedometer --probe='v8.log:{prof:true}'
```

#### Probe Config File
For complex probe setups you can use `--probe-config=<file>`.
The [example file](config/probe.config.example.hjson) lists and explains all
configuration details. For the specific probe configuration properties consult
the `describe` command.

### Benchmarks
Use the `describe` command to list all benchmark details:

```bash
# List all benchmark info:
./cb.py describe benchmarks

# List an individual benchmark info:
./cb.py describe benchmark speedometer_3.0

# List a benchmark's command line options:
./cb.py speedometer_3.0 --help
```

### Stories
Stories define sequences of browser interactions. This can be simply
loading a URL and waiting for a given period of time, or in more complex
scenarios, actively interact with a page and navigate multiple times.

Use `--help` or describe to list all stories for a benchmark:

```bash
./cb.py speedometer --help
```

Use `--stories` to list individual story names, or use regular expression
as filter.

```bash
./cb.py speedometer --browser=$BROWSER --stories='.*Angular.*'
```


## Development

## Setup
This project uses [poetry](https://python-poetry.org/) deps and package scripts
to setup the correct environment for testing and debugging.

```bash
# a) On debian:
sudo apt-get install python3.10 python3-poetry
# b) With python 3.8 to 3.10 installed already:
pip3 install poetry
```

Check that you have poetry on your path and make sure you have the right
`$PATH` settings.
```bash
poetry --help || echo "Please update your \$PATH to include poetry bin location";
# Depending on your setup, add one of the following to your $PATH:
echo "`python3 -m site --user-base`/bin";
python3 -c "import sysconfig; print(sysconfig.get_path('scripts'))";
```

Install the necessary dependencies from the lock file using poetry:

```bash
# Select the python version you want to use (3.8 to 3.10):
poetry env use 3.10
poetry install

# For python 3.11 you have to skip pytype support:
poetry env use 3.11
poetry install --without=dev-pytype
```

## Crossbench
For local development / non-chromium installation you should
use `poetry run cb ...` instead of `./cb.py ...`.

Side-note, beware that poetry eats up an empty `--`:

```bash
# With cb.py:
./cb.py speedometer ... -- --custom-chrome-flag ...
# With poetry:
poetry run cb speedometer ... -- -- --custom-chrome-flag ...
```

## Tests
```
poetry run pytest
```

Run detailed test coverage:
```bash
poetry run pytest --cov=crossbench --cov-report=html
```

Run [pytype](https://github.com/google/pytype) type checker:
```bash
poetry run pytype -j auto . 
```
