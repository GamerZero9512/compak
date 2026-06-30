# compak
compak is a simple, minimal source-based package manager for POSIX systems.

![last commit](https://img.shields.io/github/last-commit/gamerzero9512/compak?color=blue)
[![MIT license](https://img.shields.io/badge/license-MIT-yellow)](https://raw.githubusercontent.com/GamerZero9512/compak/refs/heads/main/LICENSE)
![Build status](https://img.shields.io/github/actions/workflow/status/gamerzero9512/compak/.github%2Fworkflows%2Fc-cpp.yml)

```
usage: compak <options>

options:
  --help,      -?           Show this help message
  --install,   -i <package> Install the specified package
  --remove,    -r <package> Uninstall the specified package
  --list,      -l           List installed packages
    --name                  Display only package names
    --full                  Display package names and descriptions
  --update,    -u <package> Update the specified package
  --update-all              Update all installed packages
  --clean                   Remove compak temporary files
  --package,   -p <folder>  Pack folder into compak-ready archive
  --view,      -v <package> View info about a package
    --raw                   View the raw package manifest
    --simple                View a simple overview of the package
```

compak requires some libraries, so run this if you're on Debian:
```
sudo apt update && sudo apt install openssl libarchive-dev libcurl4-openssl-dev libbz2-dev libpsl-dev automake autoconf libtool m4 libidn2-dev libbrotli-dev libnghttp2-dev libldap-dev
```

To install compak, clone the source tree and run `make install`:
```
git clone https://github.com/gamerzero9512/compak.git
cd compak
make install
```

If this fails, please [open an issue](https://github.com/GamerZero9512/compak/issues/new) with the error details.

compak uses these libraries:
- [libarchive](https://github.com/libarchive/libarchive)
- [parson](https://github.com/kgabis/parson)
- [libcurl](https://github.com/curl/curl/)
