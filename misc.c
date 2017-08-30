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
#include <getopt.h>

#include "misc.h"

xmlChar *prefix= (xmlChar *)"prefix";
xmlChar *href= (xmlChar *)"http://linux.duke.edu/metadata/repo";

extern struct stats stats;

struct myprogress {
        double lastruntime;
        CURL *curl;
};

size_t write_file(FILE *fp, size_t size, size_t nmemb, FILE *stream)
{
        return fwrite(fp, size, nmemb, stream);
}

int get_http_to_file(FILE *fp, char *url, bool verbose)
{
        debug(1,"get_http_to_file");
//        printf("Downloading %s\n",url);
        curl_global_init(CURL_GLOBAL_ALL);
        CURL *curl_handle;
        CURLcode res;

        curl_handle=curl_easy_init();
        curl_easy_setopt(curl_handle, CURLOPT_URL, url);
        curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION, write_file);
        curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA, fp);
        curl_easy_setopt(curl_handle, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(curl_handle, CURLOPT_FAILONERROR, 1L);
#if LIBCURL_VERSION_NUM >= 0x072000
        struct myprogress prog;
        prog.lastruntime = 0;
        prog.curl = curl_handle;
        if (verbose) {
                curl_easy_setopt(curl_handle, CURLOPT_XFERINFOFUNCTION, xferinfo);
                curl_easy_setopt(curl_handle, CURLOPT_XFERINFODATA, &prog);
                curl_easy_setopt(curl_handle, CURLOPT_NOPROGRESS, 0L);
        }
#endif
        if (certfile)  curl_easy_setopt(curl_handle, CURLOPT_SSLCERT, certfile);
        if (keyfile)  curl_easy_setopt(curl_handle, CURLOPT_SSLKEY, keyfile);
        if (cafile)  curl_easy_setopt(curl_handle, CURLOPT_CAINFO, cafile);

        res=curl_easy_perform(curl_handle);
        if (res != CURLE_OK){
                fprintf(stderr,"curl_easy_perform() failed: %s\n",curl_easy_strerror(res));
                exit(1);
        }
        stats.downloaded++;
        double dl;
        CURLcode res_info = curl_easy_getinfo(curl_handle, CURLINFO_SIZE_DOWNLOAD, &dl);
        if(!res_info) {
              stats.down_bytes+=(int)dl;
        }

        curl_easy_cleanup(curl_handle);
        curl_global_cleanup();
        debug(-1,"get_http_to_file done.");
        return 0;
}

int uncompress_file(char *filename, Byte **content)
{
        //  printf("in uncompress_file(%s)\n", filename);
        debug(1,"uncompress file");
//        fprintf(stderr,"DEBUG: %s\n", filename);
        gzFile gzfp;
//        debug(0,"gzopen");
        gzfp=gzopen(filename,"rb");
        if (gzfp == NULL) return 1;
        Byte *uncompr;
        uLong uncomprLen=1600;
        uLong len=uncomprLen;
//        debug(0,"calloc");
        uncompr = (Byte*)calloc(uncomprLen * sizeof(char), 1);
//        debug(0,"before while");
        while (len == uncomprLen ) {
//                debug(0,"increasing buffer");
                gzrewind(gzfp);
                uncomprLen *=2;
                free(uncompr);
                uncompr = (Byte*)calloc(uncomprLen * sizeof(char), 1);
                len=gzread(gzfp,uncompr,uncomprLen);
//                fprintf(stderr,"DEBUG: len=%lu, buf=%lu\n",len, uncomprLen);
        }
//        debug(0,"gzclose");
        gzclose(gzfp);
        *content = uncompr;
        debug(-1,"uncompress file done");
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
        debug(0,"in getnodeset");

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

        debug(0,"xmlXPathEvalExpression start");
        result = xmlXPathEvalExpression(xpath, context);
        debug(0,"xmlXPathEvalExpression done");
        xmlXPathFreeContext(context);
        if (result == NULL) {
                printf("Error in xmlXPathEvalExpression\n");
                return NULL;
        }
        if(xmlXPathNodeSetIsEmpty(result->nodesetval)){
                xmlXPathFreeObject(result);
//                printf("No result for %s\n", xpath);
                return NULL;
        }
        return result;
}

int get_href_from_xml(char *repomd_xml, char *data_type, char **postfix)
{
        xmlDocPtr doc;
        int retval = 0;
        doc = xmlReadMemory(repomd_xml,strlen(repomd_xml),"noname.xml",NULL,0);
        if (doc == NULL) {
                perror("get_primary_url: Failed to parse.\n");
                puts(repomd_xml);
                return 1;
        }

        xmlNodeSetPtr nodeset;
        // xmlChar *xpath = (xmlChar*) "//prefix:data[@type='primary']/prefix:location"; 
        char *xpath;
        asprintf(&xpath, "//prefix:data[@type='%s']/prefix:location", data_type); 
        xmlXPathObjectPtr result;

        result = getnodeset(doc, href, (xmlChar *)xpath);
/*
   <data type="group">
    <checksum type="sha256">e9b3b52d52c8effd364e6086d2845124296385f62ae67c16684015b947650049</checksum>
    <location href="repodata/e9b3b52d52c8effd364e6086d2845124296385f62ae67c16684015b947650049-comps-epel7.xml"/>
    <timestamp>1502691632</timestamp>
    <size>1349657</size>
  </data>
*/
        if (result && result->nodesetval->nodeNr == 1 ) {
                nodeset = result->nodesetval;
                xmlChar *href = xmlGetProp(nodeset->nodeTab[0],(xmlChar *)"href");
                *postfix = strdup((char *)href);
//                printf("POSTFIX=%s\n",href);
        } else {
//                fprintf(stderr,"No results for %s\n", data_type);
                retval = 1;
        }
        xmlXPathFreeObject(result);
        xmlFreeDoc(doc);
        free(xpath);

        return retval;
}

int get_xml(char *url, char **xml)
{
        // get and optionally uncompress xml to string
        // printf("in get_xml: %s\n",url);
        debug(0,"get_xml");
        Byte *content;
        int retval=0;
        if ( url[0] == '/') {
                // FILE *fp = fopen(url,"r");
//                debug(0,"uncompress file start");
//                fprintf(stderr,"DEBUG uncompress file %s\n", url);
                retval = uncompress_file(url, &content);
//                debug(0,"uncompressed file");
                // fclose(fp);
        } else {
                char *tempfile = tmpnam(NULL);
                FILE *fp = fopen(tempfile,"w+b");
//                debug(0,"get http_to_file");
                get_http_to_file(fp, url, 0);
                fclose(fp);
//                debug(0,"uncompress");
                retval = uncompress_file(tempfile, &content);
//                debug(0,"gzfp");
                unlink(tempfile);
                // printf("CONTENT=%s\n",content);
        }
        *xml = (char *)content;
        return retval;
}

int get_repomd_xml(char *repo, char **xml)
{
        char *repomd_url;
        int retval = 0;
        asprintf(&repomd_url, "%s/repodata/repomd.xml", repo);
        retval = get_xml(repomd_url, xml);
        free(repomd_url);
        if (retval != 0) return 1;

        return retval;
}

int get_primary_xml(char *repo, char **primary_xml)
{
        // printf("in get_primary_xml: %s\n",repo);
        char *xml;
        int retval = 0;

        retval = get_repomd_xml(repo, &xml);
        if (retval != 0) return 1;

        int repofiles_size;
        struct repofile *repofiles;
        get_repofiles_from_repomd(xml, &repofiles, &repofiles_size);
        printf("REPOFILES_SIZE=%d\n", repofiles_size);
        printf("repofiles[0].name=%s\n",repofiles[0].name);
        char *postfix;
        char *primary_url;
        char *group;
        if ( get_href_from_xml(xml, "group", &group) == 0) {
                fprintf(stderr,"COMPS/GROUP: %s\n",group);
                group_file = strdup(group);
                free(group);
        }
        char *updateinfo;
        if ( get_href_from_xml(xml, "updateinfo", &updateinfo) == 0) {
//                fprintf(stderr,"UPDATEINFO: %s\n",updateinfo);
                free(updateinfo);
        }
        get_href_from_xml(xml, "primary", &postfix);
        asprintf(&primary_url,"%s/%s",repo,postfix);

        retval = get_xml(primary_url, &xml);
        free(postfix);
        free(primary_url);

        *primary_xml = xml;
        return retval;
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


int ensure_dir(char *basedir, char *location)
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
        if (noop)
                return 0;
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


int calc_sha1(char *path, char *output)
{
        FILE *file = fopen(path,"rb");
        if (!file) return -1;
        unsigned char hash[SHA_DIGEST_LENGTH];
        SHA_CTX sha1;
        SHA1_Init(&sha1);
        const int bufsize = 32 * 1024;
        char *buffer = malloc(bufsize*sizeof(*buffer));
        if (!buffer) return 01;
        int bytesread=0;
        while((bytesread = fread(buffer, 1,bufsize, file))) {
                SHA1_Update(&sha1, buffer, bytesread);
        }
        SHA1_Final(hash, &sha1);

        int i;
        for (i=0;i<SHA_DIGEST_LENGTH; i++) {
                sprintf(output + (i*2), "%02x", hash[i]);
        }
        output[SHA_DIGEST_LENGTH * 2] = '\0';

        free(buffer);
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
        if (strcmp(checksum_type,"sha256") == 0) {
                char sha[SHA256_DIGEST_LENGTH * 2 + 1];
                calc_sha256(path,sha);
//                printf("compare_checksum: %s\n%s\n%s\n",checksum_type,checksum,sha);
                return strcmp(checksum,sha);
        } else if (strcmp(checksum_type,"sha") == 0 || strcmp(checksum_type,"sha1") == 0) {
                char sha[SHA_DIGEST_LENGTH * 2 + 1];
                calc_sha1(path,sha);
//                printf("compare_checksum: %s\n%s\n%s\n",checksum_type,checksum,sha);
                return strcmp(checksum,sha);
        } else {
                printf("Unsupported checksum type %s, not comparing\n",checksum_type);
                return 0;
        }
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
                if (noop) {
                        printf("NOOP: would unlink(%s)\n",path);
                } else {
                        printf("unlinking(%s)\n",path);
                        unlink(path);
                }
                goto free_path;
        } else if ( compare_checksum(rpm.checksum_type,rpm.checksum,path) != 0) {
                // size is same but checksums differ
                printf("DEST found with same size: file size: %ld rpm size: %ld\n",rpmstat.st_size,rpm.size);
                if (noop) {
                        printf("NOOP: would unlink(%s)\n",path);
                } else {
                        printf("unlinking(%s)\n",path);
                        unlink(path);
                }
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
                fprintf(stderr,"%ld of %ld %2.2f%%\r", dlnow, dltotal, 100.0 * dlnow  / dltotal );
        }

        return 0;
}

void usage(void)
{
        printf("Simple rpm repo sync, compares remote and local repodata\n");
        printf("Do not forget to run createrepo on the local copy afterward.\n");
        printf("Usage: .. \n");
        printf("Flags:\n");
        printf(" -n, --noop\tdo not actually download or delete any files.\n");
        printf(" -o, --other_metadata\t.\n");
        printf(" -c, --comps\t.\n");
        printf(" -p, --purge\tpurge files in destination which are not present in source.\n");
        printf(" -l <n>, --last <n>\tOnly download last n versions of the same rpm. Defaults to 0 for all.\n");
        printf(" -s <url>, --source <url>\tSource URL, for example \\\n\t\thttp://mirrorservice.org/sites/dl.fedoraproject.org/pub/epel/7/x86_64\n");
        printf(" -d <directory>, --destination <directory>\t Destination directory, for example .\n");

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


int get_options(int argc, char **argv, char **src_repo_ptr, char **dst_repo_ptr)
{
        int opt;
        opterr = 0;
        static struct option long_options[]= {
                {"source", required_argument,0,'s'},
                {"destination", required_argument,0,'d'},
                {"purge", no_argument, 0, 'p'},
                {"noop", no_argument, 0, 'n'},
                {"comps", no_argument,0,'c'},
                {"other_metadata", no_argument,0,'o'},
                {"last", required_argument,0,'l'},
                {"key", required_argument,0,'K'},
                {"cert", required_argument,0,'C'},
                {"ca", required_argument,0,'A'},
                {0,0,0,0},
        };
        int option_index=0;

        while ((opt = getopt_long(argc, argv, "s:d:knl:K:C:A:c", long_options, &option_index)) != -1) {
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
                                        *src_repo_ptr = optarg;
                                } else {
                                        printf("source needs an option\n");
                                        return 1;
                                }
                                break;
                        case 'd':
                                if (optarg) {
                                        *dst_repo_ptr = optarg;
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
                        case 'p':
                                keep = 0;
                                break;
                        case 'n':
                                noop = 1;
                                break;
                        case 'c':
                                getcomps = 1;
                                break;
                        case '0':
                                getothermd = 1;
                                break;
                        case 'K':
                                if (optarg) {
                                        keyfile = optarg;
                                } else {
                                        printf("keyfile needs an option\n");
                                        return 1;
                                }
                                break;
                        case 'C':
                                if (optarg) {
                                        certfile = optarg;
                                } else {
                                        printf("certfile needs an option\n");
                                        return 1;
                                }
                                break;
                        case 'A':
                                if (optarg) {
                                        cafile = optarg;
                                } else {
                                        printf("cafile needs an option\n");
                                        return 1;
                                }
                                break;
                        case '?':
                                break;
                        default:
                                abort();
                }
        }
        return 0;
}
