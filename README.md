# axiomeos

a simple, lightweight operating system

(this repo contains kernel, basic utilities, etc.)

## Quick Start

* Clone repo: use `git clone --recurse-submodules https://codeberg.org/akaruineko/axiomeOS`
* Report a bug: Write an issue on GitHub Issues
* Build the kernel: use `make`

## Branches

The project uses two main branches:

- `development` — active development branch. All pull requests should target this branch.
- `stable` — stable releases only. It is periodically updated from `development`.

If you want to build the latest stable version, switch to the `stable` branch:

```sh
git switch stable
```

## Essential Documentation

- [Building the Toolchain](docs/toolchain.md) — install the required dependencies, build the `x86_64-elf` cross-compiler, and set up QEMU.
- [Building axiomeOS](docs/building.md)
