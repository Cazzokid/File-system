# File-system
In all computer systems there is a need for persistent long-term storage of information. This persistent storage system also needs
to be of substantial size to be able to hold large quantities of data. Different types of disks constitute this secondary storage.
Further, we want the information to be accessible from several processes, thus the information storage must be independent of
individual processes.
Thus, we have three essential requirements for long-term information storage [1]:
1. It must be possible to store a very large amount of information.
2. The information must survive the termination of the process using it.
3. Multiple processes must be able to access the information at once.

# Running this project:

# On Windows:

g++ -o shell main.cpp shell.cpp fs.cpp disk.cpp

shell.exe

[ Available commands:
format, create, cat, ls, cp, mv, rm, append, mkdir, cd, pwd, chmod, help, quit]

# On Linux:

g++ -o shell main.cpp shell.cpp fs.cpp disk.cpp

./shell

[ Available commands:
format, create, cat, ls, cp, mv, rm, append, mkdir, cd, pwd, chmod, help, quit]
