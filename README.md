# FSCHECK
File System checker for xv6


## Usage :

gcc -Wall -Werror fcheck.c -o fcheck


## Conditions
| SI No | Condition | Error Message                                                                |
| ----- | --------------------------------------------------------------------------------------------------------------------------------------------------- | ---------------------------------------------------------------------------- |
| 1     | Each inode is either unallocated or one of the valid types (T\_FILE, T\_DIR, T\_DEV) | ERROR: bad inode.                                                            |
| 2     | For in-use inodes, each block address that is used by the inode is valid | ERROR: bad direct address in inode.<br>ERROR: bad indirect address in inode. |
| 3     | Root directory exists, its inode number is 1, and the parent of the root directory is itself                                                        | ERROR: root directory does not exist.                                        |
| 4     | Each directory contains . and .. entries, and the . entry points to the directory itself | ERROR: directory not properly formatted.                                     |
| 5     | For in-use inodes, each block address in use is also marked in use in he bitmap | ERROR: address used by inode but marked free in bitmap.                      |
| 6     | For blocks marked in-use in bitmap, the block should actually be in-use in an inode or indirect block somewhere                                     | ERROR: bitmap marks block in use but it is not in use.                       |
| 7     | For in-use inodes, each direct address in use is only use | ERROR: direct address used more than once.                                   |
| 8     | For in-use inodes, each indirect address in use is only used once | ERROR: indirect address used more than once.                                 |
| 9     | For all inodes marked in use, each must be referred to in at least one directory | ERROR: inode marked use but not found in a directory.                        |
| 10    | For each inode number that is referred to in a valid directory, it is actually marked in use                                                        | ERROR: inode referred to in directory
but marked free.                       |
| 11    | Reference counts (number of links) for regular files match the number of times file is referred to in directories (i.e., hard links work correctly) | ERROR: bad reference count for file.
|
| 12    | No extra links allowed for directories (each directory only appears in one other directory)                                                         | ERROR: directory appears more than once in file system.                      |
