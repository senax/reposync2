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
bool noop=0;
bool verifyssl=1;
bool getcomps=0;
bool getothermd=0;
bool keep=1;
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
//        struct rpm *rpms = NULL;
        struct rpm_parserstate my_state;
        int retval = 0;
        my_state.rpms = NULL;
        *num_results = 0;
//        xmlChar *primary_ns = (xmlChar *)"http://linux.duke.edu/metadata/common";
        rpm_handlers.startDocument = (void *)rpm_startdocument;
        rpm_handlers.startElement = (void *)rpm_startelement;
        rpm_handlers.endElement = (void *)rpm_endelement;
        rpm_handlers.characters = (void *)rpm_character;
        retval = xmlSAXUserParseMemory(&rpm_handlers,&my_state,xml,strlen(xml));
        if (retval != 0) exit(1);
//        fprintf(stderr,"xmlSAXUserParseMemory returned %d\n",retval); 
//        fprintf(stderr,"my_state.index %d\n",my_state.index); 
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

int get_rpms(char *repo, struct rpm **retval, int *result_size)
{
        int ret = 0;
//        printf("in get_rpms: %s\n",repo);
        debug(1,"in get_rpms: ");
        check_repo_name(repo);
        char *xml;
        ret = get_primary_xml(repo, &xml);
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
//                ctx->repofiles[ctx->index].location = strdup((char *)atts[i+1]);
//                printf("NAME=%s\n", atts[1]);
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
        printf("HERE IN GET_REPOFILES_FROM_REPOMD\n\n");
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
        // printf("src_size=%d\n",src_size);
        // printf("dst_size=%d\n",dst_size);
        debug(1,"compare_repos");
        debug(0,"sort(src)");
        sort_rpms(src_rpms, src_size);
        debug(0,"sort(dst)");
        sort_rpms(dst_rpms, dst_size);
        debug(0,"simple_in_a_not_b");
        simple_in_a_not_b(src_rpms, src_size, dst_rpms, dst_size); // copy these
        debug(0,"cleanup_source");
        cleanup_source(src_rpms, src_size, last_n); // last_n, should others be removed from dst? If so, do this after the below bit.
        if (keep == 0) {
                simple_in_a_not_b(dst_rpms, dst_size, src_rpms, src_size); // delete these
                debug(0,"keep=0, simple_in_a_not_b(dst,src)");
        }
        count_actions(src_rpms, src_size, &stats.to_download, &stats.to_download_bytes);
        count_actions(dst_rpms, dst_size, &stats.to_delete, &stats.to_delete_bytes);
        printf("%d rpms and %d bytes to download.\n", stats.to_download, stats.to_download_bytes);
        printf("%d rpms and %d bytes to delete.\n", stats.to_delete, stats.to_delete_bytes);
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
                        // print_rpms(&rpms[i], 1);
                        // printf("download %s/%s, %ld to %s\n",baseurl,rpms[i].location, rpms[i].size,targetdir);
                        char *clean_location = strdup(rpms[i].location);
                        char *t = clean_location;
                        while( (t = strstr(t, "../")) )
                                        memmove(t, t+strlen("../"), 1 + strlen(t + strlen("../")));
                        ensure_dir(targetdir, clean_location);
                        asprintf(&fullpath, "%s/%s", targetdir, clean_location);
                        asprintf(&fullsrc, "%s/%s", baseurl, rpms[i].location);
                        if ( check_rpm_exists(fullpath, rpms[i]) != 0 ) {
                                printf("Skipping download %d/%d of %s already exists.\n", counter, stats.to_download, rpms[i].location);
                                stats.download_skipped++;
                                continue;
                        }
                        stats.downloaded++;
                        if (noop) {
                                // printf("NOOP: curl %s -> %s\n", fullsrc, fullpath);
                                printf("NOOP: download %d/%d %s\n", counter, stats.to_download, rpms[i].location);
                        } else {
                                printf("Downloading %d/%d %s..\n", counter, stats.to_download, fullsrc);
                                FILE *fp=fopen(fullpath, "wb");
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
                        // print_rpms(&rpms[i], 1);
                        stats.deleted++;
                        asprintf(&path,"%s/%s", targetdir, rpms[i].location);
                        if (noop) {
                                printf("NOOP: delete %s/%s\n", targetdir, rpms[i].location);
                        } else {
                                printf("delete %s/%s\n", targetdir, rpms[i].location);
                                unlink(path);
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
        debug(0,"get_rpms(dst)");
        if (get_rpms(dst, &rpm_dst_ptr, &rpm_dst_size) != 0)
                perror("Destination repodata/ not found. Run createrepo?\n");
        debug(0,"print_rpms(dst)");
        stats.dst_size = rpm_dst_size;
        stats.downloaded = 0;
//        print_rpms(rpm_dst_ptr, rpm_dst_size);
//        exit(0);
        debug(0,"get_rpms(src)");
        if (get_rpms(src, &rpm_src_ptr, &rpm_src_size) != 0) {
                perror("source repodata not found\n");
                exit(1);
        }
        stats.src_size = rpm_src_size;
        if (rpm_src_size == 0) {
                perror("source does not contain any rpms?\n");
                exit(1);
        }
        printf("src: %d rpms in %s\n",rpm_src_size, src);
        printf("dst: %d rpms in %s\n",rpm_dst_size, dst);
        debug(0,"compare_repos");
        compare_repos(rpm_src_ptr, rpm_src_size, rpm_dst_ptr, rpm_dst_size);
        debug(0,"download_rpms");
        download_rpms(src, rpm_src_ptr, rpm_src_size, dst);
        debug(0,"delete_rpms");
        delete_rpms(dst, rpm_dst_ptr, rpm_dst_size);
        debug(0,"sync_repo done.");
        // print_rpms(rpm_src_ptr, rpm_src_size);
        // print_rpms(rpm_dst_ptr, rpm_dst_size);
//        printf("\n");
//        printf("src: %d rpms in %s\n",rpm_src_size, src);
//        printf("dst: %d rpms in %s\n",rpm_dst_size, dst);
        if (getcomps != 0 && noop==0 ) {
                printf("COMPS: %s to %s\n", group_file, dst);
                char *fullpath;
                char *fullsrc;
                asprintf(&fullpath, "%s/comps", dst);
                asprintf(&fullsrc, "%s/%s", src, group_file);
                FILE *fp=fopen(fullpath, "wb");
                get_http_to_file(fp, fullsrc, 1);
                fclose(fp);
                free(fullpath);
                free(fullsrc);
        } 
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
        // diff starttime - temptime = total time
        // diff now - temptime = diff
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
//        printf("src=%s\n", src_repo_ptr);
//        printf("dst='%s'\n", dst_repo_ptr);
        //        printf("keep=%d\n", keep);
        //        printf("last_n=%d\n", last_n);
        //        printf("noop=%d\n", noop);
        //        printf("comps=%d\n", getcomps);
        //        printf("othermd=%d\n", getothermd);
        sync_repo(src_repo_ptr, dst_repo_ptr);
        debug(0,"main done");
        gettimeofday(&stats.prevtime, 0);
        printf("Completed in %.2fs\n",
                ( stats.prevtime.tv_sec - stats.starttime.tv_sec ) +
                ( stats.prevtime.tv_usec - stats.starttime.tv_usec ) / 1000000.0f
               );

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
        printf("Done.\n");
        xmlCleanupParser();
        return 0;

}

