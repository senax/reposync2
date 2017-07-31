# reposync2
A simple reposync clone which runs a bit faster than the normal python one.

# Usage
```
./reposync2
Simple rpm repo sync, compares remote and local repodata
Do not forget to run createrepo on the local copy afterward.
Usage: ..
Flags:
 -n, --noop     do not actually download or delete any files.
 -k, --keep     keep files in destination which are not present in source.
 -l <n>, --last <n>     Only download last n versions of the same rpm. Defaults to 0 for all.
 -s <url>, --source <url>       Source URL, for example \
                http://mirrorservice.org/sites/dl.fedoraproject.org/pub/epel/7/x86_64
 -d <directory>, --destination <directory>       Destination directory, for example .

```

# Example of performance
epel7 is already updated, repodata locally is a little bit behind but still decent performance.

```$ time make epel
./reposync2 -s http://mirrorservice.org/sites/dl.fedoraproject.org/pub/epel/7/x86_64/ -d /vol1/Linux/dist/epel/7/x86_64 -k -l 2
get_http_to_file: http://mirrorservice.org/sites/dl.fedoraproject.org/pub/epel/7/x86_64//repodata/repomd.xml
get_http_to_file: http://mirrorservice.org/sites/dl.fedoraproject.org/pub/epel/7/x86_64//repodata/72ef0a25b29a981d6d0bf196a7e61bbe1c730f40c8abce104f7a54d30d07b0f9-primary.xml.gz
Skipping down of z/Zim-0.67-1.el7.noarch.rpm already there but not in repodata.
Skipping down of a/abi-dumper-1.0-1.el7.noarch.rpm already there but not in repodata.
...
Skipping down of v/vim-halibut-1.2-1.el7.noarch.rpm already there but not in repodata.
Skipping down of y/yad-0.39.0-1.el7.x86_64.rpm already there but not in repodata.
Done.

real    0m3.385s
user    0m2.048s
sys     0m0.183s
```

Python-reposync takes around two minutes for the same action.

# Todo
- download comps.xml
- client certificate handling
- general cleanup
- sha1 / sha support
- remove trailing / from source/dest

