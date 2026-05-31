/*
    +--------+
    | compak |
    +--------+

    minimal source-based package manager

    TODO:
      - Add 'install.man' field:
        "install": {
          "man": {
            "1": ["docs/compak.1"],
            // All manpages are optional so you
            // don't have to spam "1" "2" "3" ...
          }
        }
*/

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

#define COMPAK_VERSION 1
/* bump this if compatibility
   with older versions breaks*/

static struct option long_options[] = {
  {"help",    no_argument,       0, '?'},
  {"install", required_argument, 0, 'i'},
  {"remove",  required_argument, 0, 'r'},
  {"list",    no_argument,       0, 'l'},
  {"package", required_argument, 0, 'p'},
  {0, 0, 0, 0}
};

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

    printf("\r\x1b[2K%s: extracting '%s'", compak_prefix, current);

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
  size_t n = json_array_get_count(arr);
  size_t i;

  for(i = 0; i < n; i++) {
    const char *file = json_array_get_string(arr, i);
    struct stat st;
    char src[PATH_MAX];
    char dst[PATH_MAX];

    snprintf(src, sizeof(src), "%s/%s", src_dir, file);
    snprintf(dst, sizeof(dst), "%s/%s", dst_dir, file);

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
  JSON_Object *obj, *install;
  JSON_Array *deps;
  size_t i, n;
  char template[] = "/tmp/compak-XXXXXX";
  char *dir = mkdtemp(template);
  char *pkg_name;
  DIR *pkgreg;
  char pkg_dir[PATH_MAX - 12]; /* to fit the /compak.json */
  char src[PATH_MAX];
  char dst[PATH_MAX];

  if(!dir) {
    fprintf(stderr, "%s: error creating temporary directory\n", compak_prefix);
    return;
  }
  if(!extract_archive(name, dir))
    printf("\r\x1b[2K%s: successfully extracted %d files\n", compak_prefix, extracted_files);
  else return;

  pid = fork();
  if(pid < 0) {
    fprintf(stderr, "%s: process creation failure\n", compak_prefix);
    return;
  }

  if(pid == 0) {
    char *argv[] = {"make", NULL};
    if(chdir(dir) != 0) {
      fprintf(stderr, "%s: error changing directory: %s\n", compak_prefix, strerror(errno));
      _exit(1);
    }
    execvp("make", argv);
    fprintf(stderr, "%s: error making\n", compak_prefix);
    _exit(1);
  }
  if(waitpid(pid, &status, 0) < 0) {
    fprintf(stderr, "%s: failure waiting for compilation\n", compak_prefix);
    return;
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

  /* copy stuff to folders 
       install.bin[] ------> /usr/local/bin
       install.lib[] ------> /usr/local/lib
       install.include[] --> /usr/local/include
  */

  root = json_parse_file("compak.json");
  obj = json_value_get_object(root);

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

  install = json_object_get_object(obj, "install");

  install_array(json_object_get_array(install, "bin"), dir, "/usr/local/bin");
  install_array(json_object_get_array(install, "lib"), dir, "/usr/local/lib");
  install_array(json_object_get_array(install, "include"), dir, "/usr/local/include");

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
  
  pkg_name = strdup(json_object_get_string(obj, "name"));

  if(!name) {
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
    if(!name) continue;
    if(snprintf(path, sizeof(path), "%s/%s", prefix, name) >= (int)sizeof(path)) {
      fprintf(stderr, "%s: path too long: '%s/%s'\n", compak_prefix, prefix, name);
      continue;
    }
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
  JSON_Object *obj, *install;

  snprintf(path, sizeof(path), "/var/lib/compak/%s/compak.json", name);
  root = json_parse_file(path);
  if(!root) {
    fprintf(stderr, "%s: failed to parse '%s'\n", compak_prefix, name);
    return;
  }
  obj = json_value_get_object(root);
  install = json_object_get_object(obj, "install");

  remove_artifacts(json_object_get_array(install, "bin"), "/usr/local/bin");
  remove_artifacts(json_object_get_array(install, "lib"), "/usr/local/lib");
  remove_artifacts(json_object_get_array(install, "include"), "/usr/local/include");

  snprintf(pkgdir, sizeof(pkgdir), "/var/lib/compak/%s", name);
  snprintf(jsonfile, sizeof(jsonfile), "%s/compak.json", pkgdir);
  
  unlink(jsonfile);
  if(rmdir(pkgdir) != 0)
    fprintf(stderr, "%s: failed to remove package directory: %s\n", compak_prefix, strerror(errno));
  else printf("%s: removed from package registry\n", compak_prefix);
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
    for(i = 0; i < regexes; i++)
      if(fnmatch(json_array_get_string(regex, i), arcpath, 0) == 0) matches = 1;
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
      printf("\r\x1b[2K%s: packaging file '%s'", compak_prefix, arcpath);
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
  printf("%s: validating JSON", compak_prefix);
  fflush(stdout);

  /* validate JSON */
  snprintf(json_path, sizeof(json_path), "%s/compak.json", folder);
  root = json_parse_file(json_path);
  if(!root) {
    fprintf(stderr, "\n%s: failed to parse 'compak.json'\n", compak_prefix);
    return;
  }
  obj = json_value_get_object(root);

  {
    const char *description;
    JSON_Array *deps;
    JSON_Object *install;
    JSON_Array *bin;
    JSON_Array *lib;
    JSON_Array *include;

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

    /* compak-min is numeric so we can't use ! */
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

    if(!valid) {
      json_value_free(root);
      fputc('\n', stdout);
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

  printf("\r\x1b[2K%s: successfully packaged %d files\n", compak_prefix, packed_files);
}

void compak_help(void) {
  printf(
    "compak: minimal source-based package manager\n"
    "\n"
    "usage: %s <options>\n"
    "\n"
    "options:\n"
    "  --help,    -?           Show this help message\n"
    "  --install, -i <package> Install the specified package\n"
    "  --remove,  -r <package> Uninstall the specified package\n"
    "  --list,    -l           List installed packages\n"
    "  --package, -p <folder>  Pack folder into compak-ready archive\n"
    , compak_prefix);
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

int main(int argc, char *argv[]) {
  int opt;

  compak_prefix = argv[0];

  if(argc == 1) {
    compak_help();
    return 0;
  }

  while((opt = getopt_long(argc, argv, "i:r:l?p:", long_options, NULL)) != -1) {
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
      default:
        return 1; 
    }
  }
  return 0;
}
