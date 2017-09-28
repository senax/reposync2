/*
vim: cindent
vim: background=dark
vim: tabstop=2 shiftwidth=2 expandtab
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <curl/curl.h>
#include <zlib.h>
#include <unistd.h>

extern char *keyfile, *certfile, *cafile;
extern bool noop;
extern bool verifyssl;
extern int last_n;
extern bool verbose;
extern bool purge;
extern bool getcomps;
extern bool getothermd;
extern bool updaterepodata;

extern char *group_file;

struct MemoryStruct {
  char *memory;
  size_t size;
};

struct rpm  {
  char *name, *arch, *version, *release, *checksum_type, *checksum, *location;
  size_t size;
  bool action;
};

/*
<data type="other">
  <checksum type="sha256">31781d234acf070623080749dfb55d250078768b87cd46fc6f9a538084709e1a</checksum>
  <open-checksum type="sha256">3fefb1b833212f82496be873f3d1d6a3d1ce5a8a369700525e0c4e2295813194</open-checksum>
  <location href="repodata/31781d234acf070623080749dfb55d250078768b87cd46fc6f9a538084709e1a-other.xml.gz"/>
  <timestamp>1503429569</timestamp>
  <size>2237354</size>
  <open-size>21584567</open-size>
</data>
*/
struct repofile  {
  char *name, *checksum_type, *checksum, *location;
  size_t size;
  long timestamp;
  bool action;
};

struct stats {
        int src_size;
        int dst_size;
        int downloaded;
        int download_skipped;
        int download_skipped_bytes;
        int deleted;
        int deleted_bytes;
        int to_download;
        int to_download_bytes;
        int to_delete;
        int to_delete_bytes;
        int down_bytes;
        struct timeval starttime, prevtime;
};

extern struct stats stats;

int get_http_to_file(FILE *fp,char *url, bool verbose);

int uncompress_file(char *filename, Byte **content);

void print_element_names(xmlNode * a_node);

xmlXPathObjectPtr getnodeset(xmlDocPtr doc, xmlChar *namespace, xmlChar *xpath);

int get_xml(char *url, char **xml);

int get_primary_xml(char *repo, char *xml, char **primary_xml);

void print_rpms(struct rpm *rpms, int size);

int ensure_dir(char *basedir, char *location);

int check_rpm_exists(char *path, size_t size, char *checksum_type, char *checksum);

int xferinfo(void *p, curl_off_t dltotal, curl_off_t dlnow, curl_off_t utotal, curl_off_t ulnow);

void usage(void);

void debug(int indent, char *message);

int get_options(int argc, char **argv, char **src_repo_ptr, char **dst_repo_ptr);

int rpm_compare(struct rpm *p1, struct rpm *p2);

void sort_rpms(struct rpm *rpms, int size);
void count_actions(struct rpm *rpms, int size, int *count, int *bytes);
void cleanup_source(struct rpm *rpms, int size,int last_n);
void simple_in_a_not_b(struct rpm *src_rpms,int src_size,struct rpm *dst_rpms,int dst_size);
int get_repofiles_from_repomd(char *xml, struct repofile **repofiles, int *repofiles_size);

int get_repomd_xml(char *repo, char **xml);
