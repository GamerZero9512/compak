# compak
compak is a simple, minimal source-based package manager for POSIX systems.

![last commit](https://img.shields.io/github/last-commit/gamerzero9512/compak?color=blue)
[![MIT license](https://img.shields.io/badge/license-MIT-yellow)](https://raw.githubusercontent.com/GamerZero9512/compak/refs/heads/main/LICENSE)
![Build status](https://img.shields.io/github/actions/workflow/status/gamerzero9512/compak/.github%2Fworkflows%2Fc-cpp.yml)

compak requires:
- glibc with C23 support
- OpenSSL 3.2+
- packages specified below

```
usage: compak <options>

options:
  --help,      -?           Show this help message
  --install,   -i <package> Install the specified package
  --remove,    -r <package> Uninstall the specified package
  --list,      -l           List installed packages
  --update,    -u <package> Update the specified package
  --update-all              Update all installed packages
  --clean                   Remove compak temporary files
  --package,   -p <folder>  Pack folder into compak-ready archive
  --view,      -v <package> View info about a package
    --raw                   View the raw package manifest
    --simple                View a simple overview of the package
```

To install compak, clone the source tree and run `make install`:
```
git clone https://github.com/gamerzero9512/compak.git
cd compak
make install
```

compak needs some packages to install, so run the command for your package manager:

### APT (Debian/Ubuntu)
```
sudo apt install zlib1g-dev liblzma-dev liblz4-dev libxml2-dev libssl-dev libbz2-dev libpsl-dev
```

### DNF (Fedora/RHEL/CentOS/Rocky/AlmaLinux)
```
sudo dnf install zlib-devel xz-devel lz4-devel libxml2-devel openssl-devel bzip2-devel libpsl-devel
```

### Pacman (Arch Linux)
```
sudo pacman -S zlib xz lz4 libxml2 openssl bzip2 libpsl
```

compak uses these libraries:
- [libarchive](https://github.com/libarchive/libarchive)
- [parson](https://github.com/kgabis/parson)
- [libcurl](https://github.com/curl/curl/)
