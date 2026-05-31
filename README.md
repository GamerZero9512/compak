# compak
compak is a simple, minimal source-based package manager for GNU/Linux.

```
usage: compak <options>

options:
  --help,    -?           Show this help message
  --install, -i <package> Install the specified package
  --remove,  -r <package> Uninstall the specified package
  --list,    -l           List installed packages
  --package, -p <folder>  Pack folder into compak-ready archive
```

compak uses these libraries:
- [libarchive](https://github.com/libarchive/libarchive)
- [parson](https://github.com/kgabis/parson)