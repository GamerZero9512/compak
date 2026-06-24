/*
    +--------+
    | compak |
    +--------+

    minimal source-based package manager
*/

/* required for ftw for some reason */
#define _XOPEN_SOURCE 700

#include <stdio.h>
#include <unistd.h>
#include <getopt.h>
#include <errno.h>
#include <archive.h>
#include <archive_entry.h>
#include <parson.h>
#include <sys/stat.h>
#include <string.h>
#include <dirent.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <curl/curl.h>
#include <fnmatch.h>
#include <libgen.h>
#include <ftw.h>

#define COMPAK_VERSION 1
/* bump this if compatibility with
   older versions breaks completely */

char *compak_prefix = NULL;

int is_installed(const char *name) {
  char path[PATH_MAX];
  struct stat st;

  snprintf(path, sizeof(path), "/var/lib/compak/%s", name);

  return (stat(path, &st) == 0 && S_ISDIR(st.st_mode));
}

DIR *open_pkgreg(void) {
  struct stat st;
  DIR *dir;
  int stat_result = stat("/var/lib/compak", &st);
  if(!(stat_result == 0 && S_ISDIR(st.st_mode))) {
    if(stat_result == 0) {
      fprintf(stderr, "%s: /var/lib/compak is not a directory\n", compak_prefix);
      return NULL;
    }
    if(mkdir("/var/lib/compak", 0755) == -1 && errno != EEXIST) {
      fprintf(stderr, "%s: error creating /var/lib/compak\n", compak_prefix);
      return NULL;
    }
  }
  dir = opendir("/var/lib/compak");
  if(!dir) {
    fprintf(stderr, "%s: failed to open /var/lib/compak: %s\n", compak_prefix, strerror(errno));
  }
  return dir;
}

void compak_list_pkgs(void) {
  int pkg_count = 0;
  struct dirent *entry;
  DIR *dir = open_pkgreg();
  if(!dir) return;

  while((entry = readdir(dir)) != NULL) {
    struct stat pkg_st;
    char path[PATH_MAX];
    JSON_Value *root;
    JSON_Object *obj;
    const char *description;

    if(strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;
    snprintf(path, sizeof(path), "/var/lib/compak/%s", entry->d_name);
    if(stat(path, &pkg_st) == -1) continue;
    if(!S_ISDIR(pkg_st.st_mode)) continue;

    snprintf(path, sizeof(path), "/var/lib/compak/%s/compak.json", entry->d_name);

    root = json_parse_file(path);
    if(!root) {
      fprintf(stderr, "%s: failed to parse '%s'\n", compak_prefix, path);
      continue;
    }

    obj = json_value_get_object(root);
    description = json_object_get_string(obj, "description");

    if(description) printf("%s: %s\n", entry->d_name, description);
    else printf("%s: (no description)\n", entry->d_name);
    json_value_free(root);
    pkg_count++;
  }
  closedir(dir);
  if(pkg_count == 0)
    fprintf(stderr, "%s: no installed packages\n", compak_prefix);
}

int copy_data(struct archive *in, struct archive *out) {
  int r;
  const void *buf;
  size_t size;
  la_int64_t offset;

  while(1) {
    r = archive_read_data_block(in, &buf, &size, &offset);
    if(r == ARCHIVE_EOF) return ARCHIVE_OK;
    if(r != ARCHIVE_OK) return r;
    r = archive_write_data_block(out, buf, size, offset);
    if(r != ARCHIVE_OK) return r;
  }
}

int extracted_files = 0;

int extract_archive(const char *archive_path, const char *dest_dir) {
  struct archive *a = NULL;
  struct archive *disk = NULL;
  struct archive_entry *entry = NULL;

  int flags;
  int r;

  char *real;

  extracted_files = 0;

  real = realpath(archive_path, NULL);
  if(!real) {
    fprintf(stderr, "%s: error getting path of '%s': %s\n",
      compak_prefix, archive_path, strerror(errno));
    return 1;
  }

  flags = ARCHIVE_EXTRACT_TIME |
          ARCHIVE_EXTRACT_PERM |
          ARCHIVE_EXTRACT_SECURE_NODOTDOT |
          ARCHIVE_EXTRACT_SECURE_NOABSOLUTEPATHS;

  a = archive_read_new();
  if(!a) {
    fprintf(stderr, "%s: failed to allocate archive reader\n", compak_prefix);
    free(real);
    return 1;
  }

  archive_read_support_format_all(a);
  archive_read_support_filter_all(a);

  disk = archive_write_disk_new();
  if(!disk) {
    fprintf(stderr, "%s: failed to allocate disk writer\n", compak_prefix);
    archive_read_free(a);
    free(real);
    return 1;
  }

  archive_write_disk_set_options(disk, flags);
  archive_write_disk_set_standard_lookup(disk);

  if(chdir(dest_dir) != 0) {
    fprintf(stderr, "%s: failed to change directory to %s\n", compak_prefix, dest_dir);
    archive_write_free(disk);
    archive_read_free(a);
    free(real);
    return 1;
  }

  r = archive_read_open_filename(a, real, 10240);
  if(r != ARCHIVE_OK) {
    fprintf(stderr, "%s: failed to open archive '%s': %s\n",
      compak_prefix, archive_path, archive_error_string(a));
    archive_write_free(disk);
    archive_read_free(a);
    free(real);
    return 1;
  }

  while((r = archive_read_next_header(a, &entry)) == ARCHIVE_OK) {
    const char *current = archive_entry_pathname(entry);

    printf("%s: extracting '%s'\n", compak_prefix, current);

    r = archive_write_header(disk, entry);
    if(r != ARCHIVE_OK) {
      fprintf(stderr, "%s: failed to write header for '%s': %s\n",
        compak_prefix, current, archive_error_string(disk));
      archive_read_data_skip(a);
      continue;
    }

    r = copy_data(a, disk);
    if(r != ARCHIVE_OK) {
      fprintf(stderr, "%s: failed to copy data for '%s': %s\n",
        compak_prefix, current, archive_error_string(a));
      continue;
    }

    r = archive_write_finish_entry(disk);
    if(r != ARCHIVE_OK) {
      fprintf(stderr, "%s: failed to finish entry '%s': %s\n",
        compak_prefix, current, archive_error_string(disk));
      continue;
    }

    fflush(stdout);
    extracted_files++;
  }

  if (r != ARCHIVE_EOF)
    fprintf(stderr, "%s: archive read error: %s\n", compak_prefix, archive_error_string(a));

  archive_write_close(disk);
  archive_write_free(disk);
  archive_read_close(a);
  archive_read_free(a);

  free(real);

  return (r == ARCHIVE_EOF) ? 0 : 1;
}

#ifndef COPY_SIZE
  #define COPY_SIZE 4096
#endif

int copy_file(const char *src, const char *dst) {
  char buf[COPY_SIZE];
  size_t n;
  FILE *in, *out;
  struct stat st;

  in = fopen(src, "rb");
  if(!in) return 1;

  out = fopen(dst, "wb");
  if(!out) {
    fclose(in);
    return 2;
  }

  while((n = fread(buf, 1, sizeof(buf), in)) > 0)
    fwrite(buf, 1, n, out);

  fclose(in);
  fclose(out);

  if(stat(src, &st) == 0) chmod(dst, st.st_mode);
  return 0;
}

void install_array(JSON_Array *arr, const char *src_dir, const char *dst_dir) {
  size_t n;
  size_t i;

  if(!arr) return;
  n = json_array_get_count(arr);

  for(i = 0; i < n; i++) {
    const char *file = json_array_get_string(arr, i);
    char *filename;
    struct stat st;
    char src[PATH_MAX];
    char dst[PATH_MAX];

    filename = strdup(file);
    snprintf(src, sizeof(src), "%s/%s", src_dir, filename);
    snprintf(dst, sizeof(dst), "%s/%s", dst_dir, basename(filename));
    free(filename);

    if(stat(src, &st) != 0) {
      fprintf(stderr, "%s: missing artifact '%s'\n", compak_prefix, src);
      continue;
    }

    if(copy_file(src, dst))
      fprintf(stderr, "%s: failed to install '%s': %s\n", compak_prefix, file, strerror(errno));
    else
      printf("%s: installed '%s'\n", compak_prefix, file);
  }
}

int is_url(const char *s) {
  return (strncmp(s, "http://", 7) == 0 || strncmp(s, "https://", 8) == 0);
}

void compak_install(const char *name) {
  pid_t pid;
  int status;
  JSON_Value *root;
  JSON_Object *obj, *install, *man;
  JSON_Array *deps, *build;
  size_t i, n;
  char template[] = "/tmp/compak-XXXXXX";
  char *dir = mkdtemp(template);
  char *pkg_name;
  DIR *pkgreg;
  char pkg_dir[PATH_MAX - 12]; /* to fit the /compak.json */
  char src[PATH_MAX];
  char dst[PATH_MAX];
  char jsonpath[PATH_MAX];

  if(!dir) {
    fprintf(stderr, "%s: error creating temporary directory\n", compak_prefix);
    return;
  }
  if(!extract_archive(name, dir))
    printf("%s: successfully extracted %d files\n", compak_prefix, extracted_files);
  else return;

  snprintf(jsonpath, sizeof(jsonpath), "%s/compak.json", dir);
  root = json_parse_file(jsonpath);
  if(!root) {
    fprintf(stderr, "%s: failed to parse '%s'\n", compak_prefix, jsonpath);
    return;
  }
  obj = json_value_get_object(root);

  build = json_object_get_array(obj, "build");

  size_t count;
  size_t cmd_i;

  if(!build) {
    fprintf(stderr, "%s: missing field 'build'\n", compak_prefix);
    return;
  }

  count = json_array_get_count(build);
  for(cmd_i = 0; cmd_i < count; cmd_i++) {
    JSON_Array *json_argv = json_array_get_array(build, cmd_i);
    size_t argc, arg_i;
    char **argv;
    if(!json_argv) {
      fprintf(stderr, "%s: error getting build command\n", compak_prefix);
      return;
    }
    argc = json_array_get_count(json_argv);
    argv = malloc((argc + 1) * sizeof(*argv));
    if(!argv) {
      fprintf(stderr, "%s: error allocating memory: %s\n", compak_prefix, strerror(errno));
      return;
    }
    for(arg_i = 0; arg_i < argc; arg_i++) {
      argv[arg_i] = (char*)json_array_get_string(json_argv, arg_i);
      if(!argv[arg_i]) {
        fprintf(stderr, "%s: error getting build command %zu\n", compak_prefix, arg_i);
        free(argv);
        return;
      }
    }
    argv[argc] = NULL;

    pid = fork();
    if(pid < 0) {
      fprintf(stderr, "%s: process creation failure\n", compak_prefix);
      return;
    }

    if(pid == 0) {
      if(chdir(dir) != 0) {
        fprintf(stderr, "%s: error changing directory: %s\n", compak_prefix, strerror(errno));
        _exit(1);
      }
      execvp(argv[0], argv);
      fprintf(stderr, "%s: error compiling package\n", compak_prefix);
      _exit(1);
    }
    free(argv);

    if(waitpid(pid, &status, 0) < 0) {
      fprintf(stderr, "%s: failure waiting for compilation\n", compak_prefix);
      return;
    }
  }

  if(mkdir("/usr/local/bin", 0755) == -1 && errno != EEXIST) {
    fprintf(stderr, "%s: error creating '/usr/local/bin': %s\n", compak_prefix, strerror(errno));
    return;
  }
  if(mkdir("/usr/local/lib", 0755) == -1 && errno != EEXIST) {
    fprintf(stderr, "%s: error creating '/usr/local/lib': %s\n", compak_prefix, strerror(errno));
    return;
  }
  if(mkdir("/usr/local/include", 0755) == -1 && errno != EEXIST) {
    fprintf(stderr, "%s: error creating '/usr/local/include': %s\n", compak_prefix, strerror(errno));
    return;
  }
  if(mkdir("/usr/local/share/man", 0755) == -1 && errno != EEXIST) {
    fprintf(stderr, "%s: error creating '/usr/local/share/man': %s\n", compak_prefix, strerror(errno));
    return;
  }
  if(mkdir("/usr/local/share/man/man1", 0755) == -1 && errno != EEXIST) {
    fprintf(stderr, "%s: error creating '/usr/local/share/man1': %s\n", compak_prefix, strerror(errno));
    return;
  }
  if(mkdir("/usr/local/share/man/man2", 0755) == -1 && errno != EEXIST) {
    fprintf(stderr, "%s: error creating '/usr/local/share/man2': %s\n", compak_prefix, strerror(errno));
    return;
  }
  if(mkdir("/usr/local/share/man/man3", 0755) == -1 && errno != EEXIST) {
    fprintf(stderr, "%s: error creating '/usr/local/share/man3': %s\n", compak_prefix, strerror(errno));
    return;
  }
  if(mkdir("/usr/local/share/man/man4", 0755) == -1 && errno != EEXIST) {
    fprintf(stderr, "%s: error creating '/usr/local/share/man4': %s\n", compak_prefix, strerror(errno));
    return;
  }
  if(mkdir("/usr/local/share/man/man5", 0755) == -1 && errno != EEXIST) {
    fprintf(stderr, "%s: error creating '/usr/local/share/man5': %s\n", compak_prefix, strerror(errno));
    return;
  }
  if(mkdir("/usr/local/share/man/man6", 0755) == -1 && errno != EEXIST) {
    fprintf(stderr, "%s: error creating '/usr/local/share/man6': %s\n", compak_prefix, strerror(errno));
    return;
  }
  if(mkdir("/usr/local/share/man/man7", 0755) == -1 && errno != EEXIST) {
    fprintf(stderr, "%s: error creating '/usr/local/share/man7': %s\n", compak_prefix, strerror(errno));
    return;
  }
  if(mkdir("/usr/local/share/man/man8", 0755) == -1 && errno != EEXIST) {
    fprintf(stderr, "%s: error creating '/usr/local/share/man8': %s\n", compak_prefix, strerror(errno));
    return;
  }
  if(mkdir("/usr/local/share/man/man9", 0755) == -1 && errno != EEXIST) {
    fprintf(stderr, "%s: error creating '/usr/local/share/man9': %s\n", compak_prefix, strerror(errno));
    return;
  }

  /* copy stuff to folders
       install.bin[] ------> /usr/local/bin
       install.lib[] ------> /usr/local/lib
       install.include[] --> /usr/local/include
       install.man.*[] ----> /usr/local/share/man
  */

  /* moved upwards:

  snprintf(jsonpath, sizeof(jsonpath), "%s/compak.json", dir);
  root = json_parse_file(jsonpath);
  if(!root) {
    fprintf(stderr, "%s: failed to parse '%s'\n", compak_prefix, jsonpath);
    return;
  }
  obj = json_value_get_object(root);

  */

  /* verify we're on the right version of compak */
  if(json_object_has_value(obj, "compak-min"))
    if(json_object_get_number(obj, "compak-min") > COMPAK_VERSION) {
      fprintf(stderr, "%s: incompatible compak version\n", compak_prefix);
      json_value_free(root);
      return;
    }
  if(json_object_has_value(obj, "compak-max"))
    if(json_object_get_number(obj, "compak-max") < COMPAK_VERSION) {
      fprintf(stderr, "%s: incompatible compak version\n", compak_prefix);
      json_value_free(root);
      return;
    }

  deps = json_object_get_array(obj, "deps");
  n = json_array_get_count(deps);
  for(i = 0; i < n; i++) {
    const char *dep = json_array_get_string(deps, i);
    if(!dep) continue;
    if (!is_installed(dep)) {
      fprintf(stderr, "%s: missing dependency '%s' for package '%s'\n", compak_prefix, dep, name);
      json_value_free(root);
      return;
    }
  }

  install = json_object_get_object(obj, "install");
  man = json_object_get_object(install, "man");

  install_array(json_object_get_array(install, "bin"), dir, "/usr/local/bin");
  install_array(json_object_get_array(install, "lib"), dir, "/usr/local/lib");
  install_array(json_object_get_array(install, "include"), dir, "/usr/local/include");
  /* install manfiles */
  install_array(json_object_get_array(man, "1"), dir, "/usr/local/share/man/man1");
  install_array(json_object_get_array(man, "2"), dir, "/usr/local/share/man/man2");
  install_array(json_object_get_array(man, "3"), dir, "/usr/local/share/man/man3");
  install_array(json_object_get_array(man, "4"), dir, "/usr/local/share/man/man4");
  install_array(json_object_get_array(man, "5"), dir, "/usr/local/share/man/man5");
  install_array(json_object_get_array(man, "6"), dir, "/usr/local/share/man/man6");
  install_array(json_object_get_array(man, "7"), dir, "/usr/local/share/man/man7");
  install_array(json_object_get_array(man, "8"), dir, "/usr/local/share/man/man8");
  install_array(json_object_get_array(man, "9"), dir, "/usr/local/share/man/man9");

  pkg_name = strdup(json_object_get_string(obj, "name"));

  if(!pkg_name) {
    fprintf(stderr, "%s: missing package name\n", compak_prefix);
    json_value_free(root);
    return;
  }

  json_value_free(root);

  /* register in /var/lib/compak */

  /* hack to create /var/lib/compak */
  pkgreg = open_pkgreg();
  if(!pkgreg) return;
  closedir(pkgreg);

  snprintf(pkg_dir, sizeof(pkg_dir), "/var/lib/compak/%s", pkg_name);
  free(pkg_name);
  if(mkdir(pkg_dir, 0755) == -1 && errno != EEXIST) {
    fprintf(stderr, "%s: failed to create '%s': %s\n", compak_prefix, pkg_dir, strerror(errno));
    return;
  }

  snprintf(src, sizeof(src), "%s/compak.json", dir);
  snprintf(dst, sizeof(dst), "%s/compak.json", pkg_dir);
  if(copy_file(src, dst) != 0)
    fprintf(stderr, "%s: failed to save '%s' to package database\n", compak_prefix, name);
}

void remove_artifacts(JSON_Array *arr, const char *prefix) {
  size_t i;
  if(!arr) return;
  for(i = 0; i < json_array_get_count(arr); i++) {
    const char *name = json_array_get_string(arr, i);
    char path[PATH_MAX];
    char *filename, *base;
    if(!name) continue;
    filename = strdup(name);
    base = basename(filename);
    if(snprintf(path, sizeof(path), "%s/%s", prefix, base) >= (int)sizeof(path)) {
      fprintf(stderr, "%s: path too long: '%s/%s'\n", compak_prefix, prefix, base);
      free(filename);
      continue;
    }
    free(filename);
    if(unlink(path) == 0) printf("%s: removed '%s'\n", compak_prefix, path);
    else if(errno == ENOENT) fprintf(stderr, "%s: missing artifact: '%s'\n", compak_prefix, path);
    else fprintf(stderr, "%s: failed to remove '%s': %s\n", compak_prefix, path, strerror(errno));
  }
}

void compak_remove(const char *name) {
  char path[PATH_MAX];
  char pkgdir[PATH_MAX - 12]; /* to fit the /compak.json */
  char jsonfile[PATH_MAX];

  JSON_Value *root;
  JSON_Object *obj, *install, *man;

  snprintf(path, sizeof(path), "/var/lib/compak/%s/compak.json", name);
  root = json_parse_file(path);
  if(!root) {
    fprintf(stderr, "%s: failed to parse manifest of '%s'\n", compak_prefix, name);
    return;
  }
  obj = json_value_get_object(root);
  install = json_object_get_object(obj, "install");

  remove_artifacts(json_object_get_array(install, "bin"), "/usr/local/bin");
  remove_artifacts(json_object_get_array(install, "lib"), "/usr/local/lib");
  remove_artifacts(json_object_get_array(install, "include"), "/usr/local/include");

  man = json_object_get_object(install, "man");
  remove_artifacts(json_object_get_array(man, "1"), "/usr/local/share/man/man1");
  remove_artifacts(json_object_get_array(man, "2"), "/usr/local/share/man/man2");
  remove_artifacts(json_object_get_array(man, "3"), "/usr/local/share/man/man3");
  remove_artifacts(json_object_get_array(man, "4"), "/usr/local/share/man/man4");
  remove_artifacts(json_object_get_array(man, "5"), "/usr/local/share/man/man5");
  remove_artifacts(json_object_get_array(man, "6"), "/usr/local/share/man/man6");
  remove_artifacts(json_object_get_array(man, "7"), "/usr/local/share/man/man7");
  remove_artifacts(json_object_get_array(man, "8"), "/usr/local/share/man/man8");
  remove_artifacts(json_object_get_array(man, "9"), "/usr/local/share/man/man9");

  snprintf(pkgdir, sizeof(pkgdir), "/var/lib/compak/%s", name);
  snprintf(jsonfile, sizeof(jsonfile), "%s/compak.json", pkgdir);

  unlink(jsonfile);
  if(rmdir(pkgdir) != 0)
    fprintf(stderr, "%s: failed to remove package directory: %s\n", compak_prefix, strerror(errno));
  else printf("%s: removed from package registry\n", compak_prefix);

  json_value_free(root);
}

const char *strip_prefix(const char *path, const char *folder) {
  size_t len = strlen(folder);
  if(strncmp(path, folder, len) == 0)
    if(path[len] == '/')
      return path + len + 1;
  return NULL;
}

int copy_file_to_archive(const char *path, struct archive *a) {
  FILE *fp;
  char buf[COPY_SIZE];
  size_t n;

  fp = fopen(path, "rb");
  if(!fp) {
    fprintf(stderr, "%s: failed to open '%s': %s\n", compak_prefix, path, strerror(errno));
    return 1;
  }

  while((n = fread(buf, 1, sizeof(buf), fp)) > 0) {
    la_ssize_t written = archive_write_data(a, buf, n);
    if(written < 0) {
      fprintf(stderr, "%s: archive write error: %s\n", compak_prefix, archive_error_string(a));
      fclose(fp);
      return 1;
    }
  }
  if(ferror(fp)) {
    fprintf(stderr, "%s: read error on '%s'\n", compak_prefix, path);
    fclose(fp);
    return 1;
  }
  fclose(fp);
  return 0;
}

int packed_files = 0;

int pack_dir(struct archive *a, const char *base, const char *rel, const char *exclude,
             JSON_Array *regex) {
  DIR *dir;
  struct dirent *e;
  struct stat st;
  char path[PATH_MAX];
  char arcpath[PATH_MAX];
  size_t i, regexes;
  int matches;

  snprintf(path, sizeof(path), "%s/%s", base, rel);
  dir = opendir(path);
  if(!dir) {
    fprintf(stderr, "%s: cannot open directory '%s'\n", compak_prefix, base);
    return 1;
  }
  while((e = readdir(dir))) {
    if(!strcmp(e->d_name, ".") || !strcmp(e->d_name, ".."))
      continue;
    if(rel && rel[0])
      snprintf(path, sizeof(path), "%s/%s/%s", base, rel, e->d_name);
    else
      snprintf(path, sizeof(path), "%s/%s", base, e->d_name);
    if(stat(path, &st) == -1)
      continue;
    if(rel && rel[0])
      snprintf(arcpath, sizeof(arcpath), "%s/%s", rel, e->d_name);
    else
      snprintf(arcpath, sizeof(arcpath), "%s", e->d_name);
    /* validate excludes */
    regexes = json_array_get_count(regex);
    matches = 0;
    for(i = 0; i < regexes; i++) {
      const char *string = json_array_get_string(regex, i);
      if(!string) {
        fprintf(stderr, "%s: error getting exclude pattern %zu", compak_prefix, i);
        continue;
      }
      if(fnmatch(string, arcpath, 0) == 0) matches = 1;
    }
    if(matches) continue;
    if(S_ISDIR(st.st_mode)) {
      struct archive_entry *dirent = archive_entry_new();
      archive_entry_set_pathname(dirent, arcpath);
      archive_entry_set_filetype(dirent, AE_IFDIR);
      archive_entry_set_perm(dirent, st.st_mode);
      archive_write_header(a, dirent);
      archive_entry_free(dirent);
      pack_dir(a, base, arcpath, NULL, regex);
    } else if(S_ISREG(st.st_mode)) {
      if(exclude) if(!strcmp(arcpath, exclude)) continue;
      struct archive_entry *entry = archive_entry_new();
      printf("%s: packaging file '%s'\n", compak_prefix, arcpath);
      fflush(stdout);
      packed_files++;
      archive_entry_set_pathname(entry, arcpath);
      archive_entry_set_size(entry, st.st_size);
      archive_entry_set_filetype(entry, AE_IFREG);
      archive_entry_set_perm(entry, st.st_mode);
      if(archive_write_header(a, entry) != ARCHIVE_OK) {
        fprintf(stderr, "%s: %s\n", compak_prefix, archive_error_string(a));
        archive_entry_free(entry);
        continue;
      }
      copy_file_to_archive(path, a);
      archive_entry_free(entry);
    }
  }
  closedir(dir);
  return 0;
}

void compak_package(const char *folder) {
  JSON_Value *root;
  JSON_Object *obj;
  JSON_Array *exclude;
  char output[PATH_MAX];
  char json_path[PATH_MAX];
  struct archive *a;
  DIR *dir;
  const char *name;

  packed_files = 0;
  printf("%s: validating JSON\n", compak_prefix);
  fflush(stdout);

  /* validate JSON */
  snprintf(json_path, sizeof(json_path), "%s/compak.json", folder);
  root = json_parse_file(json_path);
  if(!root) {
    fprintf(stderr, "%s: failed to parse 'compak.json'\n", compak_prefix);
    return;
  }
  obj = json_value_get_object(root);

  {
    const char  *description;
    JSON_Array  *deps;
    JSON_Object *install;
    JSON_Array  *bin;
    JSON_Array  *lib;
    JSON_Array  *include;
    JSON_Object *man;
    JSON_Value  *source;

    int valid = 1;

    name = json_object_get_string(obj, "name");
    if(!name) {
      fprintf(stderr, "%s: missing field 'name'\n", compak_prefix);
      valid = 0;
    }

    description = json_object_get_string(obj, "description");
    if(!description) {
      fprintf(stderr, "%s: missing field 'description'\n", compak_prefix);
      valid = 0;
    }

    /* compak-min is numeric so we can't use unary ! */
    if(!json_object_has_value(obj, "compak-min")) {
      fprintf(stderr, "%s: missing or invalid field 'compak-min'\n", compak_prefix);
      valid = 0;
    }

    deps = json_object_get_array(obj, "deps");
    if(!deps) {
      fprintf(stderr, "%s: missing field 'deps'\n", compak_prefix);
      valid = 0;
    }

    install = json_object_get_object(obj, "install");
    if(!install) {
      fprintf(stderr, "%s: missing field 'install'\n", compak_prefix);
      valid = 0;
    }

    bin = json_object_get_array(install, "bin");
    if(!bin) {
      fprintf(stderr, "%s: missing field 'install.bin'\n", compak_prefix);
      valid = 0;
    }

    lib = json_object_get_array(install, "lib");
    if(!lib) {
      fprintf(stderr, "%s: missing field 'install.lib'\n", compak_prefix);
      valid = 0;
    }

    include = json_object_get_array(install, "include");
    if(!include) {
      fprintf(stderr, "%s: missing field 'install.include'\n", compak_prefix);
      valid = 0;
    }

    exclude = json_object_get_array(obj, "exclude");
    if(!exclude) {
      fprintf(stderr, "%s: missing field 'exclude'\n", compak_prefix);
      valid = 0;
    }

    man = json_object_get_object(install, "man");
    if(!man) {
      fprintf(stderr, "%s: missing field 'install.man'\n", compak_prefix);
      valid = 0;
    }

    source = json_object_get_value(obj, "source");
    if(!source) {
      fprintf(stderr, "%s: missing field 'source'\n", compak_prefix);
      valid = 0;
    }

    if(!valid) {
      json_value_free(root);
      return;
    }
  }

  snprintf(output, sizeof(output), "%s.tar.xz", name);
  a = archive_write_new();
  archive_write_add_filter_xz(a);
  archive_write_set_format_pax_restricted(a);
  if(archive_write_open_filename(a, output) != ARCHIVE_OK) {
    fprintf(stderr, "%s: cannot open output archive\n", compak_prefix);
    json_value_free(root);
    return;
  }
  dir = opendir(folder);
  if(!dir) {
    fprintf(stderr, "%s: cannot open folder '%s'\n", compak_prefix, folder);
    json_value_free(root);
    return;
  }

  pack_dir(a, folder, "", output, exclude);

  archive_write_close(a);
  archive_write_free(a);
  json_value_free(root);

  printf("%s: successfully packaged %d files\n", compak_prefix, packed_files);
  closedir(dir);
}

size_t write_cb(void *ptr, size_t size, size_t nmemb, void *stream) {
  return fwrite(ptr, size, nmemb, (FILE*)stream);
}

int download_to_file(const char *url, const char *out_path) {
  CURL *curl = curl_easy_init();
  if(!curl) return 1;
  FILE *fp = fopen(out_path, "wb");
  if(!fp) return 1;
  curl_easy_setopt(curl, CURLOPT_URL, url);
  curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, fp);
  CURLcode res = curl_easy_perform(curl);
  fclose(fp);
  curl_easy_cleanup(curl);
  return (res == CURLE_OK) ? 0 : 1;
}

void compak_install_url(const char *url) {
  char tmpdir[] = "/tmp/compak-XXXXXX";
  char filepath[PATH_MAX];
  if(!mkdtemp(tmpdir)) {
    fprintf(stderr, "%s: error creating temporary directory\n", compak_prefix);
    return;
  }
  snprintf(filepath, sizeof(filepath), "%s/pkg.tar.xz", tmpdir);
  fprintf(stderr, "%s: downloading '%s'\n", compak_prefix, url);
  if(download_to_file(url, filepath)) {
    fprintf(stderr, "%s: download failed\n", compak_prefix);
    return;
  }
  compak_install(filepath);
  return;
}

void compak_update(const char *name) {
  char path[PATH_MAX];
  JSON_Value *root;
  JSON_Object *obj;
  JSON_Value *src;
  const char *source;

  snprintf(path, sizeof(path), "/var/lib/compak/%s/compak.json", name);

  root = json_parse_file(path);
  if(!root) {
    fprintf(stderr, "%s: failed to parse 'compak.json'\n", compak_prefix);
    return;
  }
  obj = json_value_get_object(root);
  src = json_object_get_value(obj, "source");
  if(!src) {
    fprintf(stderr, "%s: missing source field\n", compak_prefix);
    json_value_free(root);
    return;
  }
  if(json_value_get_type(src) == JSONNull) {
    fprintf(stderr, "%s: package is local only\n", compak_prefix);
    json_value_free(root);
    return;
  }
  source = json_value_get_string(src);
  if(!source) {
    fprintf(stderr, "%s: error finding package source\n", compak_prefix);
    json_value_free(root);
    return;
  }
  compak_remove(name);
  compak_install_url(source);
  json_value_free(root);
}

void compak_update_all(void) {
  struct dirent *entry;
  DIR *dir = open_pkgreg();
  if(!dir) return;

  while((entry = readdir(dir)) != NULL) {
    struct stat pkg_st;
    char path[PATH_MAX];

    if(strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;
    snprintf(path, sizeof(path), "/var/lib/compak/%s", entry->d_name);
    if(stat(path, &pkg_st) == -1) continue;
    if(!S_ISDIR(pkg_st.st_mode)) continue;

    printf("%s: updating '%s'\n", compak_prefix, entry->d_name);
    compak_update(entry->d_name);
  }
  closedir(dir);
}

int rm(const char *f, const struct stat *s, int t, struct FTW *ftw) {
  (void)s;
  (void)t;
  (void)ftw;
  printf("%s: removing '%s'\n", compak_prefix, f);
  return remove(f);
}

void compak_clean(void) {
  DIR *dir;
  struct dirent *entry;

  dir = opendir("/tmp");
  if(!dir) {
    fprintf(stderr, "%s: error opening '/tmp'\n", compak_prefix);
    return;
  }

  while((entry = readdir(dir)) != NULL) {
    struct stat st;
    char path[PATH_MAX];

    if(strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
      continue;

    snprintf(path, sizeof(path), "/tmp/%s", entry->d_name);

    if(stat(path, &st) == -1) {
      fprintf(stderr, "%s: error statting '%s'\n", compak_prefix, path);
      continue;
    }

    if(!S_ISDIR(st.st_mode)) continue;
    if(fnmatch("compak-*", entry->d_name, 0) != 0) continue;

    nftw(path, rm, 64, FTW_DEPTH | FTW_PHYS);
  }

  closedir(dir);
}

enum {
  OPT_UPDATE_ALL,
  OPT_CLEAN,
  OPT_VIEW_RAW,
  OPT_VIEW_SIMPLE
};

int view_mode = OPT_VIEW_SIMPLE;

void compak_view(const char *pkg_name) {
  char buf[PATH_MAX];
  JSON_Value *root;
  JSON_Object *obj;
  const char *description;
  const char *name;
  size_t i;
  JSON_Object *install;
  JSON_Array *bin;
  JSON_Array *lib;
  JSON_Array *include;
  JSON_Object *man;
  JSON_Array *man_c;
  JSON_Value *source;

  snprintf(buf, sizeof(buf), "/var/lib/compak/%s/compak.json", pkg_name);

  if(view_mode == OPT_VIEW_RAW) {
    int ch;
    FILE *stream = fopen(buf, "r");
    if(!stream) {
      fprintf(stderr, "%s: error opening '%s': %s\n", compak_prefix, buf, strerror(errno));
      return;
    }
    while((ch = fgetc(stream)) != EOF) fputc(ch, stdout);
    fclose(stream);
    return;
  }

  root = json_parse_file(buf);
  if(!root) {
    fprintf(stderr, "%s: failed to parse '%s'\n", compak_prefix, buf);
    return;
  }
  obj = json_value_get_object(root);
  name = json_object_get_string(obj, "name");
  if(!name) {
    fprintf(stderr, "%s: missing name field\n", compak_prefix);
    json_value_free(root);
    return;
  }
  description = json_object_get_string(obj, "description");
  if(!description) description = "(no description)";

  source = json_object_get_value(obj, "source");
  if(!source) {
    fprintf(stderr, "%s: missing 'source' field\n", compak_prefix);
    return;
  }
  printf(
    "%s:\n"
    "  %s\n"
    "  from: %s\n"
    "\n"
    "files:\n"
    "  bin:\n"
  , name, description, json_value_get_type(source) == JSONNull ? "local file" : json_value_get_string(source));
  install = json_object_get_object(obj, "install");
  if(!install) {
    fprintf(stderr, "%s: missing install field\n", compak_prefix);
    json_value_free(root);
    return;
  }

  bin = json_object_get_array(install, "bin");
  if(!bin) {
    fprintf(stderr, "%s: missing 'bin' field\n", compak_prefix);
    json_value_free(root);
    return;
  }
  for(i = 0; i < json_array_get_count(bin); i++) {
    const char *str = json_array_get_string(bin, i);
    printf("    %s -> %s%s\n", str ? str : "error getting item", str ? "/usr/local/bin/" : "", str ? basename((char*)str) : "error getting item");
  }
  fputc('\n', stdout);

  printf("  lib:\n");
  lib = json_object_get_array(install, "lib");
  if(!lib) {
    fprintf(stderr, "%s: missing 'lib' field\n", compak_prefix);
    json_value_free(root);
    return;
  }
  for(i = 0; i < json_array_get_count(lib); i++) {
    const char *str = json_array_get_string(lib, i);
    printf("    %s -> %s%s\n", str ? str : "error getting item", str ? "/usr/local/lib/" : "", str ? basename((char*)str) : "error getting item");
  }
  fputc('\n', stdout);

  printf("  include:\n");
  include = json_object_get_array(install, "include");
  if(!include) {
    fprintf(stderr, "%s: missing 'include' field\n", compak_prefix);
    json_value_free(root);
    return;
  }
  for(i = 0; i < json_array_get_count(include); i++) {
    const char *str = json_array_get_string(include, i);
    printf("    %s -> %s%s\n", str ? str : "error getting item", str ? "/usr/local/include/" : "", str ? basename((char*)str) : "error getting item");
  }
  fputc('\n', stdout);

  printf("  manpages:\n");
  man = json_object_get_object(install, "man");
  if(!man) {
    fprintf(stderr, "%s: missing 'man' field\n", compak_prefix);
    json_value_free(root);
    return;
  }

  man_c = json_object_get_array(man, "1");
  if(man_c) {
    printf("    1:\n");
    for(i = 0; i < json_array_get_count(man_c); i++) {
      const char *str = json_array_get_string(man_c, i);
      printf("      %s -> %s%s\n", str ? str : "error getting item", str ? "/usr/local/share/man/man1/" : "", str ? basename((char*)str) : "error getting item");
    }
    fputc('\n', stdout);
  }

  man_c = json_object_get_array(man, "2");
  if(man_c) {
    printf("    2:\n");
    for(i = 0; i < json_array_get_count(man_c); i++) {
      const char *str = json_array_get_string(man_c, i);
      printf("      %s -> %s%s\n", str ? str : "error getting item", str ? "/usr/local/share/man/man2/" : "", str ? basename((char*)str) : "error getting item");
    }
    fputc('\n', stdout);
  }

  man_c = json_object_get_array(man, "3");
  if(man_c) {
    printf("    3:\n");
    for(i = 0; i < json_array_get_count(man_c); i++) {
      const char *str = json_array_get_string(man_c, i);
      printf("      %s -> %s%s\n", str ? str : "error getting item", str ? "/usr/local/share/man/man3/" : "", str ? basename((char*)str) : "error getting item");
    }
    fputc('\n', stdout);
  }

  man_c = json_object_get_array(man, "4");
  if(man_c) {
    printf("    4:\n");
    for(i = 0; i < json_array_get_count(man_c); i++) {
      const char *str = json_array_get_string(man_c, i);
      printf("      %s -> %s%s\n", str ? str : "error getting item", str ? "/usr/local/share/man/man4/" : "", str ? basename((char*)str) : "error getting item");
    }
    fputc('\n', stdout);
  }

  man_c = json_object_get_array(man, "5");
  if(man_c) {
    printf("    5:\n");
    for(i = 0; i < json_array_get_count(man_c); i++) {
      const char *str = json_array_get_string(man_c, i);
      printf("      %s -> %s%s\n", str ? str : "error getting item", str ? "/usr/local/share/man/man5/" : "", str ? basename((char*)str) : "error getting item");
    }
    fputc('\n', stdout);
  }

  man_c = json_object_get_array(man, "6");
  if(man_c) {
    printf("    6:\n");
    for(i = 0; i < json_array_get_count(man_c); i++) {
      const char *str = json_array_get_string(man_c, i);
      printf("      %s -> %s%s\n", str ? str : "error getting item", str ? "/usr/local/share/man/man6/" : "", str ? basename((char*)str) : "error getting item");
    }
    fputc('\n', stdout);
  }

  man_c = json_object_get_array(man, "7");
  if(man_c) {
    printf("    7:\n");
    for(i = 0; i < json_array_get_count(man_c); i++) {
      const char *str = json_array_get_string(man_c, i);
      printf("      %s -> %s%s\n", str ? str : "error getting item", str ? "/usr/local/share/man/man7/" : "", str ? basename((char*)str) : "error getting item");
    }
    fputc('\n', stdout);
  }

  man_c = json_object_get_array(man, "8");
  if(man_c) {
    printf("    8:\n");
    for(i = 0; i < json_array_get_count(man_c); i++) {
      const char *str = json_array_get_string(man_c, i);
      printf("      %s -> %s%s\n", str ? str : "error getting item", str ? "/usr/local/share/man/man8/" : "", str ? basename((char*)str) : "error getting item");
    }
    fputc('\n', stdout);
  }

  man_c = json_object_get_array(man, "9");
  if(man_c) {
    printf("    9:\n");
    for(i = 0; i < json_array_get_count(man_c); i++) {
      const char *str = json_array_get_string(man_c, i);
      printf("      %s -> %s%s\n", str ? str : "error getting item", str ? "/usr/local/share/man/man9/" : "", str ? basename((char*)str) : "error getting item");
    }
  }

  json_value_free(root);
}

void compak_help(void) {
  printf(
    "compak: minimal source-based package manager\n"
    "\n"
    "usage: %s <options>\n"
    "\n"
    "options:\n"
    "  --help,      -?           Show this help message\n"
    "  --install,   -i <package> Install the specified package\n"
    "  --remove,    -r <package> Uninstall the specified package\n"
    "  --list,      -l           List installed packages\n"
    "  --update,    -u <package> Update the specified package\n"
    "  --update-all              Update all installed packages\n"
    "  --clean                   Remove compak temporary files\n"
    "  --package,   -p <folder>  Pack folder into compak-ready archive\n"
    "  --view,      -v <package> View info about a package\n"
    "    --raw                   View the raw package manifest\n"
    "    --simple                View a simple overview of the package\n"
    , compak_prefix);
}

/* ENUM HAS BEEN MOVED TO :965 */

struct option long_options[] = {
  {"help",       no_argument,       0, '?'            },
  {"install",    required_argument, 0, 'i'            },
  {"remove",     required_argument, 0, 'r'            },
  {"list",       no_argument,       0, 'l'            },
  {"update",     required_argument, 0, 'u'            },
  {"package",    required_argument, 0, 'p'            },
  {"clean",      no_argument,       0, OPT_CLEAN      },
  {"update-all", no_argument,       0, OPT_UPDATE_ALL },
  {"view",       required_argument, 0, 'v'            },
  {"raw",        no_argument,       0, OPT_VIEW_RAW   },
  {"simple",     no_argument,       0, OPT_VIEW_SIMPLE},
  {0, 0, 0, 0}
};

int main(int argc, char *argv[]) {
  int opt;

  compak_prefix = argv[0];

  if(argc == 1) {
    compak_help();
    return 0;
  }

  while((opt = getopt_long(argc, argv, "?i:r:lu:p:v:", long_options, NULL)) != -1) {
    switch(opt) {
      case 'i':
        if(geteuid() != 0) {
          fprintf(stderr, "%s: install must run as root\n", compak_prefix);
          return 1;
        }
        if(is_url(optarg)) compak_install_url(optarg);
        else compak_install(optarg);
        break;
      case 'r':
        if(geteuid() != 0) {
          fprintf(stderr, "%s: remove must run as root\n", compak_prefix);
          return 1;
        }
        compak_remove(optarg);
        break;
      case 'l':
        compak_list_pkgs();
        break;
      case '?':
        compak_help();
        break;
      case 'p':
        compak_package(optarg);
        break;
      case 'u':
        if(geteuid() != 0) {
          fprintf(stderr, "%s: update must run as root\n", compak_prefix);
          return 1;
        }
        compak_update(optarg);
        break;
      case OPT_UPDATE_ALL:
        if(geteuid() != 0) {
          fprintf(stderr, "%s: update must run as root\n", compak_prefix);
          return 1;
        }
        compak_update_all();
        break;
      case OPT_CLEAN:
        if(geteuid() != 0) {
          fprintf(stderr, "%s: clean must run as root\n", compak_prefix);
          return 1;
        }
        compak_clean();
        break;
      case 'v':
        compak_view(optarg);
        break;
      case OPT_VIEW_SIMPLE:
        view_mode = OPT_VIEW_SIMPLE;
        break;
      case OPT_VIEW_RAW:
        view_mode = OPT_VIEW_RAW;
        break;
      default:
        return 1;
    }
  }
  return 0;
}
