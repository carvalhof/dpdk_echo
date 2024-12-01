# DPDK echo

Follow these instructions to build the DPDK echo using DPDK 23.11 and CloudLab nodes

## DPDK

```bash
./dpdk.sh
```

## Building

> **Make sure that `PKG_CONFIG_PATH` is configured properly.**

```bash
git clone https://github.com/carvalhof/dpdk_echo
cd dpdk_echo
PKG_CONFIG_PATH=$HOME/lib/x86_64-linux-gnu/pkgconfig make
```

## Running

> **Make sure that `LD_LIBRARY_PATH` is configured properly.**

```bash
sudo LD_LIBRARY_PATH=$HOME/lib/x86_64-linux-gnu ./build/dpdk_echo -a 41:00.0 -n 4 -c 0xff -- -n $CORES
```

> **Example**

```bash
sudo LD_LIBRARY_PATH=$HOME/lib/x86_64-linux-gnu ./build/dpdk_echo -a 41:00.0 -n 4 -c 0xff -- -n 1
```

### Parameters

- `$CORES` : the number of cores
