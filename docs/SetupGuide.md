# Setup Guide

<!-- TOC -->
* [Setup Guide](#setup-guide)
  * [Prerequisites](#prerequisites)
  * [Install dependencies](#install-dependencies)
    * [A. Linux / WSL / VM](#a-linux--wsl--vm)
    * [B. Docker](#b-docker)
    * [C. Docker Compose](#c-docker-compose)
  * [Usage](#usage)
    * [Recompile IDO](#recompile-ido)
    * [Extract assets from base rom(s)](#extract-assets-from-base-roms)
    * [Build the ROM](#build-the-rom)
  * [In-depth build process](#in-depth-build-process)
    * [Requirements](#requirements)
    * [Environment](#environment)
    * [C Compiler](#c-compiler)
    * [Assembly Preprocessor](#assembly-preprocessor)
    * [Code build process](#code-build-process)
    * [Asset/data build process](#assetdata-build-process)
    * [Building the final ROM](#building-the-final-rom)
<!-- TOC -->

## Prerequisites

* clone this repository to a directory of your choosing
* place an unmodified copy of your existing NTSC (US) ROM inside the root of this repository with the name `baserom.u.z64`
  * optionally: if you want to extract assets from NTSC (JP) or PAL (EU), **additionally** place `baserom.j.z64` and `baserom.e.z64` into the root dir

## Install dependencies

You can choose one of the following installation methods to get all needed dependencies.

### A. Linux / WSL / VM

**Install build dependencies**

The requirements for Debian / Ubuntu should be:

```bash
sudo apt-get update
sudo apt-get install binutils-mips-linux-gnu make git python3
sudo apt-get install libcapstone-dev pkg-config
```

If you don't have host development tools already installed then you will also need to install `build-essential`:

```bash
sudo apt-get install build-essential
```

Additionally, [qemu-irix](https://github.com/n64decomp/qemu-irix/releases) is needed. Download the package to a desired location and install with:

```bash
sudo dpkg -i qemu-irix-2.11.0-2169-g32ab296eef_amd64.deb
```

### B. Docker

* change directory to the cloned repo
* build the docker image: `docker build -t goldeneye .`
* connect to the container: `docker run --rm -it -v $(pwd):/home/dev goldeneye` (alternatively, replace `$(pwd)` with the absolute path of your directory)

### C. Docker Compose

* run and connect to shell in project root: `docker-compose run ge007 bash`

Alternatively, you can start the container with `docker compose up` and connect to the container in e.g. docker desktop

## Usage

### Recompile IDO

Recompile IDO for your platform to increase compilation speed

```bash
cd tools/ido5.3_recomp
make
```

Be careful! If you previously compiled GoldenEye on a different operating system or CPU architecture, the binaries (gzip, n64cksum) that were compiled will be incompatible.
You must delete them.

There may be a "dubious ownership" error from Git, and it may say it fails to detect the Git repository.
Run `git status` to see how to fix it.

### Extract assets from base rom(s)

* to have the files in correct location, see [prerequisites](#prerequisites)

To extract the NTSC (US) base rom assets run the following from root directory:

```bash
./scripts/extract_baserom.u.sh
```

> **Extracting NTSC (US) base rom assets is mandatory before extracting NTSC (JP) or PAL (EU) assets.**

To extract the NTSC (JP) base rom assets run the following from root directory:

```bash
./scripts/extract_baserom.u.sh && ./scripts/extract_diff.j.sh
```

To extract the PAL (EU) base rom assets run the following from root directory:

```bash
./scripts/extract_baserom.u.sh && ./scripts/extract_diff.e.sh
```

Other options to extract base rom assets or extract diff:

```bash
./scripts/extract_baserom.u.sh /path_to/rom.n64 # ROM in another directory
./scripts/extract_baserom.u.sh /mnt/e/Goldeneye.n64 # ROM located on EverDrive
./scripts/extract_baserom.u.sh files # Extract files only
./scripts/extract_baserom.u.sh images # Extract images only
```

**Note: If you are upgrading from an old repository, run:**

```bash
./scripts/clean_baserom.sh && ./scripts/extract_baserom.u.sh && make clean
```

### Build the ROM

Run `make` from root directory to build the ROM (defaults to `VERSION=US`).

```bash
make
```

If all goes well, resulting artifacts can be found in the `build` directory and the following text should be printed:

```bash
build/u/ge007.u.z64: OK
```

Other examples:

```bash
make VERSION=JP -j4       # build NTSC (JP) version instead with 4 jobs
make VERSION=EU COMPARE=0 # build PAL (EU) version but do not compare ROM hashes
```

The full list of configurable variables are listed below, **with the default being the first listed**:

* ``VERSION``: ``US``, ``JP``, ``EU``
* ``COMPARE``: ``1`` (compare ROM hash), ``0`` (do not compare ROM hash)
* ``IDO_RECOMP``: ``YES`` (build with IDO recomp), ``NO`` (build using [qemu-irix](https://github.com/n64decomp/qemu-irix/releases))
* ``FINAL``: ``YES`` (builds final version with -O2 optimization), ``NO`` (debug)
* ``VERBOSE``: ``0`` (quiet), ``1``

Additional documentation of the build process can be found in the next section.

## In-depth build process

This section explains the details of the build process.

### Requirements

It is required that `qemu-irix` be installed and available.

### Environment

The build uses the `US` version by default. Available options are `US`, `EU`, and `JP`. For example

    make clean VERSION=JP
    make VERSION=JP

### C Compiler

The c compiler can be found in `tools/irix/root/usr/bin/cc`. This splits the compilation process into several steps.

- cfe: compiler front end
- uopt: ?
- ugen: ?

### Assembly Preprocessor

There is a (much forked) preprocessor used by many N64 projects, found in `tools/asm-processor/asm_processor.py`. This searches for a line
beginning with `GLOBAL_ASM(` and a subsequent line beginning with `)` and treats everything in between as assembly. This is bundled with any
c code in the file and sent to the compiler.

### Code build process

`src` and `src/game`: .c and .s files are compiled into .o files

`src/libultra`: .c and .s files are compiled into .o files

### Asset/data build process

Before compilation begins, assets are converted into .c files. This file is then compiled using the c compiler in the usual manner.
Once an .o file exists, it is converted to an .elf file using the toolchain `-ld` program, and a .ld file specification explaining
where the ELF sections should be arranged in the file (and also which sections to exclude). The toolchain `-objcopy` program is
then used to dump the data in the .elf file into a similar .bin file.

The .bin file is then compressed using the standard compression program to produce a .rz file.

For compilation, having a .c file is not necessary as long the correct .bin file is available. This can be created from the extract script.

Each obseg asset category has its own `Makefile` in the obseg folder.

Once the .rz files exist for an asset category, they can be bundled together into an .o file. All obseg assets are bundled
in `assets/obseg/ob_seg.s` and music is bundled in `assets/music/music.s`.

### Building the final ROM

Once all code and assets are compiled into .o files, these are combined into one .elf file. The layout of the object files
is given by the `ge007.*.ld` files in the root of the project. For a list of individual methods, assets, and files see
the map file in `build/[uje]/ge007.*.map` (where `*` is the country code, `u`, `e`, or `j`).

The toolchain `-objcopy` program is then used to create the bundled .bin of the entire ROM.

The final step is to run the `tools/n64cksum` program on the .bin file to create the final .z64.
