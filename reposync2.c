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

#define ELEMSIZE 100

struct ParserState {
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

static xmlSAXHandler my_handlers;

void OnStartDocument(struct ParserState *ctx)
{
        ctx->index = 0;
}

void OnStartElement(struct ParserState *ctx, const xmlChar* fullname, const xmlChar** atts)
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

static void OnEndElement( struct ParserState *ctx, const xmlChar* name)
{
        if ( strcmp("package", (char *)name) == 0 ) {
                ctx->package = 0;
                ctx->index++;
        }
        ctx->element[0]='\0';
}

static void OnCharacters( struct ParserState *ctx, const xmlChar *ch, int len)
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
        struct ParserState my_state;
        int retval = 0;
        my_state.rpms = NULL;
        *num_results = 0;
//        xmlChar *primary_ns = (xmlChar *)"http://linux.duke.edu/metadata/common";
        my_handlers.startDocument = (void *)OnStartDocument;
        my_handlers.startElement = (void *)OnStartElement;
        my_handlers.endElement = (void *)OnEndElement;
        my_handlers.characters = (void *)OnCharacters;
        retval = xmlSAXUserParseMemory(&my_handlers,&my_state,xml,strlen(xml));
        if (retval != 0) exit(1);
//        fprintf(stderr,"xmlSAXUserParseMemory returned %d\n",retval); 
//        fprintf(stderr,"my_state.index %d\n",my_state.index); 
        my_state.rpms = calloc( my_state.index + 1, sizeof(struct rpm));
        retval = xmlSAXUserParseMemory(&my_handlers,&my_state,xml,strlen(xml));

        debug(-1,"rpms_from_xml done\n");
        *num_results=my_state.index;
        return my_state.rpms;
}

int get_rpms(char *repo, struct rpm **retval, int *result_size)
{
        int ret = 0;
//        printf("in get_rpms: %s\n",repo);
        debug(1,"in get_rpms: ");
        if (repo[strlen(repo) - 1] == '/')
                repo[strlen(repo) - 1] = '\0';
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

int rpm_compare(struct rpm *p1, struct rpm *p2)
{
        // compare name, arch, version, release
        int name_cmp = strcmp((const char *)p1->name, (const char *)p2->name);
        if (name_cmp != 0) {
                return name_cmp;
        } 
        // compare arch
        int arch_cmp = strverscmp((const char *)p1->arch, (const char *)p2->arch);
        if (arch_cmp != 0) {
                return arch_cmp;
        }
        // compare versions.
        int ver_cmp = strverscmp((const char *)p1->version, (const char *)p2->version);
        if (ver_cmp != 0) {
                return ver_cmp;
        }
        // compare releases.
        int rel_cmp = strverscmp((const char *)p1->release, (const char *)p2->release);
        if (rel_cmp != 0) {
                return rel_cmp;
        }
        return 0;
}

void sort_rpms(struct rpm *rpms, int size)
{
        qsort(rpms, size, sizeof(struct rpm), (__compar_fn_t)rpm_compare);
}

void simple_in_a_not_b(struct rpm *src_rpms,int src_size,struct rpm *dst_rpms,int dst_size)
{
        /*
         * improvements; keep position of previous name+arch strcmp < 0 in dst as starting position for next search. 
         * stop searching when strcmp names < 0 (i.e. src < dst or dst  after src.
         */
        int src;
        int dst_start=0;
        for (src=0;src<src_size;src++) {
                int dst = 0;
                bool found = 0;
                for (dst = dst_start; dst<dst_size; dst++) {
                        if ( rpm_compare(&src_rpms[src], &dst_rpms[dst]) == 0 ) {
                                found = 1;
                                break;
                        } else {
                                int cmp = strcmp(src_rpms[src].name, dst_rpms[dst].name);
                                if ( cmp > 0 ) { // move up start of dst search if name > dst_name
                                        // printf("moving dst_start to %s:%s %d/%d\n",src_rpms[src].name, dst_rpms[dst].name,dst_start,dst_size);
                                        dst_start = dst;
                                } else if ( cmp < 0 ) { // stop searching if name in dest is after src. they are sorted.
                                        // printf("early exit. %s:%s %d/%d\n",src_rpms[src].name, dst_rpms[dst].name,dst,dst_size);
                                        found = 0;
                                        break;
                                }
                        }
                }
                if (found != 0) {
                        src_rpms[src].action = 0;
                } else {
                        src_rpms[src].action = 1;
                }
        }
}

void cleanup_source(struct rpm *rpms, int size,int last_n)
{
        /*
         * work backwards. check for same, when more than last_n same ones, clear action flag to ignore them.
         */
        if (last_n < 1) { return;}
        int i = size - 1;
        int same;
        char *name, *arch;
        while (i>=0) {
                same = 0;
                name = strdup(rpms[i].name);
                arch = strdup(rpms[i].arch);
                //    printf("considering %d/%d: %s-%s\n",i,size,name,arch);
                while (
                                i>=0 &&
                                strcmp(name,rpms[i].name) == 0 &&
                                strcmp(arch,rpms[i].arch) == 0 
                      ) {
                        same++;
                        // if (same > 1) {
                        //   printf("found same on position %d, same=%d\n",i,same);
                        // }
                        // if same > last_n; then clear action flag.
                        if (same > last_n) {
                                // printf("last_n=%d, ignoring %s-%s-%s-%s\n", last_n, rpms[i].name, rpms[i].version, rpms[i].release, rpms[i].arch);
                                rpms[i].action = 0;
                        }
                        i--;
                }
                free(name);
                free(arch);
        } 

}

void count_actions(struct rpm *rpms, int size, int *count, int *bytes)
{
        int i;
        *count = 0;
        *bytes = 0;
        for (i = 0; i<size;i++) {
                if ( rpms[i].action ) {
                        *count+=1;
                        *bytes+=rpms[i].size;
                }
        }
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
                        ensure_dir(targetdir, rpms[i].location);
                        asprintf(&fullpath, "%s/%s", targetdir, rpms[i].location);
                        asprintf(&fullsrc, "%s/%s", baseurl, rpms[i].location);
                        if ( check_rpm_exists(targetdir, rpms[i]) != 0 ) {
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

