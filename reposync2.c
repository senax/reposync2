/*
vv:vim: foldmethod=indent 
vim: cindent
vim: background=dark
vim: tabstop=8 shiftwidth=8 expandtab

get_libxml: get_libxml.c
gcc -g -std=c99 -Wall -o get_libxml -I/usr/include/libxml2 -lxml2 -lz -lm -ldl -lcurl get_libxml.c
valgrind ./get_libxml --leak-check=full
*/
#define _GNU_SOURCE // for strverscmp

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <curl/curl.h>
#include <libxml/parser.h>
#include <libxml/xpath.h>
#include <libxml/tree.h>
#include <libxml/xpathInternals.h>
#include <zlib.h>
#include <unistd.h>
#include <sys/time.h>

#include "misc.h"

char *keyfile, *certfile, *cafile;
bool verbose = false;
bool noop = false;
bool verifyssl = true;
bool getcomps = false;
bool getothermd = false;
bool purge = false;
bool updaterepodata = false;
int last_n=0;
struct stats stats;

char *group_file;

#define ELEMSIZE 100

struct rpm_parserstate {
        int return_val;
        int index;
        struct rpm *rpms;
        bool package;
        bool name;
        bool arch;
        bool version;
        bool checksum;
        bool size;
        bool location;
        char element[ELEMSIZE];
};

struct repomd_parserstate {
        int return_val;
        int index;
        struct repofile *repofiles;
        bool data;
        bool checksum;
        bool location;
        bool timestamp;
        bool size;
        char element[ELEMSIZE];
};


static xmlSAXHandler rpm_handlers;
static xmlSAXHandler repomd_handlers;

void rpm_startdocument(struct rpm_parserstate *ctx)
{
        ctx->index = 0;
}

void rpm_startelement(struct rpm_parserstate *ctx, const xmlChar* fullname, const xmlChar** atts)
{
        if ( strcmp("package", (char *)fullname) == 0 && strcmp("type", (char *)atts[0]) == 0 && strcmp("rpm", (char *)atts[1]) == 0) {
                ctx->package = 1;
        }
        if (!ctx->rpms) return;

        // in package scope?
        if (ctx->package) {
                strcpy(ctx->element,  (char *)fullname);
                if ( strcmp("version",(char *)fullname) == 0) {
                        int i=0;
                        while ( atts && atts[i] && atts[i+1] ){
                                if (strcmp("ver", (char *)atts[i]) == 0) {
                                        ctx->rpms[ctx->index].version = strdup((char *)atts[i+1]);
                                }
                                if (strcmp("rel", (char *)atts[i]) == 0)
                                        ctx->rpms[ctx->index].release = strdup((char *)atts[i+1]);
                                i+=2;
                        }
                }
                if ( strcmp("checksum",(char *)fullname) == 0) {
                        int i=0;
                        while ( atts && atts[i] && atts[i+1] ){
                                if (strcmp("type", (char *)atts[i]) == 0) {
                                        ctx->rpms[ctx->index].checksum_type = strdup((char *)atts[i+1]);
                                }
                                i+=2;
                        }
                }
                if ( strcmp("location",(char *)fullname) == 0) {
                        int i=0;
                        while ( atts && atts[i] && atts[i+1] ){
                                if (strcmp("href", (char *)atts[i]) == 0) {
                                        ctx->rpms[ctx->index].location = strdup((char *)atts[i+1]);
                                }
                                i+=2;
                        }
                }
                if ( strcmp("size",(char *)fullname) == 0) {
                        int i=0;
                        while ( atts && atts[i] && atts[i+1] ){
                                if (strcmp("package", (char *)atts[i]) == 0) {
                                        ctx->rpms[ctx->index].size = atoi((char *)atts[i+1]);
                                }
                                i+=2;
                        }
                }
        }
}

static void rpm_endelement( struct rpm_parserstate *ctx, const xmlChar* name)
{
        if ( strcmp("package", (char *)name) == 0 ) {
                ctx->package = 0;
                ctx->index++;
        }
        ctx->element[0]='\0';
}

static void rpm_character( struct rpm_parserstate *ctx, const xmlChar *ch, int len)
{
        char chars[len + 1];
        strncpy(chars, (const char *)ch, len);
        chars[len] = '\0';
        if (!ctx->rpms)
                return;
        if (!ctx->package)
                return;
        if (strlen(ctx->element) == 0)
                return;
        if (strcmp("name", (char *)ctx->element) == 0) {
                ctx->rpms[ctx->index].name = strdup(chars);
        } else if (strcmp("arch", (char *)ctx->element) == 0) {
                ctx->rpms[ctx->index].arch = strdup(chars);
        } else if (strcmp("checksum", (char *)ctx->element) == 0) {
                ctx->rpms[ctx->index].checksum = strdup(chars);
        }
}


struct rpm *rpms_from_xml(char *xml, int *num_results)
        /* 
         * parse xml-string from memory and return array of struct rpm. Also set num_results.
         */
{
        debug(1,"in rpms_from_xml:");
        struct rpm_parserstate my_state;
        int retval = 0;
        my_state.rpms = NULL;
        *num_results = 0;
        rpm_handlers.startDocument = (void *)rpm_startdocument;
        rpm_handlers.startElement = (void *)rpm_startelement;
        rpm_handlers.endElement = (void *)rpm_endelement;
        rpm_handlers.characters = (void *)rpm_character;
        retval = xmlSAXUserParseMemory(&rpm_handlers,&my_state,xml,strlen(xml));
        if (retval != 0) exit(1);
        my_state.rpms = calloc( my_state.index + 1, sizeof(struct rpm));
        retval = xmlSAXUserParseMemory(&rpm_handlers,&my_state,xml,strlen(xml));

        debug(-1,"rpms_from_xml done\n");
        *num_results=my_state.index;
        return my_state.rpms;
}

void check_repo_name(char *repo)
{
        if (repo[strlen(repo) - 1] == '/')
                repo[strlen(repo) - 1] = '\0';
}

int get_rpms(char *repo, char *repo_xml, struct rpm **retval, int *result_size)
{
        int ret = 0;
        //        printf("in get_rpms: %s\n",repo);
        debug(1,"in get_rpms: ");
        //        check_repo_name(repo);
        char *xml;
        ret = get_primary_xml(repo, repo_xml, &xml);
        if (ret != 0) return 1;
        struct rpm *rpms = rpms_from_xml(xml, result_size);
        debug(0,"free(xml)");
        free(xml);
        *retval = rpms;
        debug(-1,"get_rpms done");
        if (rpms) {
                return 0;
        } else {
                return 1;
        }
}

void repomd_startdocument(struct repomd_parserstate *ctx)
{
        ctx->index = 0;
}

void repomd_startelement(struct repomd_parserstate *ctx, const xmlChar* fullname, const xmlChar** atts)
{
        if ( strcmp("data", (char *)fullname) == 0 && strcmp("type", (char *)atts[0]) == 0 ) {
                ctx->data = 1;
        }
        if (!ctx->repofiles) return;

        // in data scope?
        if (ctx->data) {
                strcpy(ctx->element,  (char *)fullname);
                if ( strcmp("data",(char *)fullname) == 0) {
                        int i=0;
                        while ( atts && atts[i] && atts[i+1] ){
                                if (strcmp("type", (char *)atts[i]) == 0) {
                                        ctx->repofiles[ctx->index].name = strdup((char *)atts[i+1]);
                                }
                                i+=2;
                        }
                }
                if ( strcmp("checksum",(char *)fullname) == 0) {
                        int i=0;
                        while ( atts && atts[i] && atts[i+1] ){
                                if (strcmp("type", (char *)atts[i]) == 0) {
                                        ctx->repofiles[ctx->index].checksum_type = strdup((char *)atts[i+1]);
                                }
                                i+=2;
                        }
                }
                if ( strcmp("location",(char *)fullname) == 0) {
                        int i=0;
                        while ( atts && atts[i] && atts[i+1] ){
                                if (strcmp("href", (char *)atts[i]) == 0) {
                                        ctx->repofiles[ctx->index].location = strdup((char *)atts[i+1]);
                                }
                                i+=2;
                        }
                }
        }
}

static void repomd_endelement( struct repomd_parserstate *ctx, const xmlChar* name)
{
        if ( strcmp("data", (char *)name) == 0 ) {
                ctx->data = 0;
                ctx->index++;
        }
        ctx->element[0]='\0';
}

static void repomd_character( struct repomd_parserstate *ctx, const xmlChar *ch, int len)
{
        char chars[len + 1];
        strncpy(chars, (const char *)ch, len);
        chars[len] = '\0';
        if (!ctx->repofiles)
                return;
        if (!ctx->data)
                return;
        if (strlen(ctx->element) == 0)
                return;
        if (strcmp("timestamp", (char *)ctx->element) == 0) {
                ctx->repofiles[ctx->index].timestamp = atoi(chars);
        } else if (strcmp("size", (char *)ctx->element) == 0) {
                ctx->repofiles[ctx->index].size = atoi(chars);
        } else if (strcmp("checksum", (char *)ctx->element) == 0) {
                ctx->repofiles[ctx->index].checksum = strdup(chars);
        }
}

int get_repofiles_from_repomd(char *xml, struct repofile **repofiles, int *repofiles_size)
{
        struct repomd_parserstate my_state;
        int retval = 0;
        my_state.repofiles = NULL;
        *repofiles_size = 0;
        //        xmlChar *primary_ns = (xmlChar *)"http://linux.duke.edu/metadata/common";
        repomd_handlers.startDocument = (void *)repomd_startdocument;
        repomd_handlers.startElement = (void *)repomd_startelement;
        repomd_handlers.endElement = (void *)repomd_endelement;
        repomd_handlers.characters = (void *)repomd_character;
        retval = xmlSAXUserParseMemory(&repomd_handlers,&my_state,xml,strlen(xml));
        if (retval != 0) exit(1);
        //        fprintf(stderr,"xmlSAXUserParseMemory returned %d\n",retval); 
        //        fprintf(stderr,"my_state.index %d\n",my_state.index); 
        my_state.repofiles = calloc( my_state.index + 1, sizeof(struct repofile));
        retval = xmlSAXUserParseMemory(&repomd_handlers,&my_state,xml,strlen(xml));

        debug(-1,"get_repofiles_from_repomd done\n");
        *repofiles_size=my_state.index;
        *repofiles = my_state.repofiles;
        return 0;
}

void compare_repos(struct rpm *src_rpms, int src_size,struct rpm *dst_rpms, int dst_size)
{
        /*
         * create hashes as comparings 10k element arrays is not useful. hash on name-arch; there might/will be multiple results.
         * or... sort both arrays and walk through them.
         * either copy whole rpm struct or just an index number to the existing array?
         * options: keep X, clean_dst, sync last X.
         * in src but not in dst:
         * in dst but not in src:
         *
         * action field in src means copy, dst means delete.
         * before actual copy check if file exists locally, might not have run createrepo yet.
         *
         */
        debug(1,"compare_repos");
        debug(0,"sort(src)");
        sort_rpms(src_rpms, src_size);
        debug(0,"sort(dst)");
        sort_rpms(dst_rpms, dst_size);
        debug(0,"simple_in_a_not_b");
        simple_in_a_not_b(src_rpms, src_size, dst_rpms, dst_size); // copy these
        debug(0,"cleanup_source");
        cleanup_source(src_rpms, src_size, last_n); // last_n, should others be removed from dst? If so, do this after the below bit.
        if (purge) {
                simple_in_a_not_b(dst_rpms, dst_size, src_rpms, src_size); // delete these
                debug(0,"purge=0, simple_in_a_not_b(dst,src)");
        }
        count_actions(src_rpms, src_size, &stats.to_download, &stats.to_download_bytes);
        count_actions(dst_rpms, dst_size, &stats.to_delete, &stats.to_delete_bytes);
        debug(-1,"compare_repos done");
}

void download_rpms(char *baseurl, struct rpm *rpms, int size, char *targetdir)
{
        int i, counter = 0;
        for (i = 0; i<size;i++) {
                if ( rpms[i].action ) {
                        char *fullpath;
                        char *fullsrc;
                        counter++;

                        // ensure location does not start with / or has ../.
                        char *clean_location = strdup(rpms[i].location);
                        char *t = clean_location;

                        if ( t[0] == '/' ) {
                                memmove(t, t + 1, strlen(t) - 1);
                        }
                        while( (t = strstr(t, "../")) )
                                memmove(t, t + strlen("../"), 1 + strlen(t + strlen("../")));

                        ensure_dir(targetdir, clean_location);
                        asprintf(&fullpath, "%s/%s", targetdir, clean_location);
                        asprintf(&fullsrc, "%s/%s", baseurl, rpms[i].location);
                        if ( check_rpm_exists(fullpath, rpms[i].size,  rpms[i].checksum_type, rpms[i].checksum) != 0 ) {
                                if (verbose) printf("Skipping download %d/%d of %s already exists.\n", counter, stats.to_download, rpms[i].location);
                                stats.download_skipped++;
                                stats.download_skipped_bytes+=rpms[i].size;
                                free(fullpath);
                                continue;
                        }
                        if (noop) {
                                if (verbose) printf("NOOP: download %d/%d %s\n", counter, stats.to_download, rpms[i].location);
                        } else {
                                if (verbose) printf("Downloading %d/%d %s..\n", counter, stats.to_download, fullsrc);
                                FILE *fp=fopen(fullpath, "wb");
                                if (fp==NULL) {
                                        perror("Error downloading: ");
                                        exit(1);
                                }

                                get_http_to_file(fp, fullsrc, 1);
                                fclose(fp);
                        }
                        free(fullpath);
                }
        }
}

void delete_rpms(char *targetdir, struct rpm *rpms, int size)
{
        int i;
        for (i = 0; i < size; i++) {
                if ( rpms[i].action ) {
                        char *path;
                        asprintf(&path,"%s/%s", targetdir, rpms[i].location);
                        if (noop) {
                                if (verbose) printf("NOOP: delete %s/%s\n", targetdir, rpms[i].location);
                        } else {
                                if (verbose) printf("delete %s/%s\n", targetdir, rpms[i].location);
                                int ret = unlink(path);
                                if (ret != 0) {
                                        perror("Error unlinking rpm. Run createrepo?");
                                } else {
                                        stats.deleted++;
                                        stats.deleted_bytes+=rpms[i].size;
                                }
                        }
                        free(path);
                }
        }
}

int sync_repo(char *src, char *dst)
{
        // printf("in sync_repo: %s -> %s\n",src,dst);
        struct rpm *rpm_src_ptr, *rpm_dst_ptr;
        int rpm_src_size = 0, rpm_dst_size = 0;
        debug(1,"sync_repo");
        check_repo_name(src);
        check_repo_name(dst);
        bool gotcomps = false;
        char *src_xml;
        char *dst_xml;
        int retval = 0;
        retval = get_repomd_xml(src, &src_xml);
        if (retval != 0) return 1;
        retval = get_repomd_xml(dst, &dst_xml);
        if (retval != 0) return 1;

        int src_repofiles_size;
        struct repofile *src_repofiles;
        get_repofiles_from_repomd(src_xml, &src_repofiles, &src_repofiles_size);
        if (getcomps) {
                int i;
                for (i=0; i< src_repofiles_size; i++) {
                        if (strcmp(src_repofiles[i].name, "group")==0) {
                                char *fullpath;
                                char *fullsrc;
                                asprintf(&fullpath, "%s/comps.xml", dst);
                                asprintf(&fullsrc, "%s/%s", src, src_repofiles[i].location);

                                if ( check_rpm_exists(fullpath, src_repofiles[i].size,  src_repofiles[i].checksum_type, src_repofiles[i].checksum) != 0 ) {
                                        // if (verbose) printf("Skipping downloading identical comps.xml.\n");
                                        free(fullpath);
                                        continue;
                                }

                                if (noop) {
                                        if (verbose) printf("NOOP: %s -> comps.xml\n", fullsrc);
                                }  else {
                                        FILE *fp=fopen(fullpath, "wb");
                                        if (fp==NULL) {
                                                perror("Error downloading comps.xml: ");
                                                exit(1);
                                        }
                                        get_http_to_file(fp, fullsrc, 1);
                                        fclose(fp);
                                        gotcomps = true;
                                }
                                free(fullpath);
                                free(fullsrc);
                                break;

                        }
                }
        }
        debug(0,"get_rpms(dst)");
        if (get_rpms(dst, dst_xml, &rpm_dst_ptr, &rpm_dst_size) != 0)
                perror("Destination repodata/ not found. Run createrepo?\n");
        debug(0,"print_rpms(dst)");
        stats.dst_size = rpm_dst_size;
        debug(0,"get_rpms(src)");
        if (get_rpms(src, src_xml, &rpm_src_ptr, &rpm_src_size) != 0) {
                perror("source repodata not found\n");
                exit(1);
        }
        stats.src_size = rpm_src_size;
        if (rpm_src_size == 0) {
                perror("source does not contain any rpms?\n");
                exit(1);
        }
        if (verbose) {
                printf("src: %d rpms in %s\n",rpm_src_size, src);
                printf("dst: %d rpms in %s\n",rpm_dst_size, dst);
        }
        debug(0,"compare_repos");
        compare_repos(rpm_src_ptr, rpm_src_size, rpm_dst_ptr, rpm_dst_size);
        stats.downloaded = 0;
        stats.down_bytes = 0;
        stats.download_skipped = 0;
        stats.download_skipped_bytes = 0;
        stats.deleted = 0;
        stats.deleted_bytes = 0;
        debug(0,"download_rpms");
        download_rpms(src, rpm_src_ptr, rpm_src_size, dst);
        debug(0,"delete_rpms");
        delete_rpms(dst, rpm_dst_ptr, rpm_dst_size);

        if (updaterepodata) {
                if ( stats.downloaded + stats.deleted == 0 && gotcomps == false ) { 
                        printf("no need to run createrepo\n");
                        return 0;
                }
                char *command;
                if (gotcomps)  {
                        asprintf(&command, "createrepo --update --pretty --workers 2 --groupfile %s/comps.xml %s\n", dst, dst);
                } else {
                        asprintf(&command, "createrepo --update --pretty --workers 2 %s\n", dst);
                }
                printf("Running %s", command);
                system(command);
                free(command);
        }

        debug(0,"sync_repo done.");
        debug(-1,"sync_repo done");
        return 0;
}

void debug(int indent, char *message)
{
        return;
        struct timeval temptime;
        static int indentation;
        indentation += indent;
        gettimeofday(&temptime, 0);
        int i;
        fprintf(stderr,"DEBUG:");
        for (i=0; i < indentation; i++)
                fprintf(stderr,"-");
        fprintf(stderr,"%.1f/%.0fms %s\n",
                                ( temptime.tv_sec - stats.prevtime.tv_sec) * 1000.0f + (temptime.tv_usec - stats.prevtime.tv_usec ) / 1000.0f,
                                ( temptime.tv_sec - stats.starttime.tv_sec) * 1000.0f + (temptime.tv_usec - stats.starttime.tv_usec ) / 1000.0f,
                                message
                 );
         gettimeofday(&stats.prevtime, 0);
}


int main(int argc, char **argv)
{
        char *src_repo_ptr;
        char *dst_repo_ptr;
        gettimeofday(&stats.starttime, 0);
        gettimeofday(&stats.prevtime, 0);

        get_options(argc, argv, &src_repo_ptr, &dst_repo_ptr);

        if (src_repo_ptr == NULL || dst_repo_ptr == NULL || strlen(src_repo_ptr) == 1 || strlen(dst_repo_ptr) == 1) {
                usage();
                return 1;
        }
        debug(0,"main start");
        LIBXML_TEST_VERSION
        printf("Reposync2 started.\n");
        sync_repo(src_repo_ptr, dst_repo_ptr);
        debug(0,"main done");
        /*
        printf("stats.src_size=%d\n", stats.src_size);
        printf("stats.dst_size=%d\n", stats.dst_size);
        printf("stats.to_download=%d\n", stats.to_download);
        printf("stats.to_download_bytes=%d\n", stats.to_download_bytes);
        printf("stats.to_delete=%d\n", stats.to_delete);
        printf("stats.to_delete_bytes=%d\n", stats.to_delete_bytes);
        printf("stats.downloaded=%d\n", stats.downloaded);
        printf("stats.download_skipped=%d\n", stats.download_skipped);
        printf("stats.deleted=%d\n", stats.deleted);
        printf("stats.down_bytes=%d\n", stats.down_bytes);
        */
        gettimeofday(&stats.prevtime, 0);
        printf("Completed: %d/%.2f MB downloaded %d/%.2f MB skipped %d/%.2f MB deleted rpms in %.2fs.\n",
                         stats.downloaded, 1.0 * stats.down_bytes / (1024 * 1024),
                         stats.download_skipped, 1.0 * stats.download_skipped_bytes / (1024 * 1024),
                         stats.deleted, 1.0 * stats.deleted_bytes / (1024 * 1024),
                        ( stats.prevtime.tv_sec - stats.starttime.tv_sec ) +
                        ( stats.prevtime.tv_usec - stats.starttime.tv_usec ) / 1000000.0f
              );

        xmlCleanupParser();
        return 0;
}


