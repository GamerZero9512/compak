# compak
compak is a simple, minimal source-based package manager for GNU/Linux.

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
  --update,    -u <package> Update the specified package
  --update-all              Update all installed packages
  --package,   -p <folder>  Pack folder into compak-ready archive
```

To install compak, clone the source tree and run `make install`:
```
git clone https://github.com/gamerzero9512/compak.git
cd compak
make install
```

compak uses these libraries:
- [libarchive](https://github.com/libarchive/libarchive)
- [parson](https://github.com/kgabis/parson)
- [libcurl](https://github.com/curl/curl/)
