/*
vim: cindent
vim: background=dark
vim: tabstop=8 shiftwidth=8 expandtab
*/
#define _GNU_SOURCE
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
#include <sys/stat.h>
#include <errno.h>
#include <openssl/sha.h>

#include "misc.h"

xmlChar *prefix= (xmlChar *)"prefix";
xmlChar *href= (xmlChar *)"http://linux.duke.edu/metadata/repo";

struct myprogress {
        double lastruntime;
        CURL *curl;
};

size_t write_file(FILE *fp, size_t size, size_t nmemb, FILE *stream)
{
        return fwrite(fp, size, nmemb, stream);
}

int get_http_to_file(FILE *fp,char *url, bool verbose)
{
        printf("get_http_to_file: %s\n",url);
        curl_global_init(CURL_GLOBAL_ALL);
        CURL *curl_handle;
        CURLcode res;
        struct myprogress prog;

        curl_handle=curl_easy_init();
        prog.lastruntime = 0;
        prog.curl = curl_handle;
        curl_easy_setopt(curl_handle, CURLOPT_URL, url);
        curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION, write_file);
        curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA, fp);
        curl_easy_setopt(curl_handle, CURLOPT_FOLLOWLOCATION, 1L);
        if (verbose) {
                curl_easy_setopt(curl_handle, CURLOPT_XFERINFOFUNCTION, xferinfo);
                curl_easy_setopt(curl_handle, CURLOPT_XFERINFODATA, &prog);
                curl_easy_setopt(curl_handle, CURLOPT_NOPROGRESS, 0L);
        }

        res=curl_easy_perform(curl_handle);
        if (res != CURLE_OK){
                fprintf(stderr,"curl_easy_perform() failed: %s\n",curl_easy_strerror(res));
                exit(1);
        }

        curl_easy_cleanup(curl_handle);
        curl_global_cleanup();
        return 0;
}

int uncompress_file(char *filename, Byte **content)
{
        gzFile gzfp;
        gzfp=gzopen(filename,"rb");
        Byte *uncompr;
        uLong uncomprLen=16;
        uLong len=uncomprLen;
        uncompr = (Byte*)calloc((uInt)uncomprLen, 1);
        while (len == uncomprLen ) {
                gzrewind(gzfp);
                uncomprLen *=2;
                free(uncompr);
                uncompr = (Byte*)calloc((uInt)uncomprLen, 1);
                len=gzread(gzfp,uncompr,uncomprLen);
        }
        gzclose(gzfp);
        *content = uncompr;
        return 0;
}

void print_element_names(xmlNode * a_node)
{
        xmlNode *cur_node = NULL;
        for (cur_node=a_node; cur_node; cur_node=cur_node->next) {
                if (cur_node->type == XML_ELEMENT_NODE) {
                        printf("node type: Element, name: %s\n", cur_node->name);
                } else if (cur_node->type == XML_TEXT_NODE){
                        printf("node type: %d, content: %s\n",cur_node->type, xmlNodeGetContent(cur_node));
                }
                print_element_names(cur_node->children);
        }
}

xmlXPathObjectPtr getnodeset(xmlDocPtr doc, xmlChar *namespace, xmlChar *xpath)
{
        xmlXPathContextPtr context;
        xmlXPathObjectPtr result;

        context = xmlXPathNewContext(doc);
        if (context == NULL) {
                printf("Error in xmlXPathNewContext\n");
                return NULL;
        }

        /* do register namespace */
        if(xmlXPathRegisterNs(context, prefix, (xmlChar *)namespace) != 0) {
                fprintf(stderr,"Error: unable to register NS with prefix=\"%s\" and href=\"%s\"\n", prefix, href);
                return NULL;
        }

        result = xmlXPathEvalExpression(xpath, context);
        xmlXPathFreeContext(context);
        if (result == NULL) {
                printf("Error in xmlXPathEvalExpression\n");
                return NULL;
        }
        if(xmlXPathNodeSetIsEmpty(result->nodesetval)){
                xmlXPathFreeObject(result);
                printf("No result\n");
                return NULL;
        }
        return result;
}

int get_primary_url(char *repomd_xml, char **postfix)
{
        // printf("get_primary_url:\n");
        xmlDocPtr doc;
        char *url_postfix=calloc(1000*sizeof(*url_postfix),1);
        doc = xmlReadMemory(repomd_xml,strlen(repomd_xml),"noname.xml",NULL,0);
        if (doc == NULL) {
                perror("get_primary_url: Failed to parse.\n");
                puts(repomd_xml);
                return 1;
        }

        xmlNodeSetPtr nodeset;
        xmlChar *xpath = (xmlChar*) "//prefix:data[@type='primary']/prefix:location"; 
        xmlXPathObjectPtr result;

        result = getnodeset(doc,href,xpath);

        if (result && result->nodesetval->nodeNr == 1 ) {
                nodeset = result->nodesetval;

                xmlChar *href = xmlGetProp(nodeset->nodeTab[0],(xmlChar *)"href");
                strncpy(url_postfix,(char *)href,1000);

                xmlXPathFreeObject(result);
        } else {
                fprintf(stderr,"No results???\n");
                return 1;
        }
        xmlFreeDoc(doc);

        *postfix=url_postfix;
        return 0;
}

int get_xml(char *url, char **xml)
{
        // get and optionally uncompress xml
        // printf("in get_xml: %s\n",url);
        Byte *content;
        if ( url[0] == '/') {
                uncompress_file(url,&content);
        } else {
                FILE *fp = fopen("tmp_primary.gz","w+b");
                get_http_to_file(fp,url,0);
                fclose(fp);
                uncompress_file("tmp_primary.gz",&content);
                // printf("CONTENT=%s\n",content);
        }
        *xml = (char *)content;
        return 0;
}

int get_primary_xml(char *repo, char **primary_xml)
{
        // printf("in get_primary_xml: %s\n",repo);
        char *repomd_url = malloc(strlen(repo) + strlen("/repodata/repomd.xml") + 1);
        sprintf(repomd_url,"%s/repodata/repomd.xml",repo);
        char *xml;
        get_xml(repomd_url, &xml);

        char *postfix;
        get_primary_url(xml, &postfix);
        free(xml);
        // printf("postfix=%s\n",postfix);
        char *primary_url = malloc(strlen(repo) + strlen(postfix) + 1 + 1 );
        sprintf(primary_url,"%s/%s",repo,postfix);
        get_xml(primary_url, &xml);

        *primary_xml = xml;
        return 0;
}

void print_rpms(struct rpm *rpms, int size)
{
        // printf("print_rpms: %d\n",size);
        int i;
        for (i=0;i<size;i++) {
                printf("rpm[%d]: %s",i, rpms[i].name);
                printf("-%s",rpms[i].version);
                printf("-%s",rpms[i].release);
                printf(".%s",rpms[i].arch);
                printf(" @  %s",rpms[i].location);
                printf(",%d",rpms[i].action);
                printf(",%zu\n",rpms[i].size);
        }
}


int ensure_dir(char *basedir, char *location, bool noop)
{
        // char location[]="z/zvbi-0.2.35-1.el7.x86_64.rpm";
        // ensure_dir("/home/frank/REPOSYNC/c/DEST",location);
        char *str_pos, *dirname, *path;
        size_t i=0;
        struct stat dirstat;
        // check that basedir exists
        if (lstat(basedir, &dirstat) == -1) {
                perror("Destination basedir check");
                exit(1);
        }
        while (( str_pos = strchr(location + i , '/')) != NULL ) {
                i = str_pos - location + 1;
                dirname = strndup(location, i - 1); // no trailing /
                asprintf(&path,"%s/%s",basedir,dirname);
                // printf("i=%d dirname=%s, str_pos=%s path=%s\n",i, dirname, str_pos, path);
                free(dirname);
                if ( lstat(path, &dirstat) == -1) {
                        // path does not exist; create it.
                        if (noop) {
                                printf("mkdir(\"%s\")\n",path);
                        } else if ( mkdir(path, 0755) != 0 ) {
                                perror("mkdir path");
                                exit(1);
                        }
                } else if (S_ISDIR(dirstat.st_mode)) {
                        // excellent, path is already a directory
                        goto free_path;
                } else {
                        printf("destination exists but is not a directory. %s\n",path);
                        exit(1);
                }
free_path:
                free(path);
        }
        return 0;
}


int calc_sha256(char *path, char *output)
{
        FILE *file = fopen(path,"rb");
        if (!file) return -1;
        unsigned char hash[SHA256_DIGEST_LENGTH];
        SHA256_CTX sha256;
        SHA256_Init(&sha256);
        const int bufsize = 32 * 1024;
        char *buffer = malloc(bufsize*sizeof(*buffer));
        if (!buffer) return 01;
        int bytesread=0;
        while((bytesread = fread(buffer, 1,bufsize, file))) {
                SHA256_Update(&sha256, buffer, bytesread);
        }
        SHA256_Final(hash, &sha256);

        int i;
        for (i=0;i<SHA256_DIGEST_LENGTH; i++) {
                sprintf(output + (i*2), "%02x", hash[i]);
        }
        output[SHA256_DIGEST_LENGTH * 2] = '\0';

        free(buffer);
        return 0;
}

int compare_checksum(char *checksum_type,char *checksum,char *path)
{
        char sha[65];
        calc_sha256(path,sha);
        // printf("compare_checksum: %s\n%s\n%s\n",checksum_type,checksum,sha);
        return strcmp(checksum,sha);
}

int check_rpm_exists(char *targetdir, struct rpm rpm){
        // check if rpm exists in targetdir with same size/checksum
        struct stat rpmstat;
        char *path;
        int retval = 0;
        asprintf(&path,"%s/%s",targetdir,rpm.location);
        if (lstat(path,&rpmstat) == -1) {
                // file not found
                goto free_path;
        } else if (rpmstat.st_size != rpm.size) {
                // check size
                printf("DEST found but size wrong: file size: %ld rpm size: %ld\n",rpmstat.st_size,rpm.size);
                printf("unlinking(%s)\n",path);
                unlink(path);
                goto free_path;
        } else if ( compare_checksum(rpm.checksum_type,rpm.checksum,path) != 0) {
                // size is same but checksums differ
                printf("DEST found with same size: file size: %ld rpm size: %ld\n",rpmstat.st_size,rpm.size);
                printf("unlinking(%s)\n",path);
                unlink(path);
                goto free_path;
        }
        retval = 1;
free_path:
        free(path);
        return retval;
}

int xferinfo(void *p, curl_off_t dltotal, curl_off_t dlnow, curl_off_t ultotal, curl_off_t ulnow)
{
        struct myprogress *myp = (struct myprogress *)p;
        CURL *curl = myp->curl;
        double curtime = 0;

        curl_easy_getinfo(curl, CURLINFO_TOTAL_TIME, &curtime);

        if ((curtime - myp->lastruntime) >= 1 ){
                // update at least every 1 seconds
                myp->lastruntime = curtime;
                fprintf(stderr,"UP: %" CURL_FORMAT_CURL_OFF_T " of %" CURL_FORMAT_CURL_OFF_T "  DOWN: %" 
                                CURL_FORMAT_CURL_OFF_T " of %" CURL_FORMAT_CURL_OFF_T "\r",
                                ulnow, ultotal, dlnow, dltotal);
        }

        return 0;
}

void usage(void)
{
        printf("Simple rpm repo sync, compares remote and local repodata\n");
        printf("Usage: .. \n");
        printf("Flags:\n");
        printf(" -n, --noop\tdo not actually download or delete any files.\n");
        printf(" -k, --keep\tkeep files in destination which are not present in source.\n");
        printf(" -l <n>, --last <n>\tOnly download last n versions of the same rpm.\n");
        printf(" -s <url>, --source <url>\tSource URL, for example http://mirrorservice.org/sites/dl.fedoraproject.org/pub/epel/7/x86_64\n");
        printf(" -d <directory>, --destination <directory>\t Destination directory, for example .\n");

}
