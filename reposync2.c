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
#include <getopt.h>

#include "misc.h"


struct rpm *rpms_from_xml(char *xml, int *num_results)
{
        // printf("in rpms_from_xml:\n");
        struct rpm *rpms;
        xmlChar *primary_ns = (xmlChar *)"http://linux.duke.edu/metadata/common";
        xmlDocPtr doc;
        doc = xmlReadMemory(xml, strlen(xml), "noname.xml", NULL, 0);
        if (doc == NULL) {
                fprintf(stderr, "Failed to parse.\n");
                return NULL;
        }

        xmlNodeSetPtr nodeset;
        xmlChar *xpath = (xmlChar*) "//prefix:package[@type='rpm']/prefix:name";
        xmlXPathObjectPtr result;
        result = getnodeset(doc, primary_ns, xpath);
        if (result) {
                int i;
                nodeset = result->nodesetval;
                *num_results = nodeset->nodeNr  ;
                // printf("num_results=%d\n",*num_results);
                rpms = calloc(*num_results*sizeof(*rpms), 1);
                for (i=0; i < *num_results  ; i++) {
                        // printf("looking for rpm details for result. i=%d\n",i);
                        xmlNode *cur = nodeset->nodeTab[i];
                        while ( cur != NULL ) {
                                if ( cur->type == XML_ELEMENT_NODE ) { // xmlNodeGetContent automatically walks the text child nodes.
                                        if (!xmlStrcmp(cur->name, (const xmlChar *)"name")) {
                                                rpms[i].name = (char *)xmlNodeGetContent(cur);
                                        }
                                        if (!xmlStrcmp(cur->name, (const xmlChar *)"arch")) { 
                                                rpms[i].arch = (char *)xmlNodeGetContent(cur);
                                        }
                                        if (!xmlStrcmp(cur->name, (const xmlChar *)"location")) { 
                                                rpms[i].location = (char *)xmlGetProp(cur, (const xmlChar *)"href");
                                        }
                                        if (!xmlStrcmp(cur->name, (const xmlChar *)"checksum")) { 
                                                rpms[i].checksum_type = (char *)xmlGetProp(cur, (const xmlChar *)"type");
                                                rpms[i].checksum = (char *)xmlNodeGetContent(cur);
                                        }
                                        if (!xmlStrcmp(cur->name, (const xmlChar *)"size")) { 
                                                rpms[i].size = strtol((const char * restrict) xmlGetProp(cur, (const xmlChar *)"package"), NULL, 10);
                                        }
                                        if (!xmlStrcmp(cur->name, (const xmlChar *)"version")) { // get ver rel
                                                rpms[i].version = (char *)xmlGetProp(cur, (const xmlChar *)"ver");
                                                rpms[i].release = (char *)xmlGetProp(cur, (const xmlChar *)"rel");
                                        }
                                }
                                cur = cur->next;
                        }
                        // printf("done search while cur loop\n");
                        // print_rpms(&rpms[i], 1);
                }

                xmlXPathFreeObject(result);
        } else {
                fprintf(stderr, "No results???\n");
                return NULL;
        }
        xmlFreeDoc(doc);
        return rpms;
}

int get_rpms(char *repo, struct rpm **retval, int *result_size)
{
        // printf("in get_rpms: %s\n",repo);
        char *xml;
        get_primary_xml(repo, &xml);
        struct rpm *rpms = rpms_from_xml(xml, result_size);
        *retval = rpms;
        return 0;
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
                                printf("last_n=%d, ignoring %s-%s-%s-%s\n", last_n, rpms[i].name, rpms[i].version, rpms[i].release, rpms[i].arch);
                                rpms[i].action = 0;
                        }
                        i--;
                }
                free(name);
                free(arch);
        } 

}

void compare_repos(struct rpm *src_rpms, int src_size,struct rpm *dst_rpms, int dst_size, bool keep, int last_n)
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
        sort_rpms(src_rpms, src_size);
        sort_rpms(dst_rpms, dst_size);
        simple_in_a_not_b(src_rpms, src_size, dst_rpms, dst_size); // copy these
        cleanup_source(src_rpms, src_size, last_n); // last_n, should others be removed from dst? If so, do this after the below bit.
        if (keep == 0) {
                simple_in_a_not_b(dst_rpms, dst_size, src_rpms, src_size); // delete these
        }
}

void download_rpms(char *baseurl, struct rpm *rpms, int size, char *targetdir, bool noop)
{
        int i;
        for (i = 0; i<size;i++) {
                if ( rpms[i].action ) {
                        char *fullpath;
                        char *fullsrc;
                        // print_rpms(&rpms[i], 1);
                        // printf("download %s/%s, %ld to %s\n",baseurl,rpms[i].location, rpms[i].size,targetdir);
                        ensure_dir(targetdir, rpms[i].location, noop);
                        asprintf(&fullpath, "%s/%s", targetdir, rpms[i].location);
                        asprintf(&fullsrc, "%s/%s", baseurl, rpms[i].location);
                        if ( check_rpm_exists(targetdir, rpms[i]) != 0 ) {
                                printf("Skipping down of %s already there but not in repodata.\n",rpms[i].location);
                                continue;
                        }
                        if (noop) {
                                printf("NOOP: curl %s -> %s\n", fullsrc, fullpath);
                        } else {
                                // printf("Downloading %s..\n",fullsrc);
                                FILE *fp=fopen(fullpath, "wb");
                                get_http_to_file(fp, fullsrc, 1);
                                fclose(fp);
                        }
                        free(fullpath);
                }
        }
}

void delete_rpms(char *targetdir, struct rpm *rpms, int size, bool noop)
{
        int i;
        for (i = 0; i < size; i++) {
                if ( rpms[i].action ) {
                        char *path;
                        // print_rpms(&rpms[i], 1);
                        printf("delete %s/%s\n", targetdir, rpms[i].location);
                        asprintf(&path,"%s/%s", targetdir, rpms[i].location);
                        unlink(path);
                        free(path);
                }
        }
}

int sync_repo(char *src, char *dst, bool keep, int last_n, bool noop)
{
        // printf("in sync_repo: %s -> %s\n",src,dst);
        struct rpm *rpm_src_ptr, *rpm_dst_ptr;
        int rpm_src_size = 0, rpm_dst_size = 0;
        get_rpms(src, &rpm_src_ptr, &rpm_src_size);
        get_rpms(dst, &rpm_dst_ptr, &rpm_dst_size);
        compare_repos(rpm_src_ptr, rpm_src_size, rpm_dst_ptr, rpm_dst_size, keep, last_n);
        download_rpms(src, rpm_src_ptr, rpm_src_size, dst, noop);
        delete_rpms(dst, rpm_dst_ptr, rpm_dst_size, noop);
        // print_rpms(rpm_src_ptr, rpm_src_size);
        // print_rpms(rpm_dst_ptr, rpm_dst_size);
        return 0;
}

int main(int argc, char **argv)
{
        // char src_url[] = "http://rhel-packages/latest/apache-maven";
        /// char src_repo[]="file:///home/frank/c/SOURCE";
        char *src_repo;
        // char dst_repo[]="file:///home/frank/c/epel7_old";
        char *dst_repo;
        //char dst_repo[]="file:///home/frank/c/DEST";
        bool noop=0,keep=0; int last_n=0;
        int opt;
        opterr = 0;
        static struct option long_options[]= {
                {"source", required_argument,0,'s'},
                {"destination", required_argument,0,'d'},
                {"keep", no_argument, 0, 'k'},
                {"noop", no_argument, 0, 'n'},
                {"last", required_argument,0,'l'},
                {0,0,0,0},
        };
        int option_index=0;

        while ((opt = getopt_long(argc, argv, "s:d:knl:", long_options, &option_index)) != -1) {
                //    printf("opt=%c\n",opt);
                switch(opt) {
                        //      case 0:
                        //        printf("option %s", long_options[option_index].name);
                        //        if (optarg)
                        //          printf(" with arg %s", optarg);
                        //        printf("\n");
                        //        break;
                        case 's':
                                if (optarg) {
                                        src_repo = optarg;
                                } else {
                                        printf("source needs an option\n");
                                        return 1;
                                }
                                break;
                        case 'd':
                                if (optarg) {
                                        dst_repo = optarg;
                                } else {
                                        printf("destination needs an option\n");
                                        return 1;
                                }
                                break;
                        case 'l':
                                //        if (optarg) {
                                //          printf(" l optarg= %s\n",optarg);
                                //        }
                                last_n=atoi(optarg);
                                break;
                        case 'k':
                                keep = 1;
                                break;
                        case 'n':
                                noop = 1;
                                break;
                        case '?':
                                break;
                        default:
                                abort();
                }
        }
        if (src_repo == NULL || dst_repo == NULL || strlen(src_repo) == 1 || strlen(dst_repo) == 1) {
                usage();
                return 1;
        }
        printf("src=%s\n", src_repo);
        printf("dst='%s'\n", dst_repo);
        printf("keep=%d\n", keep);
        printf("last_n=%d\n", last_n);
        printf("noop=%d\n", noop);
        sync_repo(src_repo, dst_repo, keep, last_n, noop);
        printf("Done.\n");
        xmlCleanupParser();
        return 0;
}

